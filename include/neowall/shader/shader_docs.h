#ifndef NEOWALL_SHADER_DOCS_H
#define NEOWALL_SHADER_DOCS_H

/* `neowall docs [query]`: every helper and uniform a shader can use, with its
 * signature and the comment written above it, pulled straight from the GLSL
 * neowall injects. Generated from the source, so it can't go stale. */

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

typedef struct {
    char signature[160];   /* "float nwFbm(vec2 p, int oct)" / "uniform float iCpu" */
    char name[48];
    char doc[240];         /* comment lines above it, joined; may be empty */
    char section[64];      /* "value noise + fbm", from the section ruler */
    bool uniform;
} shader_doc_entry;

/* Scan GLSL chunks for nw* functions and i* uniforms. Returns the count. */
size_t shader_docs_collect(const char *const *chunks, size_t nchunks,
                           shader_doc_entry *out, size_t max);

/* Print entries matching query (case-insensitive substring of name or doc;
 * NULL = all), grouped as uniforms then functions. Returns how many printed. */
size_t shader_docs_print(FILE *f, const shader_doc_entry *e, size_t n,
                         const char *query, bool color);

#endif /* NEOWALL_SHADER_DOCS_H */
