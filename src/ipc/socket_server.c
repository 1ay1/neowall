/*
 * NeoWall IPC Socket Server (Daemon Side)
 */

#include "socket.h"
#include "protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>
#include <sys/stat.h>

#define MAX_CLIENTS 10

struct ipc_server {
    int listen_fd;
    char socket_path[256];
    ipc_command_handler_t handler;
    void *user_data;
    int client_fds[MAX_CLIENTS];
    int num_clients;
};

const char *ipc_get_socket_path(void) {
    static char path[256];
    const char *runtime_dir = getenv("XDG_RUNTIME_DIR");

    if (runtime_dir) {
        snprintf(path, sizeof(path), "%s/neowalld.sock", runtime_dir);
    } else {
        const char *home = getenv("HOME");
        if (home) {
            snprintf(path, sizeof(path), "%s/.neowalld.sock", home);
        } else {
            snprintf(path, sizeof(path), "/tmp/neowalld-%d.sock", getuid());
        }
    }

    return path;
}

ipc_server_t *ipc_server_create(const char *socket_path, ipc_command_handler_t handler, void *user_data) {
    if (!handler) {
        fprintf(stderr, "IPC: Handler is required\n");
        return NULL;
    }

    if (!socket_path) {
        socket_path = ipc_get_socket_path();
    }

    ipc_server_t *server = calloc(1, sizeof(ipc_server_t));
    if (!server) {
        perror("IPC: Failed to allocate server");
        return NULL;
    }

    server->handler = handler;
    server->user_data = user_data;
    server->listen_fd = -1;
    strncpy(server->socket_path, socket_path, sizeof(server->socket_path) - 1);

    /* Create Unix domain socket */
    server->listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server->listen_fd < 0) {
        perror("IPC: Failed to create socket");
        free(server);
        return NULL;
    }

    /* Set non-blocking */
    int flags = fcntl(server->listen_fd, F_GETFL, 0);
    fcntl(server->listen_fd, F_SETFL, flags | O_NONBLOCK);

    /* Remove old socket file if exists */
    unlink(socket_path);

    /* Bind socket */
    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

    if (bind(server->listen_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("IPC: Failed to bind socket");
        close(server->listen_fd);
        free(server);
        return NULL;
    }

    /* Listen for connections */
    if (listen(server->listen_fd, 5) < 0) {
        perror("IPC: Failed to listen");
        close(server->listen_fd);
        unlink(socket_path);
        free(server);
        return NULL;
    }

    /* Set permissions */
    chmod(socket_path, 0600);

    fprintf(stderr, "IPC: Server listening on %s\n", socket_path);

    return server;
}

void ipc_server_destroy(ipc_server_t *server) {
    if (!server) return;

    /* Close all client connections */
    for (int i = 0; i < server->num_clients; i++) {
        if (server->client_fds[i] >= 0) {
            close(server->client_fds[i]);
        }
    }

    if (server->listen_fd >= 0) {
        close(server->listen_fd);
    }

    unlink(server->socket_path);
    fprintf(stderr, "IPC: Server stopped\n");

    free(server);
}

int ipc_server_get_fd(ipc_server_t *server) {
    return server ? server->listen_fd : -1;
}

static void handle_client_request(ipc_server_t *server, int client_fd) {
    char buffer[IPC_MAX_MESSAGE_SIZE];

    /* Read request */
    ssize_t n = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
    if (n <= 0) {
        if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            perror("IPC: recv error");
        }
        return;
    }

    buffer[n] = '\0';

    /* Parse request */
    ipc_request_t req;
    if (!ipc_parse_request(buffer, &req)) {
        /* Send error response */
        ipc_response_t resp;
        ipc_error_response(&resp, IPC_STATUS_INVALID_JSON, "Failed to parse request");

        char response[IPC_MAX_MESSAGE_SIZE];
        int len = ipc_build_response(&resp, response, sizeof(response));
        if (len > 0) {
            send(client_fd, response, len, 0);
        }
        return;
    }

    /* Handle command */
    ipc_response_t resp;
    server->handler(&req, &resp, server->user_data);

    /* Send response */
    char response[IPC_MAX_MESSAGE_SIZE];
    int len = ipc_build_response(&resp, response, sizeof(response));
    if (len > 0) {
        send(client_fd, response, len, 0);
    }
}

int ipc_server_process(ipc_server_t *server) {
    if (!server || server->listen_fd < 0) return -1;

    int processed = 0;

    /* Accept new connections */
    while (1) {
        int client_fd = accept(server->listen_fd, NULL, NULL);
        if (client_fd < 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                perror("IPC: accept error");
            }
            break;
        }

        /* Set non-blocking */
        int flags = fcntl(client_fd, F_GETFL, 0);
        fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);

        /* Handle request immediately and close */
        handle_client_request(server, client_fd);
        close(client_fd);

        processed++;
    }

    return processed;
}
