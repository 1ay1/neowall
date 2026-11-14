/* control: requests off the control socket. Each verb becomes a message, so a
 * socket command and its signal take the same path through update. The
 * checks here only read the model, to answer honestly. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/un.h>

#include "app_internal.h"
#include "neowall/app/app.h"
#include "neowall/output/output.h"
#include "version.h"
#include "neowall/core/util.h"

const char *app_socket_path(void) {
    static char path[sizeof(((struct sockaddr_un *)0)->sun_path)];
    /* $XDG_RUNTIME_DIR is per-user and 0700 already; else make our own. */
    const char *d = getenv("XDG_RUNTIME_DIR");
    if (!d || !d[0]) d = neowall_secure_runtime_dir();
    int n = d ? snprintf(path, sizeof(path), "%s/neowall.sock", d) : -1;
    if (n < 0 || (size_t)n >= sizeof(path)) path[0] = '\0';
    return path;
}

/* What the outputs can do right now. */
struct output_summary {
    size_t longest_cycle;   /* largest cycle on any output (0/1 = nothing to cycle) */
    bool has_terminal;
};

static struct output_summary summarize_outputs(struct neowall_state *state) {
    struct output_summary s = {0};
    pthread_rwlock_rdlock(&state->output_list_lock);
    for (struct output_state *o = state->outputs; o; o = o->next) {
        if (o->config->cycle && o->config->cycle_count > s.longest_cycle) {
            s.longest_cycle = o->config->cycle_count;
        }
        if (o->config->type == WALLPAPER_TERMINAL) s.has_terminal = true;
    }
    pthread_rwlock_unlock(&state->output_list_lock);
    return s;
}

/* `set <index>`: an index no output can take is an error, not an "ok" the
 * daemon then quietly drops. */
static void set_index(struct app *a, rt_loop *loop, const nw_ipc_request_t *r) {
    char text[NW_IPC_MAX_LINE];
    char *end = NULL;
    long idx = strtol(r->arg, &end, 10);
    if (!r->arg[0] || *end || idx < 0 || idx > 100000) {
        ipc_server_reply(a->host.ipc, r->conn, false, "usage: set <index>");
        return;
    }
    size_t most = summarize_outputs(a->state).longest_cycle;
    if (most <= 1) {
        ipc_server_reply(a->host.ipc, r->conn, false,
                         "nothing to set: only one wallpaper or shader is configured");
        return;
    }
    if ((size_t)idx >= most) {
        snprintf(text, sizeof(text), "index %ld is out of range (0-%zu)", idx, most - 1);
        ipc_server_reply(a->host.ipc, r->conn, false, text);
        return;
    }
    app_send(loop, NW_MSG_SET_INDEX, 0, idx);
    snprintf(text, sizeof(text), "setting wallpaper %ld", idx);
    ipc_server_reply(a->host.ipc, r->conn, true, text);
}

/* `set-terminal <command>`. The message carries a copy of the command. */
static void set_terminal(struct app *a, rt_loop *loop, const nw_ipc_request_t *r) {
    if (!r->arg[0] || strlen(r->arg) >= APP_TERMINAL_CMD_MAX) {
        ipc_server_reply(a->host.ipc, r->conn, false,
                         r->arg[0] ? "command too long" : "usage: set-terminal <command>");
        return;
    }
    if (!summarize_outputs(a->state).has_terminal) {
        ipc_server_reply(a->host.ipc, r->conn, false, "no output is running a terminal wallpaper");
        return;
    }
    if (!app_send_terminal(loop, r->arg)) {
        ipc_server_reply(a->host.ipc, r->conn, false, "out of memory");
        return;
    }
    ipc_server_reply(a->host.ipc, r->conn, true, "swapping terminal wallpaper");
}

void app_on_ipc_request(void *ctx, rt_loop *loop, const nw_ipc_request_t *r) {
    static const struct { const char *verb; uint32_t kind; const char *done; } simple[] = {
        {"next",          NW_MSG_NEXT,          "skipping to next wallpaper"},
        {"pause",         NW_MSG_CYCLE_PAUSE,   "cycling paused"},
        {"resume",        NW_MSG_CYCLE_RESUME,  "cycling resumed"},
        {"pause-shader",  NW_MSG_SHADER_PAUSE,  "shader animation paused"},
        {"resume-shader", NW_MSG_SHADER_RESUME, "shader animation resumed"},
        {"kill",          NW_MSG_QUIT,          "stopping"},
    };
    struct app *a = ctx;
    char text[NW_IPC_MAX_LINE];

    if (strcmp(r->verb, "ping") == 0) {
        snprintf(text, sizeof(text), "neowall %s pid %d", NEOWALL_VERSION_STRING, (int)getpid());
        ipc_server_reply(a->host.ipc, r->conn, true, text);
        return;
    }

    if (strcmp(r->verb, "next") == 0 && summarize_outputs(a->state).longest_cycle <= 1) {
        ipc_server_reply(a->host.ipc, r->conn, false,
                         "nothing to cycle: only one wallpaper or shader is configured");
        return;
    }

    /* The client wants to know if the new config took, so the connection
     * stays open and the reload command answers it. */
    if (strcmp(r->verb, "reload") == 0) {
        app_send(loop, NW_MSG_RELOAD, 0, r->conn);
        return;
    }

    for (size_t i = 0; i < sizeof(simple) / sizeof(simple[0]); i++) {
        if (strcmp(r->verb, simple[i].verb) != 0) continue;
        /* Reply now: "kill" stops the loop, and the client is owed an answer
         * before that. */
        ipc_server_reply(a->host.ipc, r->conn, true, simple[i].done);
        app_send(loop, simple[i].kind, 0, 0);
        return;
    }

    if (strcmp(r->verb, "set") == 0) {
        set_index(a, loop, r);
        return;
    }
    if (strcmp(r->verb, "set-terminal") == 0) {
        set_terminal(a, loop, r);
        return;
    }

    snprintf(text, sizeof(text), "unknown command '%s'", r->verb);
    ipc_server_reply(a->host.ipc, r->conn, false, text);
}
