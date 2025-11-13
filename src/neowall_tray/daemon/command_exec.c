/* NeoWall Tray - Command Execution Component
 * Implementation of command execution functions with extensive logging
 */

#include "command_exec.h"
#include "../common/log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>
#include <sys/stat.h>
#include <libgen.h>

#define NEOWALL_BINARY "neowall"
#define MAX_PATH 1024
#define COMPONENT "CMD-EXEC"

/* Find the neowall binary in common paths */
static char *find_neowall_binary(void) {
    static char binary_path[MAX_PATH] = {0};

    /* If we already found it, return cached path */
    if (binary_path[0] != '\0') {
        return binary_path;
    }

    /* List of paths to search */
    const char *search_paths[] = {
        /* Build directory paths */
        "./builddir/bin/neowall",
        "../builddir/bin/neowall",
        "../../builddir/bin/neowall",
        "./bin/neowall",
        "../bin/neowall",

        /* Installed paths */
        "/usr/local/bin/neowall",
        "/usr/bin/neowall",

        /* Home directory */
        NULL  /* Will be set to ~/.local/bin/neowall */
    };

    /* Check home directory path */
    const char *home = getenv("HOME");
    char home_bin[MAX_PATH];
    if (home) {
        snprintf(home_bin, sizeof(home_bin), "%s/.local/bin/neowall", home);
    }

    TRAY_LOG_DEBUG(COMPONENT, "Searching for neowall binary...");

    /* Try each search path */
    for (int i = 0; search_paths[i] != NULL || (i == 8 && home); i++) {
        const char *path = (i == 8 && home) ? home_bin : search_paths[i];
        if (!path) continue;

        struct stat st;
        if (stat(path, &st) == 0 && S_ISREG(st.st_mode) && (st.st_mode & S_IXUSR)) {
            /* Found executable file */
            if (realpath(path, binary_path) != NULL) {
                TRAY_LOG_INFO(COMPONENT, "Found neowall binary at: %s", binary_path);
                return binary_path;
            }
        }
    }

    /* Try to find in PATH using 'which' */
    FILE *fp = popen("which neowall 2>/dev/null", "r");
    if (fp) {
        if (fgets(binary_path, sizeof(binary_path), fp) != NULL) {
            /* Remove newline */
            size_t len = strlen(binary_path);
            if (len > 0 && binary_path[len - 1] == '\n') {
                binary_path[len - 1] = '\0';
            }

            pclose(fp);

            if (binary_path[0] != '\0') {
                TRAY_LOG_INFO(COMPONENT, "Found neowall binary in PATH: %s", binary_path);
                return binary_path;
            }
        }
        pclose(fp);
    }

    /* Not found - clear cache and use simple name (let execvp search PATH) */
    TRAY_LOG_DEBUG(COMPONENT, "neowall binary not found in common paths, will try PATH");
    strncpy(binary_path, NEOWALL_BINARY, sizeof(binary_path) - 1);
    return binary_path;
}

/* Execute a neowall command asynchronously */
bool command_execute(const char *cmd) {
    if (!cmd) {
        TRAY_LOG_ERROR(COMPONENT, "command_execute called with NULL command");
        return false;
    }

    TRAY_LOG_INFO(COMPONENT, "Executing command: %s", cmd);

    char *binary = find_neowall_binary();
    TRAY_LOG_DEBUG(COMPONENT, "Using binary path: %s", binary);

    pid_t pid = fork();

    if (pid == -1) {
        TRAY_LOG_ERROR(COMPONENT, "Failed to fork for command '%s': %s", cmd, strerror(errno));
        return false;
    }

    if (pid == 0) {
        /* Child process */
        TRAY_LOG_DEBUG(COMPONENT, "Child process executing: %s %s", binary, cmd);

        char *args[] = {basename(binary), (char *)cmd, NULL};
        execv(binary, args);

        /* If execv returns, an error occurred */
        TRAY_LOG_ERROR(COMPONENT, "Failed to execute %s %s: %s", binary, cmd, strerror(errno));

        /* Try execvp as fallback */
        char *args_fallback[] = {NEOWALL_BINARY, (char *)cmd, NULL};
        execvp(NEOWALL_BINARY, args_fallback);

        TRAY_LOG_ERROR(COMPONENT, "Failed to execute (fallback) %s %s: %s",
                       NEOWALL_BINARY, cmd, strerror(errno));
        exit(1);
    }

    /* Parent process - don't wait, let it run async */
    TRAY_LOG_DEBUG(COMPONENT, "Spawned child process PID %d for command: %s", pid, cmd);
    return true;
}

