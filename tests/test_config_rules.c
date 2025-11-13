/*
 * NeoWall Configuration Rules - Comprehensive Test Suite
 *
 * This test suite validates the entire configuration rules system,
 * ensuring that static wallpaper and shader settings are properly
 * enforced and validated.
 */

#include "config/config_rules.h"
#include "config/config_keys.h"
#include "neowall.h"
#include "output/output.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <pthread.h>

/* ============================================================================
 * TEST FRAMEWORK
 * ============================================================================ */

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define COLOR_RED     "\033[0;31m"
#define COLOR_GREEN   "\033[0;32m"
#define COLOR_YELLOW  "\033[0;33m"
#define COLOR_BLUE    "\033[0;34m"
#define COLOR_MAGENTA "\033[0;35m"
#define COLOR_CYAN    "\033[0;36m"
#define COLOR_RESET   "\033[0m"

#define TEST_START(name) \
    do { \
        printf("\n" COLOR_CYAN "TEST: %s" COLOR_RESET "\n", name); \
        tests_run++; \
    } while(0)

#define ASSERT(condition, message) \
    do { \
        if (!(condition)) { \
            printf(COLOR_RED "  ✗ FAILED: %s" COLOR_RESET "\n", message); \
            printf("    at %s:%d\n", __FILE__, __LINE__); \
            tests_failed++; \
            return false; \
        } else { \
            printf(COLOR_GREEN "  ✓ %s" COLOR_RESET "\n", message); \
        } \
    } while(0)

#define ASSERT_TRUE(condition, message) ASSERT(condition, message)
#define ASSERT_FALSE(condition, message) ASSERT(!(condition), message)
#define ASSERT_EQ(a, b, message) ASSERT((a) == (b), message)
#define ASSERT_STR_EQ(a, b, message) ASSERT(strcmp(a, b) == 0, message)
#define ASSERT_STR_CONTAINS(str, substr, message) ASSERT(strstr(str, substr) != NULL, message)

#define TEST_PASS() \
    do { \
        tests_passed++; \
        printf(COLOR_GREEN "  PASSED" COLOR_RESET "\n"); \
        return true; \
    } while(0)

/* ============================================================================
 * TEST HELPERS
 * ============================================================================ */

/* Create a test state with an output */
static struct neowall_state* create_test_state(void) {
    struct neowall_state *state = calloc(1, sizeof(struct neowall_state));
    pthread_rwlock_init(&state->output_list_lock, NULL);
    return state;
}

/* Add an output to the state */
static struct output_state* add_test_output(struct neowall_state *state, const char *name) {
    struct output_state *output = calloc(1, sizeof(struct output_state));
    strncpy(output->connector_name, name, sizeof(output->connector_name) - 1);

    pthread_rwlock_wrlock(&state->output_list_lock);
    output->next = state->outputs;
    state->outputs = output;
    pthread_rwlock_unlock(&state->output_list_lock);

    return output;
}

/* Set output as static wallpaper */
static void set_static_wallpaper(struct output_state *output, const char *path) {
    strncpy(output->config.path, path, sizeof(output->config.path) - 1);
    output->config.shader_path[0] = '\0';
}

/* Set output as shader wallpaper */
static void set_shader_wallpaper(struct output_state *output, const char *shader) {
    strncpy(output->config.shader_path, shader, sizeof(output->config.shader_path) - 1);
    output->config.path[0] = '\0';
}

/* Free test state */
static void free_test_state(struct neowall_state *state) {
    if (!state) return;

    pthread_rwlock_wrlock(&state->output_list_lock);
    struct output_state *output = state->outputs;
    while (output) {
        struct output_state *next = output->next;
        free(output);
        output = next;
    }
    pthread_rwlock_unlock(&state->output_list_lock);

    pthread_rwlock_destroy(&state->output_list_lock);
    free(state);
}

/* ============================================================================
 * TEST CASES: WALLPAPER TYPE DETECTION
 * ============================================================================ */

