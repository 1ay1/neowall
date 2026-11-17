/* neowall/cli/cli.h — the command line: everything `neowall <verb>` does that
 * isn't running the daemon itself (src/cli/). */
#ifndef NEOWALL_CLI_H
#define NEOWALL_CLI_H

#include <stdbool.h>
#include <stddef.h>

/* usage.c */
void print_usage(const char *program_name);
void print_version(void);

/* control.c: drive a running daemon. Returns an exit code, or -1 if argv[1]
 * isn't a control command. */
int cmd_control(int argc, char *argv[]);
/* The control commands for --help: name and description, NULL past the end. */
bool cli_control_command(size_t i, const char **name, const char **description);

/* tools.c: one-shot tools. Return an exit code. */
int cmd_new(int argc, char *argv[]);
int cmd_import(int argc, char *argv[]);
int cmd_docs(int argc, char *argv[]);
/* render.c: draw a shader offscreen to a PNG (+ --bench, --check). */
int cmd_render(int argc, char *argv[]);


#endif /* NEOWALL_CLI_H */
