/* The control socket. See include/neowall/ipc.h. */

#define _GNU_SOURCE   /* accept4, MSG_NOSIGNAL */

#include "neowall/ipc.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>

#include "neowall/neowall.h"
#include "neowall/runtime/rt.h"

/* A connection that hasn't sent a whole line in this long is dropped, so a
 * stuck or hostile client can't hold a slot forever. */
#define IPC_CLIENT_TIMEOUT_MS 2000
#define IPC_MAX_CONNS 32

const char *nw_ipc_socket_path(void) {
    static char path[sizeof(((struct sockaddr_un *)0)->sun_path)];
    const char *rt = getenv("XDG_RUNTIME_DIR");
    if (rt && rt[0]) {
        /* $XDG_RUNTIME_DIR is per-user and 0700 already. */
        int n = snprintf(path, sizeof(path), "%s/neowall.sock", rt);
        if (n > 0 && (size_t)n < sizeof(path)) return path;
    }
    const char *dir = neowall_secure_runtime_dir();
    if (dir) {
        int n = snprintf(path, sizeof(path), "%s/neowall.sock", dir);
        if (n > 0 && (size_t)n < sizeof(path)) return path;
    }
    path[0] = '\0';
    return path;
}

static bool fill_addr(struct sockaddr_un *a, const char *path) {
    memset(a, 0, sizeof(*a));
    a->sun_family = AF_UNIX;
    size_t n = strlen(path);
    if (n == 0 || n >= sizeof(a->sun_path)) return false;
    memcpy(a->sun_path, path, n + 1);
    return true;
}

static int64_t now_ms(void) { return rt_now_ns() / 1000000; }

/* Write all of buf, giving up at `deadline`. */
static bool write_all(int fd, const char *buf, size_t len, int64_t deadline) {
    while (len) {
        ssize_t w = send(fd, buf, len, MSG_NOSIGNAL);
        if (w > 0) { buf += w; len -= (size_t)w; continue; }
        if (w < 0 && errno == EINTR) continue;
        if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            int64_t left = deadline - now_ms();
            if (left <= 0) return false;
            struct pollfd p = {.fd = fd, .events = POLLOUT};
            if (poll(&p, 1, (int)left) <= 0) return false;
            continue;
        }
        return false;
    }
    return true;
}

/* ── client ───────────────────────────────────────────────────────────── */

nw_ipc_status nw_ipc_request(const char *verb, const char *arg,
                             char *reply, size_t reply_size, int timeout_ms) {
    if (reply && reply_size) reply[0] = '\0';
    struct sockaddr_un addr;
    if (!fill_addr(&addr, nw_ipc_socket_path())) return NW_IPC_NO_DAEMON;

    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd < 0) return NW_IPC_FAILED;
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        int e = errno;
        close(fd);
        /* No socket, or one nobody is listening on: no daemon. */
        return (e == ENOENT || e == ECONNREFUSED) ? NW_IPC_NO_DAEMON : NW_IPC_FAILED;
    }

    char line[NW_IPC_MAX_LINE];
    int n = arg && arg[0] ? snprintf(line, sizeof(line), "%s %s\n", verb, arg)
                          : snprintf(line, sizeof(line), "%s\n", verb);
    if (n <= 0 || (size_t)n >= sizeof(line)) {
        close(fd);
        if (reply && reply_size) snprintf(reply, reply_size, "request too long");
        return NW_IPC_FAILED;
    }

    int64_t deadline = now_ms() + timeout_ms;
    if (!write_all(fd, line, (size_t)n, deadline)) {
        close(fd);
        return NW_IPC_FAILED;
    }
    shutdown(fd, SHUT_WR);

    /* Read the one reply line. */
    char buf[NW_IPC_MAX_LINE];
    size_t got = 0;
    for (;;) {
        if (got + 1 >= sizeof(buf)) break;
        ssize_t r = recv(fd, buf + got, sizeof(buf) - 1 - got, 0);
        if (r > 0) {
            got += (size_t)r;
            if (memchr(buf, '\n', got)) break;
            continue;
        }
        if (r == 0) break;
        if (errno == EINTR) continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            int64_t left = deadline - now_ms();
            if (left <= 0) { close(fd); return NW_IPC_FAILED; }
            struct pollfd p = {.fd = fd, .events = POLLIN};
            if (poll(&p, 1, (int)left) <= 0) { close(fd); return NW_IPC_FAILED; }
            continue;
        }
        close(fd);
        return NW_IPC_FAILED;
    }
    close(fd);
    buf[got] = '\0';
    char *nl = strchr(buf, '\n');
    if (!nl) return NW_IPC_FAILED;
    *nl = '\0';

    bool ok = strncmp(buf, "ok", 2) == 0 && (buf[2] == '\0' || buf[2] == ' ');
    bool err = strncmp(buf, "err", 3) == 0 && (buf[3] == '\0' || buf[3] == ' ');
    if (!ok && !err) return NW_IPC_FAILED;
    const char *text = buf + (ok ? 2 : 3);
    if (*text == ' ') text++;
    if (reply && reply_size) snprintf(reply, reply_size, "%s", text);
    return ok ? NW_IPC_OK : NW_IPC_ERR;
}

