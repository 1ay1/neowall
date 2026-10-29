/* See neowall/shader/term_gpu.h. */
#include "neowall/shader/term_gpu.h"

#include <stdlib.h>

#include "neowall/log.h"
#include <GL/gl.h>
#include <GL/glext.h>
#include "neowall/log.h"

#ifdef NEOWALL_HAVE_TERMINAL
#include "term_render.h"

struct term_gpu {
    term_render *term;
    GLuint cells, change, atlas, color_atlas;
    float fx[4];   /* bloom, scanline, crt curve, chroma (0 = off) */
    float fade;    /* change-driven fade (0 = off) */
    int cursor_x, cursor_y;
    double cursor_moved_at;
    bool cursor_seen;
    /* what term_gpu_frame hands the engine */
    float info[4], atlas_size[2], cursor[3], cursor_prev[4], fade_now[2];
    nw_uniform_value unis[6];
    nw_texture_value texs[4];
};

static GLuint grid_texture(GLint ifmt, GLenum fmt, int cols, int rows) {
    GLuint t = 0;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexImage2D(GL_TEXTURE_2D, 0, ifmt, cols, rows, 0, fmt, GL_UNSIGNED_INT, NULL);
    /* integer textures can't be filtered */
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return t;
}

static GLuint atlas_texture(GLint ifmt, GLenum fmt, int w, int h) {
    GLuint t = 0;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, ifmt, w, h, 0, fmt, GL_UNSIGNED_BYTE, NULL);
    /* linear gives sub-pixel AA on the coverage bitmap */
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return t;
}

nw_result term_gpu_new(const term_gpu_opts *o, term_gpu **out) {
    term_gpu *t = calloc(1, sizeof(*t));
    if (!t) return nw_err(NW_ERR_OOM, "term_gpu");
    term_render_opts ro = {
        .cmd = o->cmd,
        .cols = o->cols > 0 ? o->cols : 100,
        .rows = o->rows > 0 ? o->rows : 30,
        .cell_w = o->cell_w > 0 ? o->cell_w : 9,
        .cell_h = o->cell_h > 0 ? o->cell_h : 18,
        .font_path = o->font_path,
        .font_bold_path = o->font_bold,
        .font_italic_path = o->font_italic,
        .cwd = o->cwd && o->cwd[0] ? o->cwd : NULL,
        .term_env = o->term_env && o->term_env[0] ? o->term_env : NULL,
        .default_fg = o->fg,
        .default_bg = o->bg,
        .on_output = o->on_output,
        .on_output_ctx = o->on_output_ctx,
    };
    nw_result err = nw_ok();
    t->term = term_render_create(&ro, &err);
    if (!t->term) {
        log_error("terminal: %s (%s)", nw_status_str(err.status), err.context ? err.context : "");
        free(t);
        return nw_is_err(err) ? err : nw_err(NW_ERR_IO, "terminal");
    }
    int cols = term_render_cols(t->term), rows = term_render_rows(t->term);
    int aw = term_render_atlas_w(t->term), ah = term_render_atlas_h(t->term);
    t->cells = grid_texture(GL_RGBA32UI, GL_RGBA_INTEGER, cols, rows);
    t->change = grid_texture(GL_R32UI, GL_RED_INTEGER, cols, rows);
    t->atlas = atlas_texture(GL_R8, GL_RED, aw, ah);
    /* only when a color emoji font loaded; no cell sets the color flag otherwise */
    if (term_render_color_atlas(t->term)) t->color_atlas = atlas_texture(GL_RGBA8, GL_RGBA, aw, ah);
    log_info("Attached terminal '%s' (%dx%d cells, %dx%d px)", o->cmd, ro.cols, ro.rows, ro.cell_w,
             ro.cell_h);
    *out = t;
    return nw_ok();
}

void term_gpu_shutdown(term_gpu *t) {
    if (t && t->term) {
        term_render_destroy(t->term);
        t->term = NULL;
    }
}

void term_gpu_free(term_gpu *t) {
    if (!t) return;
    term_gpu_shutdown(t);
    GLuint ts[] = {t->cells, t->change, t->atlas, t->color_atlas};
    for (size_t i = 0; i < sizeof(ts) / sizeof(ts[0]); i++)
        if (ts[i]) glDeleteTextures(1, &ts[i]);
    free(t);
}

void term_gpu_set_fx(term_gpu *t, const float fx[4], float fade) {
    for (int i = 0; i < 4; i++) t->fx[i] = fx[i];
    t->fade = fade;
}

/* Upload rows [y0,y1) of a w-wide image whose rows are `stride` bytes apart. */
static void upload_rows(GLuint tex, int w, int y0, int y1, GLenum fmt, GLenum type, const void *rows,
                        size_t stride, int align) {
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, align);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, w);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, y0, w, y1 - y0, fmt, type,
                    (const char *)rows + (size_t)y0 * stride);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
}