static bool test_type_detection_static(void) {
    TEST_START("Wallpaper Type Detection - Static");

    struct neowall_state *state = create_test_state();
    struct output_state *output = add_test_output(state, "DP-1");

    /* Initially undefined */
    wallpaper_type_t type = config_get_wallpaper_type(state, "DP-1");
    ASSERT_EQ(type, WALLPAPER_TYPE_ANY, "Initially type should be ANY");

    /* Set static wallpaper */
    set_static_wallpaper(output, "/home/user/wallpaper.png");

    /* Check type */
    type = config_get_wallpaper_type(state, "DP-1");
    ASSERT_EQ(type, WALLPAPER_TYPE_STATIC, "Type should be STATIC after setting path");

    free_test_state(state);
    TEST_PASS();
}

static bool test_type_detection_shader(void) {
    TEST_START("Wallpaper Type Detection - Shader");

    struct neowall_state *state = create_test_state();
    struct output_state *output = add_test_output(state, "HDMI-A-1");

    /* Set shader wallpaper */
    set_shader_wallpaper(output, "/home/user/shaders/plasma.glsl");

    /* Check type */
    wallpaper_type_t type = config_get_wallpaper_type(state, "HDMI-A-1");
    ASSERT_EQ(type, WALLPAPER_TYPE_SHADER, "Type should be SHADER after setting shader_path");

    free_test_state(state);
    TEST_PASS();
}

static bool test_type_detection_nonexistent(void) {
    TEST_START("Wallpaper Type Detection - Nonexistent Output");

    struct neowall_state *state = create_test_state();
    add_test_output(state, "DP-1");

    /* Check nonexistent output */
    wallpaper_type_t type = config_get_wallpaper_type(state, "DP-2");
    ASSERT_EQ(type, WALLPAPER_TYPE_ANY, "Nonexistent output should return ANY");

    free_test_state(state);
    TEST_PASS();
}

/* ============================================================================
 * TEST CASES: KEY APPLICABILITY
 * ============================================================================ */

static bool test_static_only_keys_applicability(void) {
    TEST_START("Static-Only Keys Applicability");

    const char *static_keys[] = {"mode", "transition", "transition_duration"};

    for (int i = 0; i < 3; i++) {
        const char *key = static_keys[i];

        /* Should be applicable to static wallpapers */
        bool applicable = config_is_key_applicable(key, WALLPAPER_TYPE_STATIC);
        char msg[256];
        snprintf(msg, sizeof(msg), "'%s' should be applicable to STATIC", key);
        ASSERT_TRUE(applicable, msg);

        /* Should NOT be applicable to shader wallpapers */
        applicable = config_is_key_applicable(key, WALLPAPER_TYPE_SHADER);
        snprintf(msg, sizeof(msg), "'%s' should NOT be applicable to SHADER", key);
        ASSERT_FALSE(applicable, msg);

        /* Should be applicable to ANY (pre-determination) */
        applicable = config_is_key_applicable(key, WALLPAPER_TYPE_ANY);
        snprintf(msg, sizeof(msg), "'%s' should be applicable to ANY", key);
        ASSERT_TRUE(applicable, msg);
    }

    TEST_PASS();
}

static bool test_shader_only_keys_applicability(void) {
    TEST_START("Shader-Only Keys Applicability");

    const char *shader_keys[] = {
        "shader_speed", "shader_fps", "vsync", "channels", "show_fps"
    };

    for (int i = 0; i < 5; i++) {
        const char *key = shader_keys[i];

        /* Should be applicable to shader wallpapers */
        bool applicable = config_is_key_applicable(key, WALLPAPER_TYPE_SHADER);
        char msg[256];
        snprintf(msg, sizeof(msg), "'%s' should be applicable to SHADER", key);
        ASSERT_TRUE(applicable, msg);

        /* Should NOT be applicable to static wallpapers */
        applicable = config_is_key_applicable(key, WALLPAPER_TYPE_STATIC);
        snprintf(msg, sizeof(msg), "'%s' should NOT be applicable to STATIC", key);
        ASSERT_FALSE(applicable, msg);

        /* Should be applicable to ANY (pre-determination) */
        applicable = config_is_key_applicable(key, WALLPAPER_TYPE_ANY);
        snprintf(msg, sizeof(msg), "'%s' should be applicable to ANY", key);
        ASSERT_TRUE(applicable, msg);
    }

    TEST_PASS();
}

