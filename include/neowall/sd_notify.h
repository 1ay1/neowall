#ifndef NEOWALL_SD_NOTIFY_H
#define NEOWALL_SD_NOTIFY_H

#include <stdbool.h>

/* Minimal sd_notify(3): sends `state` (e.g. "READY=1") to $NOTIFY_SOCKET.
 * No libsystemd needed. A no-op returning false when not run by systemd. */
bool nw_sd_notify(const char *state);

/* True when systemd started us as a service ($INVOCATION_ID is set), so we
 * must stay in the foreground: forking would look like an exit. */
bool nw_under_systemd(void);

#endif