/* ── server ───────────────────────────────────────────────────────────── */

typedef struct {
    uint32_t id;           /* 0 = free; stable for the reply */
    int fd;
    uint32_t watch;
    int64_t started_ms;
    char buf[NW_IPC_MAX_LINE];
    size_t len;
    bool requested;        /* a request went to the program; reply pending */
} ipc_conn;

struct nw_ipc_server {
    rt_loop *loop;
    int listen_fd;
    uint32_t listen_watch;
    char path[sizeof(((struct sockaddr_un *)0)->sun_path)];
    nw_ipc_on_request on_request;
    void *ctx;
    ipc_conn conns[IPC_MAX_CONNS];
    uint32_t next_id;
};

static ipc_conn *conn_by_id(nw_ipc_server *s, uint32_t id) {
    if (!id) return NULL;
    for (int i = 0; i < IPC_MAX_CONNS; i++)
        if (s->conns[i].id == id) return &s->conns[i];
    return NULL;
}

static void conn_close(nw_ipc_server *s, ipc_conn *c) {
    if (c->watch) rt_unwatch(s->loop, c->watch);
    if (c->fd >= 0) close(c->fd);
    memset(c, 0, sizeof(*c));
    c->fd = -1;
}

static void conn_reply(nw_ipc_server *s, ipc_conn *c, bool ok, const char *text) {
    char line[NW_IPC_MAX_LINE];
    int n = text && text[0] ? snprintf(line, sizeof(line), "%s %s\n", ok ? "ok" : "err", text)
                            : snprintf(line, sizeof(line), "%s\n", ok ? "ok" : "err");
    if (n > 0) {
        if ((size_t)n >= sizeof(line)) {   /* truncated: keep the newline */
            n = (int)sizeof(line) - 1;
            line[n - 1] = '\n';
        }
        (void)write_all(c->fd, line, (size_t)n, now_ms() + 200);
    }
    conn_close(s, c);
}

/* Parse a complete line into a request; false (and an err reply) if it's not
 * well-formed. */
static bool parse(ipc_conn *c, nw_ipc_request_t *req) {
    char *nl = memchr(c->buf, '\n', c->len);
    *nl = '\0';
    if (nl > c->buf && nl[-1] == '\r') nl[-1] = '\0';
    char *sp = strchr(c->buf, ' ');
    size_t vlen = sp ? (size_t)(sp - c->buf) : strlen(c->buf);
    if (vlen == 0 || vlen >= sizeof(req->verb)) return false;
    memcpy(req->verb, c->buf, vlen);
    req->verb[vlen] = '\0';
    snprintf(req->arg, sizeof(req->arg), "%s", sp ? sp + 1 : "");
    req->conn = c->id;
    return true;
}

static void on_conn(void *ctx, rt_loop *loop, int fd, unsigned ev) {
    (void)loop;
    nw_ipc_server *s = ctx;
    ipc_conn *c = NULL;
    for (int i = 0; i < IPC_MAX_CONNS; i++)
        if (s->conns[i].id && s->conns[i].fd == fd) { c = &s->conns[i]; break; }
    if (!c) return;

    if (c->requested) return;   /* parked until ipc_server_reply */

    if (ev & RT_READ) {
        ssize_t r = recv(fd, c->buf + c->len, sizeof(c->buf) - 1 - c->len, 0);
        if (r > 0) c->len += (size_t)r;
        if (r == 0 || (r < 0 && errno != EAGAIN && errno != EINTR)) {
            if (!memchr(c->buf, '\n', c->len)) { conn_close(s, c); return; }
        }
    } else if (ev & RT_HANGUP) {
        conn_close(s, c);
        return;
    }

    if (memchr(c->buf, '\n', c->len)) {
        nw_ipc_request_t req;
        if (!parse(c, &req)) {
            conn_reply(s, c, false, "malformed request");
            return;
        }
        c->requested = true;
        rt_watch_modify(s->loop, c->watch, 0);   /* reply is pending; stop reading */
        s->on_request(s->ctx, s->loop, &req);
        return;
    }
    if (c->len >= sizeof(c->buf) - 1) {
        conn_reply(s, c, false, "request too long");
        return;
    }
    if (now_ms() - c->started_ms > IPC_CLIENT_TIMEOUT_MS) conn_close(s, c);
}