static bool test_universal_keys_applicability(void) {
    TEST_START("Universal Keys Applicability");

    const char *key = "duration";

    /* Should be applicable to static wallpapers */
    bool applicable = config_is_key_applicable(key, WALLPAPER_TYPE_STATIC);
    ASSERT_TRUE(applicable, "'duration' should work with STATIC (directory cycling)");

    /* Should be applicable to shader wallpapers */
    applicable = config_is_key_applicable(key, WALLPAPER_TYPE_SHADER);
    ASSERT_TRUE(applicable, "'duration' should work with SHADER (directory cycling)");

    /* Should be applicable to ANY */
    applicable = config_is_key_applicable(key, WALLPAPER_TYPE_ANY);
    ASSERT_TRUE(applicable, "'duration' should work with ANY");

    TEST_PASS();
}

static bool test_key_with_section_prefix(void) {
    TEST_START("Key Applicability with Section Prefix");

    /* Test that keys with section prefix work */
    bool applicable = config_is_key_applicable("wallpaper.mode", WALLPAPER_TYPE_STATIC);
    ASSERT_TRUE(applicable, "'wallpaper.mode' should be applicable to STATIC");

    applicable = config_is_key_applicable("wallpaper.shader_speed", WALLPAPER_TYPE_SHADER);
    ASSERT_TRUE(applicable, "'wallpaper.shader_speed' should be applicable to SHADER");

    TEST_PASS();
}

/* ============================================================================
 * TEST CASES: VALIDATION RULES
 * ============================================================================ */

static bool test_validation_static_with_static_keys(void) {
    TEST_START("Validation: Static wallpaper with static-only keys");

    struct neowall_state *state = create_test_state();
    struct output_state *output = add_test_output(state, "DP-1");
    set_static_wallpaper(output, "/home/user/wallpaper.png");

    char error[512];

    /* These should all succeed */
    const char *valid_keys[][2] = {
        {"mode", "fill"},
        {"transition", "fade"},
        {"transition_duration", "500"}
    };

    for (int i = 0; i < 3; i++) {
        bool valid = config_validate_rules(state, "DP-1",
                                           valid_keys[i][0],
                                           valid_keys[i][1],
                                           error, sizeof(error));
        char msg[256];
        snprintf(msg, sizeof(msg), "'%s' should be valid for static wallpaper", valid_keys[i][0]);
        ASSERT_TRUE(valid, msg);
    }

    free_test_state(state);
    TEST_PASS();
}

static bool test_validation_shader_with_shader_keys(void) {
    TEST_START("Validation: Shader wallpaper with shader-only keys");

    struct neowall_state *state = create_test_state();
    struct output_state *output = add_test_output(state, "DP-1");
    set_shader_wallpaper(output, "/home/user/shaders/plasma.glsl");

    char error[512];

    /* These should all succeed */
    const char *valid_keys[][2] = {
        {"shader_speed", "2.0"},
        {"shader_fps", "60"},
        {"vsync", "true"},
        {"show_fps", "true"}
    };

    for (int i = 0; i < 4; i++) {
        bool valid = config_validate_rules(state, "DP-1",
                                           valid_keys[i][0],
                                           valid_keys[i][1],
                                           error, sizeof(error));
        char msg[256];
        snprintf(msg, sizeof(msg), "'%s' should be valid for shader wallpaper", valid_keys[i][0]);
        ASSERT_TRUE(valid, msg);
    }

    free_test_state(state);
    TEST_PASS();
}

static bool test_validation_static_rejects_shader_keys(void) {
    TEST_START("Validation: Static wallpaper rejects shader-only keys");

    struct neowall_state *state = create_test_state();
    struct output_state *output = add_test_output(state, "DP-1");
    set_static_wallpaper(output, "/home/user/wallpaper.png");

    char error[512];

    /* These should all fail */
    const char *invalid_keys[] = {
        "shader_speed", "shader_fps", "vsync", "show_fps", "channels"
    };

    for (int i = 0; i < 5; i++) {
        bool valid = config_validate_rules(state, "DP-1",
                                           invalid_keys[i],
                                           "test_value",
                                           error, sizeof(error));
        char msg[256];
        snprintf(msg, sizeof(msg), "'%s' should be INVALID for static wallpaper", invalid_keys[i]);
        ASSERT_FALSE(valid, msg);

        /* Check error message is helpful */
        snprintf(msg, sizeof(msg), "Error message for '%s' should mention 'shader'", invalid_keys[i]);
        ASSERT_STR_CONTAINS(error, "shader", msg);
    }

    free_test_state(state);
    TEST_PASS();
}

