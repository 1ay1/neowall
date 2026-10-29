/* nw_sd_notify talks the systemd notify protocol without libsystemd. */
#define _GNU_SOURCE
#include "neowall/sd_notify.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

static int fails = 0;
#define CHECK(c, what) do { if (!(c)) { printf("  FAIL: %s\n", what); fails++; } } while (0)

static int bind_dgram(struct sockaddr_un *addr, socklen_t len) {
    int fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (fd < 0 || bind(fd, (struct sockaddr *)addr, len) != 0) return -1;
    return fd;
}

static void expect_recv(int fd, const char *want, const char *what) {
    char buf[128] = {0};
    ssize_t n = recv(fd, buf, sizeof(buf) - 1, MSG_DONTWAIT);
    CHECK(n == (ssize_t)strlen(want) && strcmp(buf, want) == 0, what);
}

int main(void) {
    unsetenv("NOTIFY_SOCKET");
    CHECK(!nw_sd_notify("READY=1"), "no socket: returns false");
    setenv("NOTIFY_SOCKET", "relative/path", 1);
    CHECK(!nw_sd_notify("READY=1"), "relative path refused");

    /* path socket */
    char dir[] = "/tmp/nw-sdn-XXXXXX";
    CHECK(mkdtemp(dir) != NULL, "mkdtemp");
    struct sockaddr_un a = {.sun_family = AF_UNIX};
    snprintf(a.sun_path, sizeof(a.sun_path), "%s/notify", dir);
    int fd = bind_dgram(&a, sizeof(a));
    CHECK(fd >= 0, "bind path socket");
    setenv("NOTIFY_SOCKET", a.sun_path, 1);
    CHECK(nw_sd_notify("READY=1"), "send to path socket");
    expect_recv(fd, "READY=1", "path socket gets READY=1");
    close(fd);
    unlink(a.sun_path);
    rmdir(dir);

    /* abstract socket */
    char name[64];
    snprintf(name, sizeof(name), "@nw-sdn-test-%d", (int)getpid());
    struct sockaddr_un b = {.sun_family = AF_UNIX};
    memcpy(b.sun_path + 1, name + 1, strlen(name) - 1);
    fd = bind_dgram(&b, (socklen_t)(offsetof(struct sockaddr_un, sun_path) + strlen(name)));
    CHECK(fd >= 0, "bind abstract socket");
    setenv("NOTIFY_SOCKET", name, 1);
    CHECK(nw_sd_notify("STOPPING=1"), "send to abstract socket");
    expect_recv(fd, "STOPPING=1", "abstract socket gets STOPPING=1");
    close(fd);

    unsetenv("INVOCATION_ID");
    CHECK(!nw_under_systemd(), "not under systemd without INVOCATION_ID");
    setenv("INVOCATION_ID", "abc", 1);
    CHECK(nw_under_systemd(), "under systemd with INVOCATION_ID");

    printf("sd_notify: %d failures\n", fails);
    return fails ? 1 : 0;
}