static void on_listen(void *ctx, rt_loop *loop, int fd, unsigned ev) {
    nw_ipc_server *s = ctx;
    if (!(ev & RT_READ)) return;
    for (;;) {
        int cfd = accept4(fd, NULL, NULL, SOCK_CLOEXEC | SOCK_NONBLOCK);
        if (cfd < 0) return;   /* EAGAIN: drained */

        /* Only the user who owns the daemon may drive it. The socket lives in
         * a 0700 dir, so this is defence in depth, not the only check. */
        struct ucred cr;
        socklen_t crl = sizeof(cr);
        if (getsockopt(cfd, SOL_SOCKET, SO_PEERCRED, &cr, &crl) != 0 || cr.uid != getuid()) {
            close(cfd);
            continue;
        }

        ipc_conn *c = NULL;
        for (int i = 0; i < IPC_MAX_CONNS; i++) {
            if (!s->conns[i].id) { c = &s->conns[i]; break; }
            /* Reclaim a slot from a client that never finished its line. */
            if (!s->conns[i].requested &&
                now_ms() - s->conns[i].started_ms > IPC_CLIENT_TIMEOUT_MS) {
                conn_close(s, &s->conns[i]);
                c = &s->conns[i];
                break;
            }
        }
        if (!c) {
            const char busy[] = "err daemon busy\n";
            ssize_t w = send(cfd, busy, sizeof(busy) - 1, MSG_NOSIGNAL);
            (void)w;
            close(cfd);
            continue;
        }
        memset(c, 0, sizeof(*c));
        c->fd = cfd;
        c->id = ++s->next_id ? s->next_id : ++s->next_id;   /* never 0 */
        c->started_ms = now_ms();
        c->watch = rt_watch(loop, cfd, RT_READ, on_conn, s);
        if (!c->watch) conn_close(s, c);
    }
}

nw_ipc_server *ipc_server_start(rt_loop *loop, nw_ipc_on_request on_request, void *ctx) {
    const char *path = nw_ipc_socket_path();
    struct sockaddr_un addr;
    if (!fill_addr(&addr, path)) {
        log_error("ipc: no usable socket path");
        return NULL;
    }

    /* A socket file can be left behind by a daemon that crashed. If something
     * answers on it, a daemon is live and we must not steal it. */
    int probe = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (probe >= 0) {
        bool live = connect(probe, (struct sockaddr *)&addr, sizeof(addr)) == 0;
        close(probe);
        if (live) {
            log_error("ipc: %s is in use by another neowall", path);
            return NULL;
        }
    }
    unlink(path);

    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd < 0) {
        log_error("ipc: socket: %s", strerror(errno));
        return NULL;
    }
    mode_t old = umask(0177);   /* the socket file is 0600 */
    int b = bind(fd, (struct sockaddr *)&addr, sizeof(addr));
    umask(old);
    if (b != 0 || listen(fd, 16) != 0) {
        log_error("ipc: bind %s: %s", path, strerror(errno));
        close(fd);
        return NULL;
    }

    nw_ipc_server *s = calloc(1, sizeof(*s));
    if (!s) {
        close(fd);
        unlink(path);
        return NULL;
    }
    s->loop = loop;
    s->listen_fd = fd;
    snprintf(s->path, sizeof(s->path), "%s", path);
    s->on_request = on_request;
    s->ctx = ctx;
    for (int i = 0; i < IPC_MAX_CONNS; i++) s->conns[i].fd = -1;
    s->listen_watch = rt_watch(loop, fd, RT_READ, on_listen, s);
    if (!s->listen_watch) {
        ipc_server_stop(s);
        return NULL;
    }
    log_info("Control socket: %s", path);
    return s;
}

void ipc_server_reply(nw_ipc_server *s, uint32_t conn, bool ok, const char *text) {
    if (!s) return;
    ipc_conn *c = conn_by_id(s, conn);
    if (c) conn_reply(s, c, ok, text);
}

void ipc_server_stop(nw_ipc_server *s) {
    if (!s) return;
    for (int i = 0; i < IPC_MAX_CONNS; i++)
        if (s->conns[i].id) conn_close(s, &s->conns[i]);
    if (s->listen_watch) rt_unwatch(s->loop, s->listen_watch);
    if (s->listen_fd >= 0) close(s->listen_fd);
    /* Only remove the path if it's still ours: a newer daemon may have
     * replaced it already. */
    struct stat st;
    if (s->path[0] && lstat(s->path, &st) == 0 && S_ISSOCK(st.st_mode)) unlink(s->path);
    free(s);
}
