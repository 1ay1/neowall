/* One-shot tools that never touch the daemon, the config or the pid file:
 * `new`, `import`, `docs`. */

#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/file.h>
#include <sys/stat.h>

#include "neowall/cli/cli.h"
#include "neowall/neowall.h"
#include "neowall/shader/shader_docs.h"
#include "neowall/shader/shader_scaffold.h"
#include "neowall/shader/shadertoy_import.h"


/* `neowall new <name> [--template=scene|flat|reactive] [--force] [--list]` */
int cmd_new(int argc, char *argv[]) {
    const char *shader_name = NULL;
    const char *tmpl = NULL;
    bool force = false;

    for (int i = 2; i < argc; i++) {
        if (strncmp(argv[i], "--template=", 11) == 0) {
            tmpl = argv[i] + 11;
        } else if (strcmp(argv[i], "--template") == 0 && i + 1 < argc) {
            tmpl = argv[++i];
        } else if (strcmp(argv[i], "--force") == 0 || strcmp(argv[i], "-f") == 0) {
            force = true;
        } else if (strcmp(argv[i], "--list") == 0) {
            neowall_scaffold_list_templates(stdout);
            return EXIT_SUCCESS;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "Unknown option for 'new': %s\n", argv[i]);
            return EXIT_FAILURE;
        } else if (!shader_name) {
            shader_name = argv[i];
        }
    }

    if (!shader_name) {
        fprintf(stderr, "Usage: %s new <name> [--template=NAME] [--force]\n\n", argv[0]);
        neowall_scaffold_list_templates(stderr);
        fprintf(stderr, "\nExample: %s new aurora --template=scene\n", argv[0]);
        return EXIT_FAILURE;
    }
    return neowall_scaffold_create(shader_name, tmpl, force) ? EXIT_SUCCESS : EXIT_FAILURE;
}

/* Write `text` to `path` unless it exists and !force. */
static bool write_new_file(const char *path, const char *text, bool force) {
    if (!force && access(path, F_OK) == 0) {
        fprintf(stderr, "%s already exists (use --force to overwrite)\n", path);
        return false;
    }
    FILE *f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "can't write %s: %s\n", path, strerror(errno));
        return false;
    }
    bool ok = fputs(text, f) >= 0;
    ok = fclose(f) == 0 && ok;
    if (!ok) fprintf(stderr, "can't write %s: %s\n", path, strerror(errno));
    return ok;
}

/* `neowall import <export.json> [name] [--force]`: a Shadertoy export into
 * ~/.config/neowall/shaders/<name>.glsl (+ .neowall), then checked by
 * compiling the source model. */
int cmd_import(int argc, char *argv[]) {
    const char *in = NULL, *name = NULL;
    bool force = false;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--force") || !strcmp(argv[i], "-f")) force = true;
        else if (argv[i][0] == '-' && argv[i][1]) {
            fprintf(stderr, "Unknown option for 'import': %s\n", argv[i]);
            return EXIT_FAILURE;
        } else if (!in) in = argv[i];
        else if (!name) name = argv[i];
    }
    if (!in) {
        fprintf(stderr, "Usage: %s import <shadertoy-export.json> [name] [--force]\n"
                        "  Export from shadertoy.com (the shader page's Export button, or the API's\n"
                        "  /api/v1/shaders/<id>) and import it as a neowall shader.\n", argv[0]);
        return EXIT_FAILURE;
    }
    FILE *f = !strcmp(in, "-") ? stdin : fopen(in, "rb");
    if (!f) {
        fprintf(stderr, "can't read %s: %s\n", in, strerror(errno));
        return EXIT_FAILURE;
    }
    nw_str json = {0};
    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0 && json.len < (64u << 20)) nw_str_append_n(&json, buf, n);
    if (f != stdin) fclose(f);

    nw_import im = {0};
    char err[256];
    nw_result r = nw_shadertoy_import(json.data ? json.data : "", json.len, &im, err, sizeof(err));
    nw_str_free(&json);
    if (nw_is_err(r)) {
        fprintf(stderr, "%s: %s\n", in, err[0] ? err : "not a Shadertoy export");
        return EXIT_FAILURE;
    }

    /* the file name: given, else the title as a slug, else the input's stem */
    char stem[128] = "";
    const char *from = name ? name : im.name;
    for (size_t i = 0, k = 0; from && from[i] && k < sizeof(stem) - 1; i++) {
        unsigned char c = (unsigned char)from[i];
        if (isalnum(c)) stem[k++] = (char)tolower(c);
        else if (k && stem[k - 1] != '_') stem[k++] = '_';
        stem[k] = '\0';
    }
    size_t sl = strlen(stem);
    while (sl && stem[sl - 1] == '_') stem[--sl] = '\0';
    if (!sl) snprintf(stem, sizeof(stem), "imported");

    char dir[4096], glsl[4352], man[4352];
    if (!expand_path("~/.config/neowall/shaders", dir, sizeof(dir))) {
        nw_import_free(&im);
        return EXIT_FAILURE;
    }
    char partial[4096];
    snprintf(partial, sizeof(partial), "%s", dir);
    for (char *s = partial + 1; *s; s++) {
        if (*s != '/') continue;
        *s = '\0';
        (void)mkdir(partial, 0755);
        *s = '/';
    }
    (void)mkdir(partial, 0755);
    snprintf(glsl, sizeof(glsl), "%s/%s.glsl", dir, stem);
    snprintf(man, sizeof(man), "%s/%s.neowall", dir, stem);

    bool ok = write_new_file(glsl, im.glsl, force);
    if (ok && im.manifest) ok = write_new_file(man, im.manifest, force);
    if (ok) {
        printf("Imported %s\n  %s\n", im.name ? im.name : "shader", glsl);
        if (im.manifest) printf("  %s\n", man);
        if (im.notes) printf("\nNot carried over:\n%s", im.notes);
        /* parse it back, so a broken export is caught here, not at login */
        nw_shader_source *src = NULL;
        log_quiet_errors(true);
        nw_result pr = nw_shader_source_load(glsl, &src);
        log_quiet_errors(false);
        if (nw_is_ok(pr)) {
            printf("\n%zu pass(es). Try it:\n  %s watch %s.glsl\n", src->passes.len, argv[0], stem);
            nw_shader_source_free(src);
        } else {
            printf("\nwarning: it doesn't parse (%s); `%s watch %s.glsl` shows the errors\n",
                   pr.context ? pr.context : "?", argv[0], stem);
        }
    }
    nw_import_free(&im);
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}

/* `neowall docs [query]`: every helper and uniform, from the real GLSL. */
int cmd_docs(int argc, char *argv[]) {
    const char *chunks[32];
    size_t nc = nw_shader_preamble_chunks(chunks, 32);
    static shader_doc_entry entries[1024];
    size_t n = shader_docs_collect(chunks, nc, entries, 1024);
    const char *q = argc >= 3 ? argv[2] : NULL;
    bool color = isatty(STDOUT_FILENO) && !getenv("NO_COLOR");
    size_t shown = shader_docs_print(stdout, entries, n, q, color);
    if (!shown) {
        fprintf(stderr, "Nothing matches '%s'. Try `%s docs` for everything.\n", q, argv[0]);
        return EXIT_FAILURE;
    }
    if (!q) printf("Search with: %s docs <word>   e.g. noise, palette, audio, window\n", argv[0]);
    return EXIT_SUCCESS;
}
