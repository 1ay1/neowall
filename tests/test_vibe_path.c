/*
 * Test program for VIBE path operations
 * Tests get/set/delete operations with dot-notation paths
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include "../src/neowalld/config/vibe.h"
#include "../src/neowalld/config/vibe_path.h"

/* Colors for output */
#define GREEN "\033[32m"
#define RED "\033[31m"
#define YELLOW "\033[33m"
#define RESET "\033[0m"

/* Test counters */
static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

/* Test assertion macro */
#define TEST_ASSERT(condition, message) do { \
    tests_run++; \
    if (condition) { \
        tests_passed++; \
        printf(GREEN "  ✓ " RESET "%s\n", message); \
    } else { \
        tests_failed++; \
        printf(RED "  ✗ " RESET "%s\n", message); \
    } \
} while(0)

/* Test section header */
#define TEST_SECTION(name) \
    printf("\n" YELLOW "━━━ %s ━━━" RESET "\n", name)

/* ============================================================================
 * Test Functions
 * ============================================================================ */

void test_vibe_path_get() {
    TEST_SECTION("Test vibe_path_get()");

    /* Create test object */
    VibeValue *root = vibe_value_new_object();
    VibeValue *default_obj = vibe_value_new_object();
    vibe_object_set(root->as_object, "default", default_obj);
    vibe_object_set(default_obj->as_object, "shader", vibe_value_new_string("plasma.glsl"));
    vibe_object_set(default_obj->as_object, "shader_speed", vibe_value_new_float(2.0));
    vibe_object_set(default_obj->as_object, "duration", vibe_value_new_integer(300));

    /* Test get string */
    const char *shader = vibe_path_get_string(root, "default.shader");
    TEST_ASSERT(shader != NULL && strcmp(shader, "plasma.glsl") == 0,
                "Get string value: default.shader");

    /* Test get float */
    double speed;
    bool got_speed = vibe_path_get_float(root, "default.shader_speed", &speed);
    TEST_ASSERT(got_speed && speed == 2.0,
                "Get float value: default.shader_speed");

    /* Test get integer */
    int64_t duration;
    bool got_duration = vibe_path_get_int(root, "default.duration", &duration);
    TEST_ASSERT(got_duration && duration == 300,
                "Get integer value: default.duration");

    /* Test non-existent path */
    const char *nonexistent = vibe_path_get_string(root, "default.nonexistent");
    TEST_ASSERT(nonexistent == NULL,
                "Get non-existent key returns NULL");

    /* Test path exists */
    TEST_ASSERT(vibe_path_exists(root, "default.shader"),
                "Path exists: default.shader");
    TEST_ASSERT(!vibe_path_exists(root, "default.nonexistent"),
                "Path doesn't exist: default.nonexistent");

    vibe_value_free(root);
}

void test_vibe_path_set() {
    TEST_SECTION("Test vibe_path_set()");

    VibeValue *root = vibe_value_new_object();

    /* Test set string (creates nested objects) */
    bool set_ok = vibe_path_set_string(root, "default.shader", "matrix.glsl");
    TEST_ASSERT(set_ok, "Set string value: default.shader");

    const char *shader = vibe_path_get_string(root, "default.shader");
    TEST_ASSERT(shader != NULL && strcmp(shader, "matrix.glsl") == 0,
                "Verify set string value");

    /* Test set float */
    set_ok = vibe_path_set_float(root, "default.shader_speed", 3.5);
    TEST_ASSERT(set_ok, "Set float value: default.shader_speed");

    double speed;
    vibe_path_get_float(root, "default.shader_speed", &speed);
    TEST_ASSERT(speed == 3.5, "Verify set float value");

    /* Test set integer */
    set_ok = vibe_path_set_int(root, "default.duration", 600);
    TEST_ASSERT(set_ok, "Set integer value: default.duration");

    int64_t duration;
    vibe_path_get_int(root, "default.duration", &duration);
    TEST_ASSERT(duration == 600, "Verify set integer value");

    /* Test set boolean */
    set_ok = vibe_path_set_bool(root, "default.vsync", true);
    TEST_ASSERT(set_ok, "Set boolean value: default.vsync");

    bool vsync;
    vibe_path_get_bool(root, "default.vsync", &vsync);
    TEST_ASSERT(vsync == true, "Verify set boolean value");

    /* Test overwrite existing value */
    vibe_path_set_string(root, "default.shader", "plasma.glsl");
    shader = vibe_path_get_string(root, "default.shader");
    TEST_ASSERT(strcmp(shader, "plasma.glsl") == 0,
                "Overwrite existing value");

    /* Test nested output path */
    vibe_path_set_string(root, "output.DP-1.mode", "fill");
    const char *mode = vibe_path_get_string(root, "output.DP-1.mode");
    TEST_ASSERT(mode != NULL && strcmp(mode, "fill") == 0,
                "Set nested output value: output.DP-1.mode");

    vibe_value_free(root);
}

