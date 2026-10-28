#define _GNU_SOURCE
#include "neowall/sd_notify.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

bool nw_under_systemd(void) {
    const char *id = getenv("INVOCATION_ID");
    return id && id[0];
}

bool nw_sd_notify(const char *state) {
    const char *path = getenv("NOTIFY_SOCKET");
    if (!state || !path || !path[0]) return false;

    struct sockaddr_un addr = {.sun_family = AF_UNIX};
    size_t len = strlen(path);
    if (len >= sizeof(addr.sun_path)) return false;
    /* only absolute paths and abstract sockets ('@' -> leading NUL) */
    if (path[0] != '/' && path[0] != '@') return false;
    memcpy(addr.sun_path, path, len);
    if (path[0] == '@') addr.sun_path[0] = '\0';
    socklen_t alen = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + len);

    int fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return false;
    ssize_t n = sendto(fd, state, strlen(state), MSG_NOSIGNAL,
                       (struct sockaddr *)&addr, alen);
    close(fd);
    return n == (ssize_t)strlen(state);
}
