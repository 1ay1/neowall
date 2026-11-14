/* app_update with no loop, no display and no files: a message goes in, the
 * model and the list of effects come out. */

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_internal.h"
#include "neowall/app/app.h"
#include "neowall/core/constants.h"
#include "neowall/egl/gpu_health.h"

static int failures;
#define CHECK(c, ...) do { if (!(c)) { failures++; \
    fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
    fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)

static struct app_model model(void) {
    struct app_model a;
    memset(&a, 0, sizeof(a));   /* padding too, so memcmp works */
    app_model_init(&a);
    return a;
}

static rt_msg msg(uint32_t kind, uint32_t id, int64_t v) {
    rt_msg m = {.kind = kind, .id = id};
    m.v.i = v;
    return m;
}

/* Run one message; return the effect list (owned by the caller's cmds). */
static rt_cmds *step(struct app_model *a, rt_cmds *c, rt_msg m) {
    rt_cmds_clear(c);
    app_model_update(a, &m, c);
    return c;
}

static bool is_cmd(const rt_cmds *c, size_t i, uint32_t kind) {
    const rt_effect *e = rt_cmds_at(c, i);
    return e && e->tag == RT_EFFECT_CMD && e->u.cmd.kind == kind;
}

static void test_quit(rt_cmds *c) {
    struct app_model a = model();
    step(&a, c, msg(NW_MSG_QUIT, SIGTERM, 0));
    CHECK(rt_cmds_len(c) == 3, "quit: notify, stop, quit; got %zu", rt_cmds_len(c));
    CHECK(is_cmd(c, 0, APP_CMD_NOTIFY) && !strcmp(rt_cmds_at(c, 0)->u.cmd.v.s, "STOPPING=1"),
          "systemd hears STOPPING first");
    CHECK(is_cmd(c, 1, APP_CMD_STOP), "other threads told to stop");
    const rt_effect *q = rt_cmds_at(c, 2);
    CHECK(q && q->tag == RT_EFFECT_QUIT && q->u.code == 0, "loop quits with 0");
}

static void test_gpu_lost(rt_cmds *c) {
    struct app_model a = model();
    step(&a, c, msg(NW_MSG_GPU_LOST, 0, 0));
    const rt_effect *q = rt_cmds_at(c, 2);
    CHECK(is_cmd(c, 0, APP_CMD_NOTIFY) && is_cmd(c, 1, APP_CMD_STOP) && q &&
          q->tag == RT_EFFECT_QUIT && q->u.code == NW_EXIT_GPU_LOST,
          "gpu loss stops with the restart code");
}

static void test_next(rt_cmds *c) {
    struct app_model a = model();
    step(&a, c, msg(NW_MSG_NEXT, SIGUSR1, 0));
    CHECK(a.next_pending == 1 && rt_cmds_len(c) == 0, "next is a model change only");
    a.next_pending = MAX_NEXT_REQUESTS;
    step(&a, c, msg(NW_MSG_NEXT, 0, 0));
    CHECK(a.next_pending == MAX_NEXT_REQUESTS, "a full queue drops the request");
}

static void test_set_index(rt_cmds *c) {
    struct app_model a = model();
    /* From a signal the index is in a file, and reading it is an effect. */
    step(&a, c, msg(NW_MSG_SET_INDEX, 34, 0));
    CHECK(a.set_index == -1, "signal alone doesn't set an index");
    CHECK(rt_cmds_len(c) == 1 && is_cmd(c, 0, APP_CMD_READ_INDEX_FILE), "asks to read the file");
    /* perform answers with id 0 and the value. */
    step(&a, c, msg(NW_MSG_SET_INDEX, 0, 7));
    CHECK(a.set_index == 7 && rt_cmds_len(c) == 0, "socket/perform form sets it directly");
}

static void test_set_terminal(rt_cmds *c) {
    struct app_model a = model();
    step(&a, c, msg(NW_MSG_SET_TERMINAL, 37, 0));
    CHECK(!a.terminal_pending && is_cmd(c, 0, APP_CMD_READ_TERMINAL_FILE), "signal reads the file");
    rt_msg t = {.kind = NW_MSG_SET_TERMINAL};
    t.v.p = strdup("btop");     /* update owns and frees it */
    step(&a, c, t);
    CHECK(a.terminal_pending && !strcmp(a.terminal_cmd, "btop"), "command lands in the model");
    CHECK(rt_cmds_len(c) == 0, "no effect: present swaps it");
}

static void test_shader_pause(rt_cmds *c) {
    struct app_model a = model();
    step(&a, c, msg(NW_MSG_SHADER_PAUSE, 0, 0));
    CHECK(a.shader_paused && is_cmd(c, 0, APP_CMD_SHADER_PAUSE) && rt_cmds_at(c, 0)->u.cmd.v.i == 1,
          "pause freezes the clocks once");
    step(&a, c, msg(NW_MSG_SHADER_PAUSE, 0, 0));
    CHECK(rt_cmds_len(c) == 0, "pausing twice doesn't freeze twice");
    step(&a, c, msg(NW_MSG_SHADER_RESUME, 0, 0));
    CHECK(!a.shader_paused && is_cmd(c, 0, APP_CMD_SHADER_PAUSE) && rt_cmds_at(c, 0)->u.cmd.v.i == 0,
          "resume unfreezes");
}