void test_vibe_path_delete() {
    TEST_SECTION("Test vibe_path_delete()");

    VibeValue *root = vibe_value_new_object();

    /* Setup test data */
    vibe_path_set_string(root, "default.shader", "matrix.glsl");
    vibe_path_set_float(root, "default.shader_speed", 2.0);
    vibe_path_set_string(root, "output.DP-1.mode", "fill");

    /* Test delete single key */
    bool deleted = vibe_path_delete(root, "default.shader_speed");
    TEST_ASSERT(deleted, "Delete existing key: default.shader_speed");
    TEST_ASSERT(!vibe_path_exists(root, "default.shader_speed"),
                "Verify key was deleted");

    /* Test delete non-existent key */
    deleted = vibe_path_delete(root, "default.nonexistent");
    TEST_ASSERT(!deleted, "Delete non-existent key returns false");

    /* Test other keys still exist */
    TEST_ASSERT(vibe_path_exists(root, "default.shader"),
                "Other keys in same section still exist");
    TEST_ASSERT(vibe_path_exists(root, "output.DP-1.mode"),
                "Other sections still exist");

    vibe_value_free(root);
}

void test_vibe_path_list_keys() {
    TEST_SECTION("Test vibe_path_list_keys()");

    VibeValue *root = vibe_value_new_object();

    /* Setup test data */
    vibe_path_set_string(root, "default.shader", "matrix.glsl");
    vibe_path_set_float(root, "default.shader_speed", 2.0);
    vibe_path_set_int(root, "default.duration", 300);
    vibe_path_set_string(root, "output.DP-1.mode", "fill");

    /* List keys at default level */
    const char *keys[10];
    size_t count = vibe_path_list_keys(root, "default", keys, 10);
    TEST_ASSERT(count == 3, "List keys in 'default' section (count)");

    /* List all keys recursively */
    const char *all_keys[100];
    size_t all_count = vibe_path_list_all_keys(root, all_keys, 100);
    TEST_ASSERT(all_count == 4, "List all keys recursively (count)");

    /* Free duplicated strings */
    for (size_t i = 0; i < all_count; i++) {
        free((void*)all_keys[i]);
    }

    vibe_value_free(root);
}

void test_vibe_path_validate() {
    TEST_SECTION("Test vibe_path_validate()");

    TEST_ASSERT(vibe_path_validate("default.shader"),
                "Valid path: default.shader");
    TEST_ASSERT(vibe_path_validate("output.DP-1.mode"),
                "Valid path with dash: output.DP-1.mode");
    TEST_ASSERT(vibe_path_validate("output.HDMI-A-1.shader_speed"),
                "Valid path with multiple dashes: output.HDMI-A-1.shader_speed");

    TEST_ASSERT(!vibe_path_validate(""),
                "Invalid: empty path");
    TEST_ASSERT(!vibe_path_validate(".default"),
                "Invalid: leading dot");
    TEST_ASSERT(!vibe_path_validate("default."),
                "Invalid: trailing dot");
    TEST_ASSERT(!vibe_path_validate("default..shader"),
                "Invalid: double dots");
    TEST_ASSERT(!vibe_path_validate("default.sh@der"),
                "Invalid: special characters");
}

void test_vibe_path_split() {
    TEST_SECTION("Test vibe_path_split()");

    char parent[256];
    char key[256];

    /* Test nested path */
    bool ok = vibe_path_split("default.shader", parent, sizeof(parent), key, sizeof(key));
    TEST_ASSERT(ok && strcmp(parent, "default") == 0 && strcmp(key, "shader") == 0,
                "Split: default.shader");

    /* Test deep nested path */
    ok = vibe_path_split("output.DP-1.mode", parent, sizeof(parent), key, sizeof(key));
    TEST_ASSERT(ok && strcmp(parent, "output.DP-1") == 0 && strcmp(key, "mode") == 0,
                "Split: output.DP-1.mode");

    /* Test root level */
    ok = vibe_path_split("default", parent, sizeof(parent), key, sizeof(key));
    TEST_ASSERT(ok && strlen(parent) == 0 && strcmp(key, "default") == 0,
                "Split root level: default");
}

