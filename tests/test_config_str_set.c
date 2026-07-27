/* Regression test for the alias-safe config field setter.
 *
 * BUG THIS GUARDS (found live, v0.5.4): the deferred re-apply paths in
 * src/output/output.c call the wallpaper setters with the config's OWN buffer
 * as the argument:
 *
 *     output_set_terminal(output, output->config->term_cmd, ...);
 *     output_set_shader(output, output->config->shader_path);
 *     output_set_wallpaper(output, output->config->path);
 *
 * ...and each setter then writes that argument back into the same field with
 * snprintf(dst, n, "%s", src). When dst == src the source and destination
 * overlap, which is undefined behaviour; glibc in practice yields an EMPTY
 * string. The observable symptom was a terminal wallpaper coming up with no
 * command at all after a deferred config apply:
 *
 *     INFO: Terminal wallpaper running: '' (213x60 cells)
 *
 * config_str_set() fixes it by short-circuiting the self-assignment. This test
 * mirrors that function exactly (output.c can't be linked headless — it pulls
 * in the whole EGL/GL/image stack) and pins the three properties that matter.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ---- mirror of the production helper (src/output/output.c) ---- */
static void config_str_set(char *dst, size_t dst_size, const char *src) {
    if (!dst || dst_size == 0) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }
    if (dst == src) {
        return;
    }
    snprintf(dst, dst_size, "%s", src);
}

static int failures;

static void check(int cond, const char *what) {
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", what);
        failures++;
    } else {
        printf("ok: %s\n", what);
    }
}

int main(void) {
    /* 1. THE BUG: self-assignment must preserve the value, not blank it.
     *    This is the exact shape of the deferred terminal re-apply. */
    {
        char term_cmd[256];
        snprintf(term_cmd, sizeof(term_cmd), "sh -c 'while :; do date; sleep 1; done'");

        const char *cmd = term_cmd;            /* caller passes the config field */
        config_str_set(term_cmd, sizeof(term_cmd), cmd);

        check(strcmp(term_cmd, "sh -c 'while :; do date; sleep 1; done'") == 0,
              "self-assignment preserves term_cmd");
        check(term_cmd[0] != '\0', "self-assignment does not blank the field");
    }

    /* 2. Normal copy from distinct storage still works. */
    {
        char dst[64];
        memset(dst, 'X', sizeof(dst));
        config_str_set(dst, sizeof(dst), "/path/to/shader.glsl");
        check(strcmp(dst, "/path/to/shader.glsl") == 0, "distinct-buffer copy");
    }

    /* 3. Overlong source truncates and stays NUL-terminated (no overflow).
     *    The source goes through a volatile pointer so GCC can't constant-fold
     *    the length at the inlined call site and trip -Wformat-truncation on a
     *    case that is deliberately exercising truncation. */
    {
        char dst[8];
        static const char long_src[] = "0123456789abcdef";
        const char *volatile src = long_src;
        config_str_set(dst, sizeof(dst), src);
        check(strlen(dst) == 7, "overlong source truncates to dst_size-1");
        check(dst[7] == '\0', "truncated result is NUL-terminated");
    }

    /* 4. NULL source clears the field rather than dereferencing. */
    {
        char dst[16];
        snprintf(dst, sizeof(dst), "stale");
        config_str_set(dst, sizeof(dst), NULL);
        check(dst[0] == '\0', "NULL source clears the field");
    }

    /* 5. Degenerate arguments are no-ops, not crashes. */
    {
        char dst[4] = "abc";
        config_str_set(NULL, 16, "x");
        config_str_set(dst, 0, "x");
        check(strcmp(dst, "abc") == 0, "zero dst_size is a no-op");
    }

    /* 6. Self-assignment of an ALREADY-EMPTY field stays empty (the caller's
     *    "is it set?" guard must keep working after the copy). */
    {
        char dst[32] = "";
        config_str_set(dst, sizeof(dst), dst);
        check(dst[0] == '\0', "empty self-assignment stays empty");
    }

    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    printf("all config_str_set checks passed\n");
    return 0;
}
