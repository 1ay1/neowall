/* neowall/proto/proto.h — what the CLI and the daemon agree on: where the
 * control socket and the pid file are, and the command files an older CLI
 * writes before signalling. Both sides include this; neither includes the
 * other. */
#ifndef NEOWALL_PROTO_H
#define NEOWALL_PROTO_H

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

/* $XDG_RUNTIME_DIR/neowall.sock, or the private fallback dir. Static
 * buffer; empty when no private dir is available. */
const char *proto_socket_path(void);

/* The pid file, held with flock for the daemon's whole life, so "is one
 * running" doesn't depend on pid reuse. */
const char *get_pid_file_path(void);
/* Claim it. HELD (with *existing_pid) if a live daemon has it, FAILED if it
 * can't be created (logged with the reason). */
typedef enum { PID_CLAIM_OK, PID_CLAIM_HELD, PID_CLAIM_FAILED } pid_claim;
pid_claim try_take_pid_file(pid_t *existing_pid);
bool update_pid_file(void);
bool remove_stale_pid_file(void);
void remove_pid_file(void);

/* An older daemon reads `set` / `set-terminal` arguments from a file after
 * a signal. The CLI writes them; the daemon reads. */
const char *get_set_index_file_path(void);
const char *get_set_terminal_file_path(void);
bool write_set_index_file(int index);
bool write_set_terminal_file(const char *cmd);
int read_set_index_file(void);
bool read_set_terminal_file(char *out, size_t out_len);

#endif /* NEOWALL_PROTO_H */
