/* neowall/app/daemon.h — process setup for the daemon (src/app/daemon.c). */
#ifndef NEOWALL_APP_DAEMON_H
#define NEOWALL_APP_DAEMON_H

#include <stdbool.h>
#include <sys/types.h>

struct neowall_state;


/* Fork twice, setsid, chdir /, stdio to /dev/null, publish the final pid. */
bool daemonize(void);

/* Block the signals the runtime reads from its signalfd, before any thread
 * starts. Then install the crash handlers and ignore SIGPIPE. */
bool block_signals(void);
void setup_crash_handlers(struct neowall_state *state);

/* GPU-loss re-exec budget: false after 3 restarts within a minute. */
bool gpu_restart_allowed(void);

bool create_config_directory(void);

#endif /* NEOWALL_APP_DAEMON_H */