static bool test_validation_shader_rejects_static_keys(void) {
    TEST_START("Validation: Shader wallpaper rejects static-only keys");

    struct neowall_state *state = create_test_state();
    struct output_state *output = add_test_output(state, "DP-1");
    set_shader_wallpaper(output, "/home/user/shaders/matrix.glsl");

    char error[512];

    /* These should all fail */
    const char *invalid_keys[] = {
        "mode", "transition", "transition_duration"
    };

    for (int i = 0; i < 3; i++) {
        bool valid = config_validate_rules(state, "DP-1",
                                           invalid_keys[i],
                                           "test_value",
                                           error, sizeof(error));
        char msg[256];
        snprintf(msg, sizeof(msg), "'%s' should be INVALID for shader wallpaper", invalid_keys[i]);
        ASSERT_FALSE(valid, msg);

        /* Check error message is helpful */
        snprintf(msg, sizeof(msg), "Error message for '%s' should mention 'static'", invalid_keys[i]);
        ASSERT_STR_CONTAINS(error, "static", msg);
    }

    free_test_state(state);
    TEST_PASS();
}

static bool test_validation_universal_keys_both_types(void) {
    TEST_START("Validation: Universal keys work with both types");

    struct neowall_state *state = create_test_state();

    /* Test with static */
    struct output_state *output1 = add_test_output(state, "DP-1");
    set_static_wallpaper(output1, "/home/user/Pictures/");

    char error[512];
    bool valid = config_validate_rules(state, "DP-1", "duration", "300", error, sizeof(error));
    ASSERT_TRUE(valid, "'duration' should work with static (for directory cycling)");

    /* Test with shader */
    struct output_state *output2 = add_test_output(state, "DP-2");
    set_shader_wallpaper(output2, "/home/user/shaders/");

    valid = config_validate_rules(state, "DP-2", "duration", "60", error, sizeof(error));
    ASSERT_TRUE(valid, "'duration' should work with shader (for directory cycling)");

    free_test_state(state);
    TEST_PASS();
}

static bool test_validation_path_shader_mutual_exclusion(void) {
    TEST_START("Validation: path and shader are mutually exclusive");

    struct neowall_state *state = create_test_state();
    struct output_state *output = add_test_output(state, "DP-1");

    char error[512];

    /* Set path first */
    set_static_wallpaper(output, "/home/user/wallpaper.png");

    /* Try to set shader - should fail */
    bool valid = config_validate_rules(state, "DP-1", "shader", "plasma.glsl", error, sizeof(error));
    ASSERT_FALSE(valid, "Setting 'shader' after 'path' should fail");
    /* Note: The error message says "Cannot use both" */
    ASSERT_STR_CONTAINS(error, "both", "Error should mention using both");

    /* Now reverse - set shader first */
    struct output_state *output2 = add_test_output(state, "DP-2");
    set_shader_wallpaper(output2, "/home/user/shaders/matrix.glsl");

    /* Try to set path - should fail */
    valid = config_validate_rules(state, "DP-2", "path", "wallpaper.png", error, sizeof(error));
    ASSERT_FALSE(valid, "Setting 'path' after 'shader' should fail");
    /* Note: The error message says "Cannot use both" */
    ASSERT_STR_CONTAINS(error, "both", "Error should mention using both");

    free_test_state(state);
    TEST_PASS();
}

static bool test_validation_undefined_type_allows_all(void) {
    TEST_START("Validation: Undefined wallpaper type allows all keys");

    struct neowall_state *state = create_test_state();
    add_test_output(state, "DP-1"); /* No path or shader set */

    char error[512];

    /* Before type is determined, all keys should be allowed */
    bool valid = config_validate_rules(state, "DP-1", "mode", "fill", error, sizeof(error));
    ASSERT_TRUE(valid, "Static key should be allowed when type is undefined");

    valid = config_validate_rules(state, "DP-1", "shader_speed", "2.0", error, sizeof(error));
    ASSERT_TRUE(valid, "Shader key should be allowed when type is undefined");

    free_test_state(state);
    TEST_PASS();
}

/* ============================================================================
 * TEST CASES: APPLICABLE KEYS LISTING
 * ============================================================================ */