static void clamp_band(int *y0, int *y1, int n) {
    if (*y0 < 0) *y0 = 0;
    if (*y1 > n) *y1 = n;
}

static void upload(term_gpu *t) {
    term_render *tr = t->term;
    bool cells_changed = term_render_update(tr);
    int aw = term_render_atlas_w(tr), ah = term_render_atlas_h(tr);

    if (term_render_atlas_dirty(tr)) {
        int y0 = 0, y1 = ah;
        term_render_atlas_dirty_rows(tr, &y0, &y1);
        clamp_band(&y0, &y1, ah);
        if (y1 > y0) upload_rows(t->atlas, aw, y0, y1, GL_RED, GL_UNSIGNED_BYTE, term_render_atlas(tr), (size_t)aw, 1);
        term_render_clear_atlas_dirty(tr);
    }
    if (t->color_atlas && term_render_color_atlas_dirty(tr)) {
        int y0 = 0, y1 = ah;
        term_render_color_atlas_dirty_rows(tr, &y0, &y1);
        clamp_band(&y0, &y1, ah);
        const uint8_t *bits = term_render_color_atlas(tr);
        if (bits && y1 > y0) upload_rows(t->color_atlas, aw, y0, y1, GL_RGBA, GL_UNSIGNED_BYTE, bits, (size_t)aw * 4, 1);
        term_render_clear_color_atlas_dirty(tr);
    }
    if (cells_changed) {
        /* only the band of rows that changed since the last frame */
        int cols = term_render_cols(tr), rows = term_render_rows(tr);
        int y0 = 0, y1 = rows;
        term_render_cells_dirty_rows(tr, &y0, &y1);
        clamp_band(&y0, &y1, rows);
        if (y1 > y0) {
            upload_rows(t->cells, cols, y0, y1, GL_RGBA_INTEGER, GL_UNSIGNED_INT, term_render_cells(tr),
                        (size_t)cols * 16, 4);
            const uint32_t *chg = term_render_change_ms(tr);
            if (chg) upload_rows(t->change, cols, y0, y1, GL_RED_INTEGER, GL_UNSIGNED_INT, chg, (size_t)cols * 4, 4);
        }
    }
}

void term_gpu_frame(term_gpu *t, double time, const nw_uniform_value **unis, size_t *n_unis,
                    const nw_texture_value **texs, size_t *n_texs) {
    *n_unis = *n_texs = 0;
    if (!t->term) return;
    upload(t);
    term_render *tr = t->term;

    int cx = 0, cy = 0;
    bool vis = false;
    term_render_cursor(tr, &cx, &cy, &vis);
    /* the shader slides the drawn cursor from the old cell to the new one,
     * so remember where it was and when it moved */
    if (!t->cursor_seen) {
        t->cursor_x = cx;
        t->cursor_y = cy;
        t->cursor_moved_at = time;
        t->cursor_seen = true;
    } else if (cx != t->cursor_x || cy != t->cursor_y) {
        t->cursor_moved_at = time;
    }
    t->cursor_prev[0] = (float)t->cursor_x;
    t->cursor_prev[1] = (float)t->cursor_y;
    t->cursor_prev[2] = (float)t->cursor_moved_at;
    t->cursor_prev[3] = 0;
    t->cursor_x = cx;
    t->cursor_y = cy;

    t->info[0] = (float)term_render_cols(tr);
    t->info[1] = (float)term_render_rows(tr);
    t->info[2] = (float)term_render_cell_w(tr);
    t->info[3] = (float)term_render_cell_h(tr);
    t->atlas_size[0] = (float)term_render_atlas_w(tr);
    t->atlas_size[1] = (float)term_render_atlas_h(tr);
    t->cursor[0] = (float)cx;
    t->cursor[1] = (float)cy;
    t->cursor[2] = vis ? 1.0f : 0.0f;
    t->fade_now[0] = t->fade;
    t->fade_now[1] = (float)term_render_now_ms(tr);

    size_t n = 0;
    t->unis[n++] = (nw_uniform_value){.name = "iTermInfo", .count = 4, .v = t->info};
    t->unis[n++] = (nw_uniform_value){.name = "iTermAtlasSize", .count = 2, .v = t->atlas_size};
    t->unis[n++] = (nw_uniform_value){.name = "iTermCursor", .count = 3, .v = t->cursor};
    t->unis[n++] = (nw_uniform_value){.name = "iTermCursorPrev", .count = 4, .v = t->cursor_prev};
    t->unis[n++] = (nw_uniform_value){.name = "iTermFX", .count = 4, .v = t->fx};
    t->unis[n++] = (nw_uniform_value){.name = "iTermFade", .count = 2, .v = t->fade_now};
    *unis = t->unis;
    *n_unis = n;

    /* stand-ins keep every sampler valid: the shader only reads the color
     * atlas for cells flagged color, and the fade is gated by iTermFade.x */
    t->texs[0] = (nw_texture_value){"iTermCells", t->cells};
    t->texs[1] = (nw_texture_value){"iTermChange", t->change ? t->change : t->cells};
    t->texs[2] = (nw_texture_value){"iTermAtlas", t->atlas};
    t->texs[3] = (nw_texture_value){"iTermColorAtlas", t->color_atlas ? t->color_atlas : t->atlas};
    *texs = t->texs;
    *n_texs = 4;
}

