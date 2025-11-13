/* NeoWall Tray - Command Execution Component
 * Implementation of command execution functions
 */

#include "command_exec.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>

#define NEOWALL_BINARY "neowall"

/* Execute a neowall command asynchronously */
bool command_execute(const char *cmd) {
    if (!cmd) {
        return false;
    }

    pid_t pid = fork();

    if (pid == -1) {
        fprintf(stderr, "Failed to fork: %s\n", strerror(errno));
        return false;
    }

    if (pid == 0) {
        /* Child process */
        char *args[] = {NEOWALL_BINARY, (char *)cmd, NULL};
        execvp(NEOWALL_BINARY, args);

        /* If execvp returns, an error occurred */
        fprintf(stderr, "Failed to execute %s %s: %s\n",
                NEOWALL_BINARY, cmd, strerror(errno));
        exit(1);
    }

    /* Parent process - don't wait, let it run async */
    return true;
}

/* Execute a neowall command and capture its output */
bool command_execute_with_output(const char *cmd, char *output, size_t output_size) {
    if (!cmd || !output || output_size == 0) {
        return false;
    }

    /* Build command string */
    char command[256];
    snprintf(command, sizeof(command), "%s %s 2>&1", NEOWALL_BINARY, cmd);

    /* Execute and capture output */
    FILE *fp = popen(command, "r");
    if (fp == NULL) {
        return false;
    }

    /* Read output */
    size_t offset = 0;
    char buffer[256];

    while (fgets(buffer, sizeof(buffer), fp) != NULL && offset < output_size - 1) {
        size_t len = strlen(buffer);
        if (offset + len < output_size) {
            memcpy(output + offset, buffer, len);
            offset += len;
        }
    }
    output[offset] = '\0';

    /* Clean up and check status */
    int status = pclose(fp);
    return (WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

/* Execute a neowall command with arguments */
bool command_execute_argv(int argc, char **argv) {
    if (argc <= 0 || !argv) {
        return false;
    }

    pid_t pid = fork();

    if (pid == -1) {
        fprintf(stderr, "Failed to fork: %s\n", strerror(errno));
        return false;
    }

    if (pid == 0) {
        /* Child process */
        /* Build args array with neowall binary as first argument */
        char **args = malloc((argc + 2) * sizeof(char *));
        if (!args) {
            exit(1);
        }

        args[0] = NEOWALL_BINARY;
        for (int i = 0; i < argc; i++) {
            args[i + 1] = argv[i];
        }
        args[argc + 1] = NULL;

        execvp(NEOWALL_BINARY, args);

        /* If execvp returns, an error occurred */
        fprintf(stderr, "Failed to execute %s: %s\n", NEOWALL_BINARY, strerror(errno));
        free(args);
        exit(1);
    }

    /* Parent process - don't wait, let it run async */
    return true;
}