static bool test_get_applicable_keys_static(void) {
    TEST_START("Get Applicable Keys - Static");

    const char *keys[32];
    size_t count = config_get_applicable_keys(WALLPAPER_TYPE_STATIC, keys, 32);

    ASSERT_TRUE(count > 0, "Should return some keys for static");

    /* Check that static-only keys are included */
    bool has_mode = false, has_transition = false;
    for (size_t i = 0; i < count; i++) {
        if (strcmp(keys[i], "mode") == 0) has_mode = true;
        if (strcmp(keys[i], "transition") == 0) has_transition = true;
    }

    ASSERT_TRUE(has_mode, "Static keys should include 'mode'");
    ASSERT_TRUE(has_transition, "Static keys should include 'transition'");

    /* Check that shader-only keys are NOT included */
    for (size_t i = 0; i < count; i++) {
        ASSERT_TRUE(strcmp(keys[i], "shader_speed") != 0, "Static keys should NOT include 'shader_speed'");
        ASSERT_TRUE(strcmp(keys[i], "show_fps") != 0, "Static keys should NOT include 'show_fps'");
    }

    TEST_PASS();
}

static bool test_get_applicable_keys_shader(void) {
    TEST_START("Get Applicable Keys - Shader");

    const char *keys[32];
    size_t count = config_get_applicable_keys(WALLPAPER_TYPE_SHADER, keys, 32);

    ASSERT_TRUE(count > 0, "Should return some keys for shader");

    /* Check that shader-only keys are included */
    bool has_speed = false, has_fps = false, has_show_fps = false;
    for (size_t i = 0; i < count; i++) {
        if (strcmp(keys[i], "shader_speed") == 0) has_speed = true;
        if (strcmp(keys[i], "shader_fps") == 0) has_fps = true;
        if (strcmp(keys[i], "show_fps") == 0) has_show_fps = true;
    }

    ASSERT_TRUE(has_speed, "Shader keys should include 'shader_speed'");
    ASSERT_TRUE(has_fps, "Shader keys should include 'shader_fps'");
    ASSERT_TRUE(has_show_fps, "Shader keys should include 'show_fps'");

    /* Check that static-only keys are NOT included */
    for (size_t i = 0; i < count; i++) {
        ASSERT_TRUE(strcmp(keys[i], "mode") != 0, "Shader keys should NOT include 'mode'");
        ASSERT_TRUE(strcmp(keys[i], "transition") != 0, "Shader keys should NOT include 'transition'");
    }

    TEST_PASS();
}

static bool test_get_applicable_keys_universal_in_both(void) {
    TEST_START("Get Applicable Keys - Universal in both");

    const char *static_keys[32];
    size_t static_count = config_get_applicable_keys(WALLPAPER_TYPE_STATIC, static_keys, 32);

    const char *shader_keys[32];
    size_t shader_count = config_get_applicable_keys(WALLPAPER_TYPE_SHADER, shader_keys, 32);

    /* 'duration' should be in both lists */
    bool static_has_duration = false;
    for (size_t i = 0; i < static_count; i++) {
        if (strcmp(static_keys[i], "duration") == 0) static_has_duration = true;
    }

    bool shader_has_duration = false;
    for (size_t i = 0; i < shader_count; i++) {
        if (strcmp(shader_keys[i], "duration") == 0) shader_has_duration = true;
    }

    ASSERT_TRUE(static_has_duration, "'duration' should be in static keys list");
    ASSERT_TRUE(shader_has_duration, "'duration' should be in shader keys list");

    TEST_PASS();
}

/* ============================================================================
 * TEST CASES: EXPLANATION/DOCUMENTATION
 * ============================================================================ */

static bool test_explain_static_key(void) {
    TEST_START("Explain Key Rules - Static Key");

    char explanation[512];
    config_explain_key_rules("mode", explanation, sizeof(explanation));

    ASSERT_TRUE(strlen(explanation) > 0, "Explanation should not be empty");
    ASSERT_STR_CONTAINS(explanation, "static", "Explanation should mention 'static'");

    TEST_PASS();
}

static bool test_explain_shader_key(void) {
    TEST_START("Explain Key Rules - Shader Key");

    char explanation[512];
    config_explain_key_rules("shader_speed", explanation, sizeof(explanation));

    ASSERT_TRUE(strlen(explanation) > 0, "Explanation should not be empty");
    ASSERT_STR_CONTAINS(explanation, "shader", "Explanation should mention 'shader'");

    TEST_PASS();
}

