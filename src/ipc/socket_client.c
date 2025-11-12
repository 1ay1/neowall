/*
 * NeoWall IPC Socket Client (Client Side)
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

struct ipc_client {
    int fd;
    char socket_path[256];
};

ipc_client_t *ipc_client_connect(const char *socket_path) {
    if (!socket_path) {
        socket_path = ipc_get_socket_path();
    }

    ipc_client_t *client = calloc(1, sizeof(ipc_client_t));
    if (!client) {
        perror("IPC: Failed to allocate client");
        return NULL;
    }

    strncpy(client->socket_path, socket_path, sizeof(client->socket_path) - 1);
    client->fd = -1;

    /* Create socket */
    client->fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (client->fd < 0) {
        perror("IPC: Failed to create client socket");
        free(client);
        return NULL;
    }

    /* Connect to server */
    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);

    if (connect(client->fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        if (errno == ENOENT) {
            fprintf(stderr, "Error: neowalld is not running\n");
            fprintf(stderr, "Start it with: neowall start\n");
        } else {
            fprintf(stderr, "Error: Failed to connect to daemon: %s\n", strerror(errno));
        }
        close(client->fd);
        free(client);
        return NULL;
    }

    return client;
}

bool ipc_client_send(ipc_client_t *client, const ipc_request_t *req, ipc_response_t *resp) {
    if (!client || client->fd < 0 || !req || !resp) {
        return false;
    }

    /* Build request JSON */
    char request[IPC_MAX_MESSAGE_SIZE];
    int len = ipc_build_request(req, request, sizeof(request));
    if (len < 0) {
        fprintf(stderr, "Error: Failed to build request\n");
        return false;
    }

    /* Send request */
    ssize_t sent = send(client->fd, request, len, 0);
    if (sent < 0) {
        perror("IPC: Failed to send request");
        return false;
    }

    /* Receive response */
    char response[IPC_MAX_MESSAGE_SIZE];
    ssize_t received = recv(client->fd, response, sizeof(response) - 1, 0);
    if (received < 0) {
        perror("IPC: Failed to receive response");
        return false;
    }

    if (received == 0) {
        fprintf(stderr, "Error: Connection closed by daemon\n");
        return false;
    }

    response[received] = '\0';

    /* Parse response */
    if (!ipc_parse_response(response, resp)) {
        fprintf(stderr, "Error: Failed to parse response: %s\n", response);
        return false;
    }

    return true;
}

void ipc_client_close(ipc_client_t *client) {
    if (!client) return;

    if (client->fd >= 0) {
        close(client->fd);
    }

    free(client);
}
