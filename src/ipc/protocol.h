/*
 * NeoWall IPC Protocol
 * Simple JSON-based request/response protocol over Unix sockets
 */

#ifndef NEOWALL_IPC_PROTOCOL_H
#define NEOWALL_IPC_PROTOCOL_H

#include <stddef.h>
#include <stdbool.h>

/* Maximum message size */
#define IPC_MAX_MESSAGE_SIZE 8192

/* Response status codes */
typedef enum {
    IPC_STATUS_OK = 0,
    IPC_STATUS_ERROR = 1,
    IPC_STATUS_INVALID_COMMAND = 2,
    IPC_STATUS_INVALID_JSON = 3,
    IPC_STATUS_DAEMON_ERROR = 4
} ipc_status_t;

/* Request structure */
typedef struct {
    char command[256];
    char args[4096];  /* JSON string of arguments */
} ipc_request_t;

/* Response structure */
typedef struct {
    ipc_status_t status;
    char message[512];
    char data[4096];  /* JSON string of response data */
} ipc_response_t;

/* Protocol functions */

/**
 * Parse request from JSON string
 * Returns true on success, false on parse error
 */
bool ipc_parse_request(const char *json, ipc_request_t *req);

/**
 * Build response JSON string
 * Returns number of bytes written, or -1 on error
 */
int ipc_build_response(const ipc_response_t *resp, char *out, size_t out_size);

/**
 * Parse response from JSON string
 * Returns true on success, false on parse error
 */
bool ipc_parse_response(const char *json, ipc_response_t *resp);

/**
 * Build request JSON string
 * Returns number of bytes written, or -1 on error
 */
int ipc_build_request(const ipc_request_t *req, char *out, size_t out_size);

/**
 * Create error response
 */
void ipc_error_response(ipc_response_t *resp, ipc_status_t status, const char *message);

/**
 * Create success response
 */
void ipc_success_response(ipc_response_t *resp, const char *data);

#endif /* NEOWALL_IPC_PROTOCOL_H */
