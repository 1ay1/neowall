/* Where the control socket lives. */

#include <stdio.h>
#include <stdlib.h>
#include <sys/un.h>

#include "neowall/core/util.h"

const char *proto_socket_path(void) {
    static char path[sizeof(((struct sockaddr_un *)0)->sun_path)];
    /* $XDG_RUNTIME_DIR is per-user and 0700 already; else make our own. */
    const char *d = getenv("XDG_RUNTIME_DIR");
    if (!d || !d[0]) d = neowall_secure_runtime_dir();
    int n = d ? snprintf(path, sizeof(path), "%s/neowall.sock", d) : -1;
    if (n < 0 || (size_t)n >= sizeof(path)) path[0] = '\0';
    return path;
}
