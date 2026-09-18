/*
 * source.c — registry, spec parsing, and instance lifecycle for the data plane.
 *
 * See include/neowall/source/source.h for the contract, and
 * docs/architecture/SHADER_ENGINE_V2.md for why this layer exists.
 *
 * No GL and no display server: everything here is string work, a table, and
 * scheduling arithmetic against an injected clock. That is deliberate — it is
 * what lets the whole data plane be tested headless.
 */

#include "neowall/source/source.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ==========================================================================
 * Registry
 *
 * A flat array scanned linearly. Provider counts are in the dozens and lookup
 * happens at graph-build time, never per frame, so a hash table would be
 * ceremony. Entries are borrowed pointers to static vtables.
 * ========================================================================== */

static const nw_source_vtable *g_registry[NW_SOURCE_REGISTRY_MAX];
static size_t                  g_registry_count;

nw_result nw_source_register(const nw_source_vtable *vt) {
    if (!vt || !vt->name || !vt->name[0]) {
        return nw_err(NW_ERR_INVALID_ARG, "source: vtable needs a name");
    }
    if (!vt->open) {
        return nw_err(NW_ERR_INVALID_ARG, "source: vtable needs open()");
    }
    if (vt->kind == NW_SOURCE_SCALAR && !vt->scalar) {
        return nw_err(NW_ERR_INVALID_ARG, "source: scalar provider needs scalar()");
    }
    if (vt->kind == NW_SOURCE_TEXTURE && !vt->texture) {
        return nw_err(NW_ERR_INVALID_ARG, "source: texture provider needs texture()");
    }
    if (strlen(vt->name) >= NW_SOURCE_NAME_MAX) {
        return nw_err(NW_ERR_INVALID_ARG, "source: provider name too long");
    }
    if (nw_source_find(vt->name)) {
        return nw_err(NW_ERR_INVALID_ARG, "source: provider already registered");
    }
    if (g_registry_count >= NW_SOURCE_REGISTRY_MAX) {
        return nw_err(NW_ERR_OOM, "source: registry full");
    }
    g_registry[g_registry_count++] = vt;
    return nw_ok();
}

const nw_source_vtable *nw_source_find(const char *name) {
    if (!name || !name[0]) {
        return NULL;
    }
    for (size_t i = 0; i < g_registry_count; i++) {
        if (strcmp(g_registry[i]->name, name) == 0) {
            return g_registry[i];
        }
    }
    return NULL;
}

void nw_source_registry_reset(void) {
    memset(g_registry, 0, sizeof(g_registry));
    g_registry_count = 0;
}

size_t nw_source_registry_count(void) { return g_registry_count; }

const char *nw_source_registry_name_at(size_t index) {
    if (index >= g_registry_count) {
        return NULL;
    }
    return g_registry[index]->name;
}

/* ==========================================================================
 * Spec parsing
 * ========================================================================== */

static const char *skip_ws(const char *p) {
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    return p;
}

/* Is `text` entirely a number? strtod does the parsing; we only need to know
 * that it consumed everything, so "5s" stays a modifier and not a value. */
static bool parse_number(const char *text, double *out) {
    char *end = NULL;
    double v = strtod(text, &end);
    if (end == text) {
        return false;
    }
    end = (char *)skip_ws(end);
    if (*end != '\0') {
        return false;
    }
    *out = v;
    return true;
}

/*
 * A duration modifier: 500ms, 5s, 2m, 1h. A bare number means milliseconds.
 * Returns false when this is not a duration at all, which is how the caller
 * tells `watch` apart from `5s` without a keyword table.
 */
static bool parse_duration(const char *text, uint32_t *out_ms) {
    char  *end = NULL;
    double v   = strtod(text, &end);
    if (end == text || v < 0) {
        return false;
    }
    end = (char *)skip_ws(end);

    double ms;
    if (strcmp(end, "ms") == 0 || *end == '\0') {
        ms = v;
    } else if (strcmp(end, "s") == 0) {
        ms = v * 1000.0;
    } else if (strcmp(end, "m") == 0) {
        ms = v * 60000.0;
    } else if (strcmp(end, "h") == 0) {
        ms = v * 3600000.0;
    } else {
        return false;
    }

    /* Clamp rather than wrap. A silently-wrapped interval would turn a typo
     * into a busy loop, which is exactly the failure this layer must not have. */
    if (ms > (double)UINT32_MAX) {
        ms = (double)UINT32_MAX;
    }
    *out_ms = (uint32_t)ms;
    return true;
}

/* Copy [start,end) into a fixed buffer, trimming surrounding space and one
 * layer of matching quotes. Returns false if it does not fit. */
static bool copy_token(const char *start, const char *end, char *out, size_t cap) {
    while (start < end && isspace((unsigned char)*start)) {
        start++;
    }
    while (end > start && isspace((unsigned char)end[-1])) {
        end--;
    }
    if (end - start >= 2 && (*start == '"' || *start == '\'') && end[-1] == *start) {
        start++;
        end--;
    }
    size_t len = (size_t)(end - start);
    if (len >= cap) {
        return false;
    }
    memcpy(out, start, len);
    out[len] = '\0';
    return true;
}