static bool test_explain_universal_key(void) {
    TEST_START("Explain Key Rules - Universal Key");

    char explanation[512];
    config_explain_key_rules("duration", explanation, sizeof(explanation));

    ASSERT_TRUE(strlen(explanation) > 0, "Explanation should not be empty");

    TEST_PASS();
}

/* ============================================================================
 * TEST CASES: EDGE CASES
 * ============================================================================ */

static bool test_null_safety(void) {
    TEST_START("NULL Safety Checks");

    char error[512];

    /* NULL state - implementation returns true for NULL state */
    bool valid = config_validate_rules(NULL, "DP-1", "mode", "fill", error, sizeof(error));
    ASSERT_TRUE(valid, "NULL state should allow validation (returns true)");

    /* NULL key - implementation returns true for NULL key (nothing to validate) */
    struct neowall_state *state = create_test_state();
    valid = config_validate_rules(state, "DP-1", NULL, "value", error, sizeof(error));
    ASSERT_TRUE(valid, "NULL key returns true (nothing to validate)");

    /* NULL value is okay - treated as clearing/empty */
    valid = config_validate_rules(state, "DP-1", "mode", NULL, error, sizeof(error));
    ASSERT_TRUE(valid, "NULL value should be allowed (treated as empty)");

    free_test_state(state);
    TEST_PASS();
}

static bool test_empty_strings(void) {
    TEST_START("Empty String Handling");

    struct neowall_state *state = create_test_state();
    struct output_state *output = add_test_output(state, "");

    /* Empty output name */
    wallpaper_type_t type = config_get_wallpaper_type(state, "");
    /* Should handle gracefully - returns ANY or matches empty connector */
    (void)type; /* Suppress warning */
    (void)output; /* Suppress warning */

    /* Empty key - implementation checks IS_UNIVERSAL_KEY which may return true for empty */
    bool applicable = config_is_key_applicable("", WALLPAPER_TYPE_STATIC);
    /* The actual implementation behavior: empty string may be considered universal */
    /* Just verify it doesn't crash */
    (void)applicable;

    free_test_state(state);
    TEST_PASS();
}

static bool test_multiple_outputs_independence(void) {
    TEST_START("Multiple Outputs Independence");

    struct neowall_state *state = create_test_state();

    /* Output 1: static */
    struct output_state *output1 = add_test_output(state, "DP-1");
    set_static_wallpaper(output1, "/home/user/wallpaper.png");

    /* Output 2: shader */
    struct output_state *output2 = add_test_output(state, "DP-2");
    set_shader_wallpaper(output2, "/home/user/shaders/plasma.glsl");

    /* Verify types are correct */
    wallpaper_type_t type1 = config_get_wallpaper_type(state, "DP-1");
    wallpaper_type_t type2 = config_get_wallpaper_type(state, "DP-2");

    ASSERT_EQ(type1, WALLPAPER_TYPE_STATIC, "DP-1 should be STATIC");
    ASSERT_EQ(type2, WALLPAPER_TYPE_SHADER, "DP-2 should be SHADER");

    /* Validate correct keys for each */
    char error[512];

    bool valid = config_validate_rules(state, "DP-1", "mode", "fill", error, sizeof(error));
    ASSERT_TRUE(valid, "Static key should work on DP-1");

    valid = config_validate_rules(state, "DP-2", "shader_speed", "2.0", error, sizeof(error));
    ASSERT_TRUE(valid, "Shader key should work on DP-2");

    /* Validate incorrect keys fail */
    valid = config_validate_rules(state, "DP-1", "shader_speed", "2.0", error, sizeof(error));
    ASSERT_FALSE(valid, "Shader key should fail on static DP-1");

    valid = config_validate_rules(state, "DP-2", "mode", "fill", error, sizeof(error));
    ASSERT_FALSE(valid, "Static key should fail on shader DP-2");

    free_test_state(state);
    TEST_PASS();
}

/* ============================================================================
 * TEST CASES: REAL-WORLD SCENARIOS
 * ============================================================================ */