/* Execute a neowall command and capture its output */
bool command_execute_with_output(const char *cmd, char *output, size_t output_size) {
    if (!cmd || !output || output_size == 0) {
        TRAY_LOG_ERROR(COMPONENT, "command_execute_with_output called with invalid parameters");
        return false;
    }

    TRAY_LOG_INFO(COMPONENT, "Executing command with output capture: %s", cmd);

    char *binary = find_neowall_binary();

    /* Build command string */
    char command[2048];
    snprintf(command, sizeof(command), "%s %s 2>&1", binary, cmd);

    TRAY_LOG_DEBUG(COMPONENT, "Full command: %s", command);

    /* Execute and capture output */
    FILE *fp = popen(command, "r");
    if (fp == NULL) {
        TRAY_LOG_ERROR(COMPONENT, "popen failed for command '%s': %s", command, strerror(errno));
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
        } else {
            TRAY_LOG_DEBUG(COMPONENT, "Output buffer full, truncating");
            break;
        }
    }
    output[offset] = '\0';

    /* Clean up and check status */
    int status = pclose(fp);

    if (WIFEXITED(status)) {
        int exit_code = WEXITSTATUS(status);
        if (exit_code == 0) {
            TRAY_LOG_DEBUG(COMPONENT, "Command succeeded with output (%zu bytes)", offset);
            return true;
        } else {
            TRAY_LOG_ERROR(COMPONENT, "Command failed with exit code %d", exit_code);
            TRAY_LOG_DEBUG(COMPONENT, "Output: %s", output);
            return false;
        }
    } else if (WIFSIGNALED(status)) {
        TRAY_LOG_ERROR(COMPONENT, "Command terminated by signal %d", WTERMSIG(status));
        return false;
    }

    TRAY_LOG_ERROR(COMPONENT, "Command failed with unknown status: %d", status);
    return false;
}

/* Execute a neowall command with arguments */
bool command_execute_argv(int argc, char **argv) {
    if (argc <= 0 || !argv) {
        TRAY_LOG_ERROR(COMPONENT, "command_execute_argv called with invalid parameters");
        return false;
    }

    TRAY_LOG_INFO(COMPONENT, "Executing command with %d arguments", argc);
    for (int i = 0; i < argc; i++) {
        TRAY_LOG_DEBUG(COMPONENT, "  arg[%d] = %s", i, argv[i]);
    }

    char *binary = find_neowall_binary();
    TRAY_LOG_DEBUG(COMPONENT, "Using binary path: %s", binary);

    pid_t pid = fork();

    if (pid == -1) {
        TRAY_LOG_ERROR(COMPONENT, "Failed to fork for command: %s", strerror(errno));
        return false;
    }

    if (pid == 0) {
        /* Child process */
        /* Build args array with neowall binary as first argument */
        char **args = malloc((argc + 2) * sizeof(char *));
        if (!args) {
            TRAY_LOG_ERROR(COMPONENT, "Failed to allocate memory for arguments");
            exit(1);
        }

        args[0] = basename(binary);
        for (int i = 0; i < argc; i++) {
            args[i + 1] = argv[i];
        }
        args[argc + 1] = NULL;

        TRAY_LOG_DEBUG(COMPONENT, "Child process executing: %s with %d args", binary, argc);
        execv(binary, args);

        /* If execv returns, an error occurred */
        TRAY_LOG_ERROR(COMPONENT, "Failed to execute %s: %s", binary, strerror(errno));

        /* Try execvp as fallback */
        args[0] = NEOWALL_BINARY;
        execvp(NEOWALL_BINARY, args);

        TRAY_LOG_ERROR(COMPONENT, "Failed to execute (fallback) %s: %s",
                       NEOWALL_BINARY, strerror(errno));
        free(args);
        exit(1);
    }

    /* Parent process - don't wait, let it run async */
    TRAY_LOG_DEBUG(COMPONENT, "Spawned child process PID %d", pid);
    return true;
}