static void test_reload(rt_cmds *c) {
    struct app_model a = model();
    step(&a, c, msg(NW_MSG_RELOAD, SIGHUP, 0));
    CHECK(is_cmd(c, 0, APP_CMD_RELOAD) && rt_cmds_at(c, 0)->u.cmd.v.u == 0,
          "SIGHUP reload has nobody to answer");
    step(&a, c, msg(NW_MSG_RELOAD, 0, 42));
    CHECK(is_cmd(c, 0, APP_CMD_RELOAD) && rt_cmds_at(c, 0)->u.cmd.v.u == 42,
          "socket reload carries its connection");
}

static void test_timers_and_display(rt_cmds *c) {
    struct app_model a = model();
    step(&a, c, msg(NW_MSG_FRAME_DUE, 5, 0));
    CHECK(is_cmd(c, 0, APP_CMD_FRAME) && rt_cmds_at(c, 0)->u.cmd.id == 5, "frame goes to its output");
    step(&a, c, msg(NW_MSG_DISPLAY, 0, 0));
    CHECK(rt_cmds_len(c) == 0, "no occlusion check without occlusion support");
    a.has_occlusion = true;
    step(&a, c, msg(NW_MSG_DISPLAY, 0, 0));
    CHECK(is_cmd(c, 0, APP_CMD_OCCLUSION), "occlusion checked after the display");
    step(&a, c, msg(NW_MSG_OUTPUTS_NEW, 0, 0));
    CHECK(a.outputs_need_init && rt_cmds_len(c) == 0, "hotplug is a model flag");
}

static rt_msg applied(uint32_t flags, int index, uint32_t tgen, uint32_t ogen) {
    struct app_applied d = {flags, index, tgen, ogen};
    rt_msg m = {.kind = NW_MSG_APPLIED};
    memcpy(m.v.bytes, &d, sizeof(d));
    return m;
}

/* present reports what it used; update clears only that request. */
static void test_applied(rt_cmds *c) {
    struct app_model a = model();
    step(&a, c, msg(NW_MSG_NEXT, 0, 0));
    step(&a, c, msg(NW_MSG_NEXT, 0, 0));
    step(&a, c, applied(APP_APPLIED_NEXT, 0, 0, 0));
    CHECK(a.next_pending == 1, "one next used, one left");

    step(&a, c, msg(NW_MSG_SET_INDEX, 0, 4));
    step(&a, c, msg(NW_MSG_SET_INDEX, 0, 9));       /* newer, before present saw 4 */
    step(&a, c, applied(APP_APPLIED_INDEX, 4, 0, 0));
    CHECK(a.set_index == 9, "a stale report doesn't clear a newer index");
    step(&a, c, applied(APP_APPLIED_INDEX, 9, 0, 0));
    CHECK(a.set_index == -1, "the matching one does");

    rt_msg t = {.kind = NW_MSG_SET_TERMINAL};
    t.v.p = strdup("a");
    step(&a, c, t);
    uint32_t first = a.terminal_gen;
    t.v.p = strdup("b");
    step(&a, c, t);
    step(&a, c, applied(APP_APPLIED_TERMINAL, 0, first, 0));
    CHECK(a.terminal_pending && !strcmp(a.terminal_cmd, "b"), "stale terminal report kept 'b'");
    step(&a, c, applied(APP_APPLIED_TERMINAL, 0, a.terminal_gen, 0));
    CHECK(!a.terminal_pending, "current one cleared");

    step(&a, c, msg(NW_MSG_OUTPUTS_NEW, 0, 0));
    uint32_t og = a.outputs_gen;
    step(&a, c, msg(NW_MSG_OUTPUTS_NEW, 0, 0));       /* another hotplug mid-frame */
    step(&a, c, applied(APP_APPLIED_OUTPUTS, 0, 0, og));
    CHECK(a.outputs_need_init, "a second hotplug isn't lost");
    CHECK(rt_cmds_len(c) == 0, "APPLIED has no effects");
}

/* The same message on the same model gives the same result. */
static void test_deterministic(rt_cmds *c) {
    const rt_msg seq[] = {
        msg(NW_MSG_NEXT, 0, 0), msg(NW_MSG_SET_INDEX, 0, 3), msg(NW_MSG_SHADER_PAUSE, 0, 0),
        msg(NW_MSG_CYCLE_PAUSE, 0, 0), msg(NW_MSG_NEXT, 0, 0), msg(NW_MSG_SHADER_RESUME, 0, 0),
    };
    struct app_model x = model(), y = model();
    size_t nx = 0, ny = 0;
    for (size_t i = 0; i < sizeof(seq) / sizeof(seq[0]); i++) {
        nx += rt_cmds_len(step(&x, c, seq[i]));
        ny += rt_cmds_len(step(&y, c, seq[i]));
    }
    CHECK(!memcmp(&x, &y, sizeof(x)) && nx == ny, "replaying a sequence gives the same model");
    CHECK(x.next_pending == 2 && x.set_index == 3 && x.cycle_paused && !x.shader_paused,
          "and the model it should");
}

int main(void) {
    log_set_level(LOG_LEVEL_ERROR);
    rt_cmds *c = rt_cmds_new();
    test_quit(c);
    test_gpu_lost(c);
    test_next(c);
    test_set_index(c);
    test_set_terminal(c);
    test_shader_pause(c);
    test_reload(c);
    test_timers_and_display(c);
    test_applied(c);
    test_deterministic(c);
    rt_cmds_free(c);
    if (failures) fprintf(stderr, "%d failure(s)\n", failures);
    else printf("app_update: all passed\n");
    return failures ? 1 : 0;
}
