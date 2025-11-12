/*
 * NeoWall IPC Protocol Implementation
 * Simple JSON parser/builder for IPC messages
 */

#include "protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* Simple JSON string extractor */
static bool extract_json_string(const char *json, const char *key, char *out, size_t out_size) {
    char search[128];
    snprintf(search, sizeof(search), "\"%s\"", key);

    const char *key_pos = strstr(json, search);
    if (!key_pos) return false;

    /* Find the colon */
    const char *colon = strchr(key_pos, ':');
    if (!colon) return false;

    /* Skip whitespace after colon */
    colon++;
    while (*colon && isspace(*colon)) colon++;

    /* Check if it's a string (starts with quote) */
    if (*colon == '"') {
        colon++; /* Skip opening quote */
        const char *end = strchr(colon, '"');
        if (!end) return false;

        size_t len = end - colon;
        if (len >= out_size) len = out_size - 1;

        memcpy(out, colon, len);
        out[len] = '\0';
        return true;
    }

    /* Not a string, copy until comma or brace */
    const char *end = colon;
    while (*end && *end != ',' && *end != '}' && *end != ']') end++;

    size_t len = end - colon;
    /* Trim trailing whitespace */
    while (len > 0 && isspace(colon[len-1])) len--;

    if (len >= out_size) len = out_size - 1;
    memcpy(out, colon, len);
    out[len] = '\0';

    return true;
}

/* Extract JSON object */
static bool extract_json_object(const char *json, const char *key, char *out, size_t out_size) {
    char search[128];
    snprintf(search, sizeof(search), "\"%s\"", key);

    const char *key_pos = strstr(json, search);
    if (!key_pos) {
        out[0] = '\0';
        return true; /* Empty object is ok */
    }

    const char *colon = strchr(key_pos, ':');
    if (!colon) return false;

    colon++;
    while (*colon && isspace(*colon)) colon++;

    if (*colon != '{') {
        out[0] = '\0';
        return true;
    }

    /* Find matching closing brace */
    int depth = 0;
    const char *start = colon;
    const char *p = colon;

    do {
        if (*p == '{') depth++;
        else if (*p == '}') depth--;
        p++;
    } while (*p && depth > 0);

    if (depth != 0) return false;

    size_t len = p - start;
    if (len >= out_size) len = out_size - 1;

    memcpy(out, start, len);
    out[len] = '\0';

    return true;
}

bool ipc_parse_request(const char *json, ipc_request_t *req) {
    if (!json || !req) return false;

    memset(req, 0, sizeof(*req));

    if (!extract_json_string(json, "command", req->command, sizeof(req->command))) {
        return false;
    }

    /* Args are optional */
    extract_json_object(json, "args", req->args, sizeof(req->args));

    return true;
}

bool ipc_parse_response(const char *json, ipc_response_t *resp) {
    if (!json || !resp) return false;

    memset(resp, 0, sizeof(*resp));

    char status_str[32];
    if (!extract_json_string(json, "status", status_str, sizeof(status_str))) {
        return false;
    }

    if (strcmp(status_str, "ok") == 0) {
        resp->status = IPC_STATUS_OK;
    } else if (strcmp(status_str, "error") == 0) {
        resp->status = IPC_STATUS_ERROR;
    } else {
        resp->status = IPC_STATUS_ERROR;
    }

    extract_json_string(json, "message", resp->message, sizeof(resp->message));
    extract_json_object(json, "data", resp->data, sizeof(resp->data));

    return true;
}

int ipc_build_request(const ipc_request_t *req, char *out, size_t out_size) {
    if (!req || !out || out_size == 0) return -1;

    int len;
    if (req->args[0] != '\0') {
        len = snprintf(out, out_size,
            "{\"command\":\"%s\",\"args\":%s}",
            req->command, req->args);
    } else {
        len = snprintf(out, out_size,
            "{\"command\":\"%s\",\"args\":{}}",
            req->command);
    }

    return (len < (int)out_size) ? len : -1;
}

int ipc_build_response(const ipc_response_t *resp, char *out, size_t out_size) {
    if (!resp || !out || out_size == 0) return -1;

    const char *status_str = (resp->status == IPC_STATUS_OK) ? "ok" : "error";

    int len;
    if (resp->message[0] != '\0' && resp->data[0] != '\0') {
        len = snprintf(out, out_size,
            "{\"status\":\"%s\",\"message\":\"%s\",\"data\":%s}",
            status_str, resp->message, resp->data);
    } else if (resp->message[0] != '\0') {
        len = snprintf(out, out_size,
            "{\"status\":\"%s\",\"message\":\"%s\"}",
            status_str, resp->message);
    } else if (resp->data[0] != '\0') {
        len = snprintf(out, out_size,
            "{\"status\":\"%s\",\"data\":%s}",
            status_str, resp->data);
    } else {
        len = snprintf(out, out_size,
            "{\"status\":\"%s\"}",
            status_str);
    }

    return (len < (int)out_size) ? len : -1;
}

void ipc_error_response(ipc_response_t *resp, ipc_status_t status, const char *message) {
    if (!resp) return;

    memset(resp, 0, sizeof(*resp));
    resp->status = status;

    if (message) {
        strncpy(resp->message, message, sizeof(resp->message) - 1);
    }
}

void ipc_success_response(ipc_response_t *resp, const char *data) {
    if (!resp) return;

    memset(resp, 0, sizeof(*resp));
    resp->status = IPC_STATUS_OK;

    if (data) {
        strncpy(resp->data, data, sizeof(resp->data) - 1);
    } else {
        strcpy(resp->data, "{}");
    }
}