void test_vibe_path_file_io() {
    TEST_SECTION("Test vibe_path_write_file()");

    const char *test_file = "/tmp/neowall-test-vibe-path.vibe";

    /* Create test object */
    VibeValue *root = vibe_value_new_object();
    vibe_path_set_string(root, "default.shader", "matrix.glsl");
    vibe_path_set_float(root, "default.shader_speed", 2.5);
    vibe_path_set_int(root, "default.duration", 300);
    vibe_path_set_string(root, "output.DP-1.mode", "fill");

    /* Write to file */
    bool written = vibe_path_write_file(root, test_file);
    TEST_ASSERT(written, "Write config to file");

    vibe_value_free(root);

    /* Read back from file */
    VibeParser *parser = vibe_parser_new();
    VibeValue *loaded = vibe_parse_file(parser, test_file);
    vibe_parser_free(parser);

    TEST_ASSERT(loaded != NULL, "Read config from file");

    /* Verify values */
    const char *shader = vibe_path_get_string(loaded, "default.shader");
    TEST_ASSERT(shader != NULL && strcmp(shader, "matrix.glsl") == 0,
                "Verify loaded value: default.shader");

    double speed;
    vibe_path_get_float(loaded, "default.shader_speed", &speed);
    TEST_ASSERT(speed == 2.5, "Verify loaded value: default.shader_speed");

    const char *mode = vibe_path_get_string(loaded, "output.DP-1.mode");
    TEST_ASSERT(mode != NULL && strcmp(mode, "fill") == 0,
                "Verify loaded value: output.DP-1.mode");

    vibe_value_free(loaded);

    /* Clean up */
    unlink(test_file);
}

void test_integration_scenario() {
    TEST_SECTION("Integration Test: Full Config Workflow");

    const char *test_file = "/tmp/neowall-test-integration.vibe";

    /* Create initial config */
    VibeValue *root = vibe_value_new_object();
    vibe_path_set_string(root, "default.shader", "plasma.glsl");
    vibe_path_set_float(root, "default.shader_speed", 1.0);

    /* Save to file */
    TEST_ASSERT(vibe_path_write_file(root, test_file),
                "Save initial config");

    /* Load from file */
    VibeParser *parser = vibe_parser_new();
    VibeValue *loaded = vibe_parse_file(parser, test_file);
    vibe_parser_free(parser);

    TEST_ASSERT(loaded != NULL, "Load config");

    /* Modify values (simulating set-config commands) */
    vibe_path_set_float(loaded, "default.shader_speed", 2.0);
    vibe_path_set_string(loaded, "output.DP-1.mode", "fit");
    vibe_path_set_int(loaded, "output.DP-1.duration", 600);

    /* Save modified config */
    TEST_ASSERT(vibe_path_write_file(loaded, test_file),
                "Save modified config");

    vibe_value_free(loaded);

    /* Load again and verify changes persisted */
    parser = vibe_parser_new();
    VibeValue *reloaded = vibe_parse_file(parser, test_file);
    vibe_parser_free(parser);

    double speed;
    vibe_path_get_float(reloaded, "default.shader_speed", &speed);
    TEST_ASSERT(speed == 2.0, "Changes persisted: shader_speed");

    const char *mode = vibe_path_get_string(reloaded, "output.DP-1.mode");
    TEST_ASSERT(strcmp(mode, "fit") == 0, "Changes persisted: mode");

    int64_t duration;
    vibe_path_get_int(reloaded, "output.DP-1.duration", &duration);
    TEST_ASSERT(duration == 600, "Changes persisted: duration");

    /* Test reset (delete keys) */
    vibe_path_delete(reloaded, "default.shader_speed");
    vibe_path_write_file(reloaded, test_file);

    vibe_value_free(reloaded);

    /* Verify deletion persisted */
    parser = vibe_parser_new();
    VibeValue *final = vibe_parse_file(parser, test_file);
    vibe_parser_free(parser);

    TEST_ASSERT(!vibe_path_exists(final, "default.shader_speed"),
                "Deletion persisted");
    TEST_ASSERT(vibe_path_exists(final, "default.shader"),
                "Other keys still exist");

    vibe_value_free(final);

    /* Clean up */
    unlink(test_file);
}

/* ============================================================================
 * Main Test Runner
 * ============================================================================ */

int main() {
    printf("\n");
    printf("┏━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┓\n");
    printf("┃  VIBE Path Operations Test Suite                   ┃\n");
    printf("┗━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┛\n");

    /* Run all tests */
    test_vibe_path_get();
    test_vibe_path_set();
    test_vibe_path_delete();
    test_vibe_path_list_keys();
    test_vibe_path_validate();
    test_vibe_path_split();
    test_vibe_path_file_io();
    test_integration_scenario();

    /* Print summary */
    printf("\n");
    printf("┏━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┓\n");
    printf("┃  Test Summary                                       ┃\n");
    printf("┗━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┛\n");
    printf("\n");
    printf("  Total:  %d\n", tests_run);
    printf("  " GREEN "Passed: %d" RESET "\n", tests_passed);
    printf("  " RED "Failed: %d" RESET "\n", tests_failed);
    printf("\n");

    if (tests_failed == 0) {
        printf(GREEN "  ✓ All tests passed!" RESET "\n\n");
        return 0;
    } else {
        printf(RED "  ✗ Some tests failed!" RESET "\n\n");
        return 1;
    }
}