static bool test_scenario_image_with_transition(void) {
    TEST_START("Scenario: Static image with transition");

    struct neowall_state *state = create_test_state();
    struct output_state *output = add_test_output(state, "DP-1");

    char error[512];

    /* Set path */
    set_static_wallpaper(output, "/home/user/Pictures/mountain.jpg");

    /* Apply transition settings */
    bool valid = config_validate_rules(state, "DP-1", "transition", "fade", error, sizeof(error));
    ASSERT_TRUE(valid, "Should allow transition on static wallpaper");

    valid = config_validate_rules(state, "DP-1", "transition_duration", "500", error, sizeof(error));
    ASSERT_TRUE(valid, "Should allow transition_duration on static wallpaper");

    valid = config_validate_rules(state, "DP-1", "mode", "fill", error, sizeof(error));
    ASSERT_TRUE(valid, "Should allow mode on static wallpaper");

    free_test_state(state);
    TEST_PASS();
}

static bool test_scenario_shader_with_speed_and_fps(void) {
    TEST_START("Scenario: Shader with speed and FPS settings");

    struct neowall_state *state = create_test_state();
    struct output_state *output = add_test_output(state, "HDMI-A-1");

    char error[512];

    /* Set shader */
    set_shader_wallpaper(output, "/home/user/.config/neowall/shaders/matrix.glsl");

    /* Apply shader settings */
    bool valid = config_validate_rules(state, "HDMI-A-1", "shader_speed", "1.5", error, sizeof(error));
    ASSERT_TRUE(valid, "Should allow shader_speed on shader wallpaper");

    valid = config_validate_rules(state, "HDMI-A-1", "shader_fps", "60", error, sizeof(error));
    ASSERT_TRUE(valid, "Should allow shader_fps on shader wallpaper");

    valid = config_validate_rules(state, "HDMI-A-1", "vsync", "true", error, sizeof(error));
    ASSERT_TRUE(valid, "Should allow vsync on shader wallpaper");

    valid = config_validate_rules(state, "HDMI-A-1", "show_fps", "true", error, sizeof(error));
    ASSERT_TRUE(valid, "Should allow show_fps on shader wallpaper");

    free_test_state(state);
    TEST_PASS();
}

static bool test_scenario_directory_cycling_images(void) {
    TEST_START("Scenario: Directory cycling with images");

    struct neowall_state *state = create_test_state();
    struct output_state *output = add_test_output(state, "DP-1");

    char error[512];

    /* Set directory path */
    set_static_wallpaper(output, "/home/user/Pictures/wallpapers/");

    /* Apply duration for cycling */
    bool valid = config_validate_rules(state, "DP-1", "duration", "300", error, sizeof(error));
    ASSERT_TRUE(valid, "Should allow duration for directory of images");

    /* Can still use transition */
    valid = config_validate_rules(state, "DP-1", "transition", "fade", error, sizeof(error));
    ASSERT_TRUE(valid, "Should allow transition with image directory");

    free_test_state(state);
    TEST_PASS();
}

static bool test_scenario_directory_cycling_shaders(void) {
    TEST_START("Scenario: Directory cycling with shaders");

    struct neowall_state *state = create_test_state();
    struct output_state *output = add_test_output(state, "DP-1");

    char error[512];

    /* Set directory of shaders */
    set_shader_wallpaper(output, "/home/user/.config/neowall/shaders/");

    /* Apply duration for cycling */
    bool valid = config_validate_rules(state, "DP-1", "duration", "60", error, sizeof(error));
    ASSERT_TRUE(valid, "Should allow duration for directory of shaders");

    /* Can use shader settings */
    valid = config_validate_rules(state, "DP-1", "shader_speed", "2.0", error, sizeof(error));
    ASSERT_TRUE(valid, "Should allow shader_speed with shader directory");

    /* Cannot use transition */
    valid = config_validate_rules(state, "DP-1", "transition", "fade", error, sizeof(error));
    ASSERT_FALSE(valid, "Should NOT allow transition with shader directory");

    free_test_state(state);
    TEST_PASS();
}

static bool test_scenario_show_fps_only_on_shaders(void) {
    TEST_START("Scenario: show_fps only works with shaders");

    struct neowall_state *state = create_test_state();
    char error[512];

    /* Try on static wallpaper */
    struct output_state *output1 = add_test_output(state, "DP-1");
    set_static_wallpaper(output1, "/home/user/wallpaper.png");

    bool valid = config_validate_rules(state, "DP-1", "show_fps", "true", error, sizeof(error));
    ASSERT_FALSE(valid, "show_fps should NOT work with static wallpaper");
    ASSERT_STR_CONTAINS(error, "shader", "Error should explain it's for shaders");

    /* Try on shader wallpaper */
    struct output_state *output2 = add_test_output(state, "DP-2");
    set_shader_wallpaper(output2, "/home/user/shaders/plasma.glsl");

    valid = config_validate_rules(state, "DP-2", "show_fps", "true", error, sizeof(error));
    ASSERT_TRUE(valid, "show_fps SHOULD work with shader wallpaper");

    free_test_state(state);
    TEST_PASS();
}