/* Find the closing paren matching the open paren at `open`, honouring quotes
 * so a command like exec("echo )") does not terminate early. */
static const char *match_paren(const char *open) {
    int  depth = 0;
    char quote = '\0';
    for (const char *p = open; *p; p++) {
        if (quote) {
            if (*p == '\\' && p[1]) {
                p++;
            } else if (*p == quote) {
                quote = '\0';
            }
            continue;
        }
        if (*p == '"' || *p == '\'') {
            quote = *p;
        } else if (*p == '(') {
            depth++;
        } else if (*p == ')') {
            if (--depth == 0) {
                return p;
            }
        }
    }
    return NULL;
}

/* Split the inside of a call on top-level commas (quote- and paren-aware).
 * Returns the number of fields found, or -1 on overflow. */
static int split_args(const char *body, const char *end, const char **starts,
                      const char **ends, int max_fields) {
    int         count = 0;
    int         depth = 0;
    char        quote = '\0';
    const char *field = body;

    for (const char *p = body; p < end; p++) {
        if (quote) {
            if (*p == '\\' && p + 1 < end) {
                p++;
            } else if (*p == quote) {
                quote = '\0';
            }
            continue;
        }
        if (*p == '"' || *p == '\'') {
            quote = *p;
        } else if (*p == '(' || *p == '[') {
            depth++;
        } else if (*p == ')' || *p == ']') {
            depth--;
        } else if (*p == ',' && depth == 0) {
            if (count >= max_fields) {
                return -1;
            }
            starts[count] = field;
            ends[count]   = p;
            count++;
            field = p + 1;
        }
    }
    if (count >= max_fields) {
        return -1;
    }
    starts[count] = field;
    ends[count]   = end;
    count++;
    return count;
}

#define NW_SOURCE_MAX_ARGS 8

nw_result nw_source_spec_parse(const char *text, nw_source_spec *out) {
    if (!text || !out) {
        return nw_err(NW_ERR_INVALID_ARG, "source: null spec");
    }

    memset(out, 0, sizeof(*out));

    const char *p = skip_ws(text);
    if (!*p) {
        return nw_err(NW_ERR_PARSE, "source: empty spec");
    }

    /* A bare number is a constant. Checked before the call form so that a
     * value like "1e3" is a number and not a mysterious provider. */
    double number = 0.0;
    if (parse_number(p, &number)) {
        snprintf(out->name, sizeof(out->name), "const");
        out->number     = number;
        out->has_number = true;
        return nw_ok();
    }

    const char *open = strchr(p, '(');
    if (!open) {
        /* Bare provider name. */
        const char *end = p;
        while (*end && !isspace((unsigned char)*end)) {
            end++;
        }
        if (!copy_token(p, end, out->name, sizeof(out->name))) {
            return nw_err(NW_ERR_PARSE, "source: provider name too long");
        }
        if (!out->name[0]) {
            return nw_err(NW_ERR_PARSE, "source: empty provider name");
        }
        if (*skip_ws(end)) {
            return nw_err(NW_ERR_PARSE, "source: trailing text after provider name");
        }
        return nw_ok();
    }

    /* Call form: name(arg, modifiers...) */
    if (!copy_token(p, open, out->name, sizeof(out->name))) {
        return nw_err(NW_ERR_PARSE, "source: provider name too long");
    }
    if (!out->name[0]) {
        return nw_err(NW_ERR_PARSE, "source: call with no provider name");
    }

    const char *close = match_paren(open);
    if (!close) {
        return nw_err(NW_ERR_PARSE, "source: unclosed '(' in spec");
    }
    if (*skip_ws(close + 1)) {
        return nw_err(NW_ERR_PARSE, "source: trailing text after ')'");
    }

    const char *starts[NW_SOURCE_MAX_ARGS];
    const char *ends[NW_SOURCE_MAX_ARGS];
    int fields = split_args(open + 1, close, starts, ends, NW_SOURCE_MAX_ARGS);
    if (fields < 0) {
        return nw_err(NW_ERR_PARSE, "source: too many arguments");
    }

    /* First field is the argument; it may legitimately be empty, as in cpu(). */
    if (fields >= 1) {
        if (!copy_token(starts[0], ends[0], out->arg, sizeof(out->arg))) {
            return nw_err(NW_ERR_PARSE, "source: argument too long");
        }
    }

    /* Remaining fields are modifiers. */
    for (int i = 1; i < fields; i++) {
        char mod[64];
        if (!copy_token(starts[i], ends[i], mod, sizeof(mod))) {
            return nw_err(NW_ERR_PARSE, "source: modifier too long");
        }
        if (!mod[0]) {
            continue;
        }
        uint32_t ms = 0;
        if (parse_duration(mod, &ms)) {
            out->interval_ms = ms;
        } else if (strcmp(mod, "watch") == 0) {
            out->flags |= NW_SOURCE_SPEC_WATCH;
        } else {
            return nw_err(NW_ERR_PARSE, "source: unknown modifier");
        }
    }

    return nw_ok();
}

