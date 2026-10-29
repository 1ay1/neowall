/* neowall/shader/shadertoy_import.h — a Shadertoy JSON export to a neowall
 * shader.
 *
 * The export (the site's "Export" button, or the API's /shaders/<id>) holds
 * every pass's code and its iChannel inputs. This turns it into one .glsl,
 * passes marked with `// Buffer A` style comments in the right order, plus
 * a .neowall sidecar with the channel bindings, filters and wraps.
 *
 * Inputs neowall can't provide get a stand-in and a note in the result:
 * Shadertoy's photo textures and cube maps (not ours to ship), video,
 * webcam, music. GL-free. */
#ifndef NEOWALL_SHADERTOY_IMPORT_H
#define NEOWALL_SHADERTOY_IMPORT_H

#include <stddef.h>

#include "neowall/result.h"

typedef struct {
    char *glsl;     /* the shader text */
    char *manifest; /* the .neowall text, or NULL when the defaults are right */
    char *notes;    /* what couldn't be carried over, one per line, or NULL */
    char *name;     /* the shader's title, or NULL */
} nw_import;

/* Parse `json` (len bytes). On failure *out is untouched and `err` says why. */
nw_result nw_shadertoy_import(const char *json, size_t len, nw_import *out, char *err, size_t err_cap);
void nw_import_free(nw_import *im);

#endif /* NEOWALL_SHADERTOY_IMPORT_H */
