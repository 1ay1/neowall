#ifndef NEOWALL_IPC_H
#define NEOWALL_IPC_H

/* The control socket: how the CLI talks to a running daemon.
 *
 * Before, every command was a signal (SIGUSR1 = next, SIGRTMIN = set-index,
 * ...) plus, for commands with an argument, a side file the daemon had to go
 * and read. That had three problems: signals carry no argument, so the file
 * dance raced; there was no reply, so the CLI printed "Skipping..." whether or
 * not anything happened; and the CLI found the daemon through a pid file,
 * which can be stale.
 *
 * Now there is one unix socket at $XDG_RUNTIME_DIR/neowall.sock. A client
 * connects, writes one request line, reads one reply line, and closes:
 *
 *   request:  <verb>[ <arg>]\n         e.g. "next\n", "set 3\n", "set-terminal htop\n"
 *   reply:    ok[ <text>]\n | err <text>\n
 *
 * One line each way keeps it scriptable (`echo next | socat - UNIX:...`) and
 * means a client can never wedge the daemon: a request has a size limit and a
 * connection that doesn't finish its line in time is dropped.
 *
 * The daemon side (ipc_server_*) is driven by the runtime: the listening fd and
 * each connection are watched fds, and a complete request becomes a message
 * the program folds. The reply is written when update() has decided the
 * answer. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Longest request line, including the newline. Big enough for a terminal
 * command; anything longer is refused. */
#define NW_IPC_MAX_LINE 1024

/* $XDG_RUNTIME_DIR/neowall.sock (or the secure fallback dir). Static buffer;
 * empty string when no private directory is available. */
const char *nw_ipc_socket_path(void);

/* ── client ───────────────────────────────────────────────────────────── */

typedef enum {
    NW_IPC_OK = 0,
    NW_IPC_ERR,          /* the daemon answered "err ..." */
    NW_IPC_NO_DAEMON,    /* nothing is listening */
    NW_IPC_FAILED,       /* the exchange itself broke (timeout, I/O) */
} nw_ipc_status;

/* Send one request and wait for the reply (bounded by `timeout_ms`). `reply`
 * receives the text after "ok " / "err " (may be empty). */
nw_ipc_status nw_ipc_request(const char *verb, const char *arg,
                             char *reply, size_t reply_size, int timeout_ms);

/* ── server (daemon) ──────────────────────────────────────────────────── */

typedef struct rt_loop rt_loop;

/* A parsed request, handed to the program as a message. `conn` identifies
 * the connection the reply must go to; pass it back to ipc_server_reply. */
typedef struct {
    uint32_t conn;
    char verb[32];
    char arg[NW_IPC_MAX_LINE];
} nw_ipc_request_t;

/* Called on the loop thread for each complete, well-formed request. */
typedef void (*nw_ipc_on_request)(void *ctx, rt_loop *loop, const nw_ipc_request_t *req);

typedef struct nw_ipc_server nw_ipc_server;

/* Bind the socket and start accepting on `loop`. Refuses to steal the socket
 * from a live daemon (a connect that succeeds means someone is listening); a
 * stale socket file from a crashed daemon is replaced. NULL on failure. */
nw_ipc_server *ipc_server_start(rt_loop *loop, nw_ipc_on_request on_request, void *ctx);

/* Answer request `conn` and close it. `ok` selects "ok"/"err"; `text` may be
 * NULL. A conn that already went away is ignored. */
void ipc_server_reply(nw_ipc_server *srv, uint32_t conn, bool ok, const char *text);

/* Stop accepting, close every connection, and unlink the socket. */
void ipc_server_stop(nw_ipc_server *srv);

#endif /* NEOWALL_IPC_H */
