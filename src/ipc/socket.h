/*
 * NeoWall IPC Socket Interface
 */

#ifndef NEOWALL_IPC_SOCKET_H
#define NEOWALL_IPC_SOCKET_H

#include <stdbool.h>
#include <stddef.h>
#include "protocol.h"

/* Socket server (daemon side) */
typedef struct ipc_server ipc_server_t;

/**
 * Command handler callback
 * Called when daemon receives a command
 */
typedef void (*ipc_command_handler_t)(const ipc_request_t *req, ipc_response_t *resp, void *user_data);

/**
 * Create and start IPC server
 * Returns server handle or NULL on error
 */
ipc_server_t *ipc_server_create(const char *socket_path, ipc_command_handler_t handler, void *user_data);

/**
 * Stop and destroy IPC server
 */
void ipc_server_destroy(ipc_server_t *server);

/**
 * Get server socket FD (for event loop integration)
 */
int ipc_server_get_fd(ipc_server_t *server);

/**
 * Process pending connections/requests
 * Call this when socket FD is readable
 * Returns number of requests processed, or -1 on error
 */
int ipc_server_process(ipc_server_t *server);

/* Socket client (client side) */
typedef struct ipc_client ipc_client_t;

/**
 * Connect to IPC server
 * Returns client handle or NULL on error
 */
ipc_client_t *ipc_client_connect(const char *socket_path);

/**
 * Send request and receive response
 * Returns true on success, false on error
 */
bool ipc_client_send(ipc_client_t *client, const ipc_request_t *req, ipc_response_t *resp);

/**
 * Close connection and free client
 */
void ipc_client_close(ipc_client_t *client);

/**
 * Get default socket path
 */
const char *ipc_get_socket_path(void);

#endif /* NEOWALL_IPC_SOCKET_H */
