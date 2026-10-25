/* The control socket, end to end: a server on the rt loop, real clients. */

#define _GNU_SOURCE
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>

#include "neowall/ipc.h"
#include "neowall/runtime/rt.h"

static int failures = 0, checks = 0;
#define CHECK(c, ...) do { checks++; if (!(c)) { failures++; \
    fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
    fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)

enum { K_STOP = 1 };

typedef struct {
    nw_ipc_server *srv;
    int requests;
    char last_verb[32], last_arg[NW_IPC_MAX_LINE];
} server_t;

static void on_req(void *ctx, rt_loop *loop, const nw_ipc_request_t *r) {
    server_t *s = ctx;
    s->requests++;
    snprintf(s->last_verb, sizeof s->last_verb, "%s", r->verb);
    snprintf(s->last_arg, sizeof s->last_arg, "%s", r->arg);
    if (strcmp(r->verb, "echo") == 0) {
        ipc_server_reply(s->srv, r->conn, true, r->arg);
    } else if (strcmp(r->verb, "stop") == 0) {
        ipc_server_reply(s->srv, r->conn, true, NULL);
        rt_msg m = {.kind = K_STOP};
        rt_send(loop, &m);
    } else {
        ipc_server_reply(s->srv, r->conn, false, "unknown");
    }
}

static bool upd(void *self, rt_loop *l, const rt_msg *m) {
    (void)self; (void)l;
    return m->kind != K_STOP;
}

/* The client side runs on its own thread while the loop serves. */
typedef struct { int ok; } client_result;

static int raw(const char *send, char *out, size_t outsz) {
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    struct sockaddr_un a = {.sun_family = AF_UNIX};
    snprintf(a.sun_path, sizeof a.sun_path, "%s", nw_ipc_socket_path());
    if (connect(fd, (struct sockaddr *)&a, sizeof a) != 0) { close(fd); return -1; }
    if (write(fd, send, strlen(send)) < 0) { close(fd); return -1; }
    ssize_t n = read(fd, out, outsz - 1);
    out[n > 0 ? n : 0] = '\0';
    close(fd);
    return (int)n;
}

static void *client(void *arg) {
    client_result *res = arg;
    char reply[256];

    CHECK(nw_ipc_request("echo", "hello world", reply, sizeof reply, 2000) == NW_IPC_OK,
          "echo succeeds");
    CHECK(strcmp(reply, "hello world") == 0, "reply text round-trips, got '%s'", reply);

    CHECK(nw_ipc_request("nope", NULL, reply, sizeof reply, 2000) == NW_IPC_ERR,
          "unknown verb is an err reply");
    CHECK(strcmp(reply, "unknown") == 0, "err text, got '%s'", reply);

    /* A client that never finishes its line doesn't block the next one. */
    int stuck = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    struct sockaddr_un a = {.sun_family = AF_UNIX};
    snprintf(a.sun_path, sizeof a.sun_path, "%s", nw_ipc_socket_path());
    CHECK(connect(stuck, (struct sockaddr *)&a, sizeof a) == 0, "stuck client connects");
    CHECK(write(stuck, "ech", 3) == 3, "stuck client writes half a line");
    CHECK(nw_ipc_request("echo", "after", reply, sizeof reply, 2000) == NW_IPC_OK &&
          strcmp(reply, "after") == 0, "a stuck client doesn't wedge the server");
    close(stuck);

    char out[256];
    char big[3000];
    memset(big, 'x', sizeof big - 2);
    big[sizeof big - 2] = '\n';
    big[sizeof big - 1] = '\0';
    raw(big, out, sizeof out);
    CHECK(strncmp(out, "err request too long", 20) == 0, "oversize refused, got '%s'", out);

    raw("\n", out, sizeof out);
    CHECK(strncmp(out, "err malformed", 13) == 0, "empty line refused, got '%s'", out);

    raw("echo crlf\r\n", out, sizeof out);
    CHECK(strcmp(out, "ok crlf\n") == 0, "CRLF tolerated, got '%s'", out);

    CHECK(nw_ipc_request("stop", NULL, reply, sizeof reply, 2000) == NW_IPC_OK, "stop");
    res->ok = 1;
    return NULL;
}

int main(void) {
    char dir[] = "/tmp/nw-ipc-test-XXXXXX";
    CHECK(mkdtemp(dir) != NULL, "mkdtemp");
    setenv("XDG_RUNTIME_DIR", dir, 1);

    /* No daemon yet: the client reports that, not a hang. */
    char reply[64];
    CHECK(nw_ipc_request("echo", "x", reply, sizeof reply, 500) == NW_IPC_NO_DAEMON,
          "no socket means no daemon");

    rt_loop *l = rt_loop_new();
    server_t s = {0};
    s.srv = ipc_server_start(l, on_req, &s);
    CHECK(s.srv != NULL, "server starts");

    struct stat st;
    CHECK(stat(nw_ipc_socket_path(), &st) == 0 && (st.st_mode & 0777) == 0600,
          "socket is 0600, got %o", st.st_mode & 0777);

    /* A second server must not steal a live socket. */
    rt_loop *l2 = rt_loop_new();
    server_t s2 = {0};
    CHECK(ipc_server_start(l2, on_req, &s2) == NULL, "a live socket isn't stolen");
    rt_loop_free(l2);

    client_result res = {0};
    pthread_t th;
    pthread_create(&th, NULL, client, &res);
    rt_program prog = {.self = &s, .update = upd};
    rt_run(l, &prog);
    pthread_join(th, NULL);
    CHECK(res.ok, "client ran to the end");

    ipc_server_stop(s.srv);
    CHECK(stat(nw_ipc_socket_path(), &st) != 0, "socket removed on stop");
    rt_loop_free(l);

    /* A stale socket file (a crashed daemon) is replaced, not refused. */
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un a = {.sun_family = AF_UNIX};
    snprintf(a.sun_path, sizeof a.sun_path, "%s", nw_ipc_socket_path());
    CHECK(bind(fd, (struct sockaddr *)&a, sizeof a) == 0, "make a stale socket");
    close(fd);   /* bound but nobody listening: a crashed daemon's leftover */
    l = rt_loop_new();
    s.srv = ipc_server_start(l, on_req, &s);
    CHECK(s.srv != NULL, "a stale socket is replaced");
    ipc_server_stop(s.srv);
    rt_loop_free(l);

    rmdir(dir);
    printf("%s: %d checks, %d failures\n", failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 1 : 0;
}