/* ============================================================================
 * TEST RUNNER
 * ============================================================================ */

typedef bool (*test_func_t)(void);

typedef struct {
    const char *name;
    test_func_t func;
} test_case_t;

static const test_case_t all_tests[] = {
    /* Type detection */
    {"Type Detection - Static", test_type_detection_static},
    {"Type Detection - Shader", test_type_detection_shader},
    {"Type Detection - Nonexistent", test_type_detection_nonexistent},

    /* Key applicability */
    {"Static-Only Keys", test_static_only_keys_applicability},
    {"Shader-Only Keys", test_shader_only_keys_applicability},
    {"Universal Keys", test_universal_keys_applicability},
    {"Keys with Section Prefix", test_key_with_section_prefix},

    /* Validation rules */
    {"Validation: Static with static keys", test_validation_static_with_static_keys},
    {"Validation: Shader with shader keys", test_validation_shader_with_shader_keys},
    {"Validation: Static rejects shader keys", test_validation_static_rejects_shader_keys},
    {"Validation: Shader rejects static keys", test_validation_shader_rejects_static_keys},
    {"Validation: Universal keys both types", test_validation_universal_keys_both_types},
    {"Validation: Mutual exclusion", test_validation_path_shader_mutual_exclusion},
    {"Validation: Undefined allows all", test_validation_undefined_type_allows_all},

    /* Applicable keys listing */
    {"Get Applicable Keys - Static", test_get_applicable_keys_static},
    {"Get Applicable Keys - Shader", test_get_applicable_keys_shader},
    {"Get Applicable Keys - Universal", test_get_applicable_keys_universal_in_both},

    /* Documentation */
    {"Explain Static Key", test_explain_static_key},
    {"Explain Shader Key", test_explain_shader_key},
    {"Explain Universal Key", test_explain_universal_key},

    /* Edge cases */
    {"NULL Safety", test_null_safety},
    {"Empty Strings", test_empty_strings},
    {"Multiple Outputs", test_multiple_outputs_independence},

    /* Real-world scenarios */
    {"Scenario: Image with transition", test_scenario_image_with_transition},
    {"Scenario: Shader with settings", test_scenario_shader_with_speed_and_fps},
    {"Scenario: Directory cycling images", test_scenario_directory_cycling_images},
    {"Scenario: Directory cycling shaders", test_scenario_directory_cycling_shaders},
    {"Scenario: show_fps shader-only", test_scenario_show_fps_only_on_shaders},
};

static void print_summary(void) {
    printf("\n");
    printf("╔════════════════════════════════════════════════════════════════╗\n");
    printf("║                        TEST SUMMARY                            ║\n");
    printf("╚════════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    printf("  Total Tests:  %d\n", tests_run);
    printf("  " COLOR_GREEN "Passed:       %d" COLOR_RESET "\n", tests_passed);
    printf("  " COLOR_RED "Failed:       %d" COLOR_RESET "\n", tests_failed);
    printf("\n");

    if (tests_failed == 0) {
        printf(COLOR_GREEN "  ✓ ALL TESTS PASSED!" COLOR_RESET "\n\n");
    } else {
        printf(COLOR_RED "  ✗ SOME TESTS FAILED" COLOR_RESET "\n\n");
    }
}

int main(int argc __attribute__((unused)), char **argv __attribute__((unused))) {
    printf("\n");
    printf("╔════════════════════════════════════════════════════════════════╗\n");
    printf("║     NEOWALL CONFIG RULES - COMPREHENSIVE TEST SUITE            ║\n");
    printf("╚════════════════════════════════════════════════════════════════╝\n");

    const size_t num_tests = sizeof(all_tests) / sizeof(all_tests[0]);

    for (size_t i = 0; i < num_tests; i++) {
        all_tests[i].func();
    }

    print_summary();

    return tests_failed == 0 ? 0 : 1;
}
