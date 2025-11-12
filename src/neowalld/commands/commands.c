/*
 * NeoWall Command System - Compatibility Wrapper
 * Routes old IPC handler to new command API
 */

#include "commands.h"
#include "ipc/socket.h"
#include <stdio.h>

/**
 * Legacy IPC handler (backwards compatibility)
 * This is called by the IPC server and routes to the new command system
 */
void ipc_handle_command(const ipc_request_t *req, ipc_response_t *resp, void *user_data) {
    /* Simply dispatch to the new command API */
    commands_dispatch(req, resp, user_data);
}