/* ==========================================================================
 * Instances
 * ========================================================================== */

nw_result nw_source_open(const nw_source_spec *spec, nw_source *out, char *err, size_t err_cap) {
    if (!spec || !out) {
        return nw_err(NW_ERR_INVALID_ARG, "source: null open args");
    }
    if (err && err_cap) {
        err[0] = '\0';
    }

    memset(out, 0, sizeof(*out));

    const nw_source_vtable *vt = nw_source_find(spec->name);
    if (!vt) {
        if (err && err_cap) {
            snprintf(err, err_cap, "unknown source '%s'", spec->name);
        }
        return nw_err(NW_ERR_NOT_FOUND, "source: unknown provider");
    }

    char provider_err[NW_SOURCE_ERR_MAX] = {0};
    void *self = vt->open(spec, provider_err, sizeof(provider_err));
    if (!self) {
        if (err && err_cap) {
            snprintf(err, err_cap, "%s: %s", spec->name,
                     provider_err[0] ? provider_err : "failed to open");
        }
        return nw_err(NW_ERR_BACKEND, "source: provider open failed");
    }

    out->vt   = vt;
    out->self = self;
    /* Spec interval wins; otherwise the provider's own default. A file under
     * /sys wants seconds, the audio FFT wants every frame. */
    out->interval_ms  = spec->interval_ms ? spec->interval_ms : vt->default_interval_ms;
    out->visible      = true;
    out->last_tick_ms = 0;
    out->ticked_once  = false;
    return nw_ok();
}

nw_result nw_source_open_text(const char *text, nw_source *out, char *err, size_t err_cap) {
    nw_source_spec spec;
    nw_result      r = nw_source_spec_parse(text, &spec);
    if (nw_is_err(r)) {
        if (err && err_cap) {
            snprintf(err, err_cap, "%s", r.context ? r.context : "parse failed");
        }
        return r;
    }
    return nw_source_open(&spec, out, err, err_cap);
}

void nw_source_destroy(nw_source *src) {
    if (!src || !src->vt) {
        return;
    }
    if (src->vt->close && src->self) {
        src->vt->close(src->self);
    }
    memset(src, 0, sizeof(*src));
}

bool nw_source_tick(nw_source *src, const nw_source_ctx *ctx) {
    if (!src || !src->vt || !ctx) {
        return false;
    }
    /* The occlusion contract. A hidden source does not poll, does not spawn,
     * does not sample. This is the single place it is enforced. */
    if (!src->visible) {
        return false;
    }
    if (!src->vt->tick) {
        return false;
    }

    /* First tick after becoming visible always runs, so a source coming back
     * into view is fresh instead of showing a stale value for one interval. */
    if (src->ticked_once && src->interval_ms > 0) {
        uint64_t elapsed = ctx->now_ms - src->last_tick_ms;
        if (elapsed < (uint64_t)src->interval_ms) {
            return false;
        }
    }

    src->vt->tick(src->self, ctx);
    src->last_tick_ms = ctx->now_ms;
    src->ticked_once  = true;

    if (src->vt->kind == NW_SOURCE_SCALAR && src->vt->scalar) {
        src->cached_scalar = src->vt->scalar(src->self);
    }
    return true;
}

void nw_source_set_visible(nw_source *src, bool visible) {
    if (!src || !src->vt) {
        return;
    }
    if (src->visible == visible) {
        return;
    }
    src->visible = visible;

    /* Becoming visible clears the schedule so the next tick runs immediately
     * rather than honouring an interval that elapsed while nobody was looking. */
    if (visible) {
        src->ticked_once = false;
    }
    if (src->vt->on_visibility) {
        src->vt->on_visibility(src->self, visible);
    }
}

float nw_source_scalar(const nw_source *src) {
    if (!src || !src->vt || src->vt->kind != NW_SOURCE_SCALAR) {
        return 0.0f;
    }
    /* Serve the cache so a value read many times per frame costs one sample,
     * and so a hidden source reports its last known value instead of 0. */
    if (src->ticked_once) {
        return src->cached_scalar;
    }
    return src->vt->scalar ? src->vt->scalar(src->self) : 0.0f;
}

unsigned nw_source_texture(const nw_source *src) {
    if (!src || !src->vt || src->vt->kind != NW_SOURCE_TEXTURE) {
        return 0;
    }
    return src->vt->texture ? src->vt->texture(src->self) : 0;
}

const char *nw_source_name(const nw_source *src) {
    if (!src || !src->vt) {
        return "<none>";
    }
    return src->vt->name;
}

nw_source_kind nw_source_kind_of(const nw_source *src) {
    if (!src || !src->vt) {
        return NW_SOURCE_SCALAR;
    }
    return src->vt->kind;
}