bool term_gpu_resize(term_gpu *t, int cols, int rows) {
    if (!t || !t->term || cols <= 0 || rows <= 0) return false;
    if (term_render_cols(t->term) == cols && term_render_rows(t->term) == rows) return true;
    nw_result r = term_render_resize(t->term, cols, rows);
    if (nw_is_err(r)) {
        log_error("Terminal resize failed: %s", r.context ? r.context : nw_status_str(r.status));
        return false;
    }
    glBindTexture(GL_TEXTURE_2D, t->cells);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32UI, cols, rows, 0, GL_RGBA_INTEGER, GL_UNSIGNED_INT, NULL);
    glBindTexture(GL_TEXTURE_2D, t->change);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32UI, cols, rows, 0, GL_RED_INTEGER, GL_UNSIGNED_INT, NULL);
    log_info("Resized terminal wallpaper grid to %dx%d", cols, rows);
    return true;
}

bool term_gpu_mouse(term_gpu *t, int px, int py, int button, bool pressed, bool motion) {
    return t && t->term && term_render_mouse(t->term, px, py, button, pressed, motion);
}

bool term_gpu_write(term_gpu *t, const void *bytes, size_t len) {
    return t && t->term && term_render_write(t->term, bytes, len);
}

bool term_gpu_animating(const term_gpu *t, unsigned fx_settle_ms) {
    return t && t->term && term_render_animating(t->term, fx_settle_ms);
}

/* no terminal counts as exited: callers ask "is there a live child?" */
bool term_gpu_child_exited(const term_gpu *t) { return !t || !t->term || term_render_child_exited(t->term); }

bool term_gpu_damage(const term_gpu *t, int surf_w, int surf_h, int *x, int *y, int *w, int *h) {
    if (!t || !t->term || surf_w <= 0 || surf_h <= 0) return false;
    int rows = term_render_rows(t->term);
    if (rows <= 0) return false;
    int y0 = 0, y1 = rows;
    term_render_cells_dirty_rows(t->term, &y0, &y1);
    clamp_band(&y0, &y1, rows);
    if (y1 <= y0) return false;              /* nothing changed */
    if (y0 == 0 && y1 == rows) return false; /* everything: full damage */
    /* a one-row margin each side for glyph overhang and atlas bleed */
    double px_per_row = (double)surf_h / (double)rows;
    int top = (int)((double)(y0 - 1) * px_per_row);
    int bot = (int)((double)(y1 + 1) * px_per_row + 0.5);
    if (top < 0) top = 0;
    if (bot > surf_h) bot = surf_h;
    if (bot <= top) return false;
    /* top-down band to EGL's bottom-left origin */
    *x = 0;
    *y = surf_h - bot;
    *w = surf_w;
    *h = bot - top;
    return true;
}

#else /* !NEOWALL_HAVE_TERMINAL */

nw_result term_gpu_new(const term_gpu_opts *o, term_gpu **out) {
    (void)o;
    (void)out;
    return nw_err(NW_ERR_UNSUPPORTED, "built without terminal support");
}
void term_gpu_free(term_gpu *t) { (void)t; }
void term_gpu_shutdown(term_gpu *t) { (void)t; }
void term_gpu_set_fx(term_gpu *t, const float fx[4], float fade) {
    (void)t;
    (void)fx;
    (void)fade;
}
void term_gpu_frame(term_gpu *t, double time, const nw_uniform_value **unis, size_t *n_unis,
                    const nw_texture_value **texs, size_t *n_texs) {
    (void)t;
    (void)time;
    *unis = NULL;
    *texs = NULL;
    *n_unis = *n_texs = 0;
}
bool term_gpu_resize(term_gpu *t, int cols, int rows) {
    (void)t;
    (void)cols;
    (void)rows;
    return false;
}
bool term_gpu_mouse(term_gpu *t, int px, int py, int button, bool pressed, bool motion) {
    (void)t;
    (void)px;
    (void)py;
    (void)button;
    (void)pressed;
    (void)motion;
    return false;
}
bool term_gpu_write(term_gpu *t, const void *bytes, size_t len) {
    (void)t;
    (void)bytes;
    (void)len;
    return false;
}
bool term_gpu_animating(const term_gpu *t, unsigned fx_settle_ms) {
    (void)t;
    (void)fx_settle_ms;
    return false;
}
bool term_gpu_child_exited(const term_gpu *t) {
    (void)t;
    return true;
}
bool term_gpu_damage(const term_gpu *t, int surf_w, int surf_h, int *x, int *y, int *w, int *h) {
    (void)t;
    (void)surf_w;
    (void)surf_h;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    return false;
}

#endif
