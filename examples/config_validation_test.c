/*
 * Configuration Rules Validation System - Test/Example
 *
 * This demonstrates the NeoWall configuration rules system that ensures
 * static wallpaper settings and shader settings are used correctly.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

/* Mock types for demonstration - in real code these come from neowall.h */
typedef struct neowall_state {
    void *outputs;
} neowall_state;

typedef struct output_state {
    char connector_name[64];
    struct {
        int type;  /* 0=image, 1=shader */
        char path[4096];
        char shader_path[4096];
    } config;
    struct output_state *next;
} output_state;

typedef enum {
    WALLPAPER_TYPE_STATIC,
    WALLPAPER_TYPE_SHADER,
    WALLPAPER_TYPE_ANY,
} wallpaper_type_t;

/* Function declarations - these would come from config_rules.h */
bool config_validate_rules(
    struct neowall_state *state,
    const char *output_name,
    const char *key,
    const char *value,
    char *error_buf,
    size_t error_len
);

wallpaper_type_t config_get_wallpaper_type(
    struct neowall_state *state,
    const char *output_name
);

bool config_is_key_applicable(
    const char *key,
    wallpaper_type_t wallpaper_type
);

size_t config_get_applicable_keys(
    wallpaper_type_t wallpaper_type,
    const char **keys,
    size_t max_keys
);

void config_explain_key_rules(
    const char *key,
    char *buf,
    size_t buf_size
);

/* ============================================================================
 * TEST CASES
 * ============================================================================ */

void test_mutual_exclusion(void) {
    printf("\n=== TEST 1: Mutual Exclusion (path vs shader) ===\n");

    neowall_state state = {0};
    char error[256];

    printf("Testing: Setting both 'path' and 'shader'\n");
    printf("Expected: ERROR - cannot use both\n");

    /* Simulate state where path is already set */
    /* In real code, this would check actual state */

    bool valid = config_validate_rules(&state, "DP-1", "shader", "plasma.glsl",
                                       error, sizeof(error));

    if (!valid) {
        printf("✓ Validation correctly rejected: %s\n", error);
    } else {
        printf("✗ Validation should have failed!\n");
    }
}

void test_static_only_keys(void) {
    printf("\n=== TEST 2: Static-Only Keys ===\n");

    const char *static_keys[] = {"mode", "transition", "transition_duration"};

    printf("Testing keys that only work with static wallpapers (path):\n");

    for (int i = 0; i < 3; i++) {
        printf("\n  Key: %s\n", static_keys[i]);

        /* Check applicability */
        bool static_ok = config_is_key_applicable(static_keys[i], WALLPAPER_TYPE_STATIC);
        bool shader_ok = config_is_key_applicable(static_keys[i], WALLPAPER_TYPE_SHADER);

        printf("    - With static wallpaper: %s\n", static_ok ? "✓ ALLOWED" : "✗ REJECTED");
        printf("    - With shader wallpaper: %s\n", shader_ok ? "✗ BUG!" : "✓ REJECTED");

        /* Get explanation */
        char explanation[512];
        config_explain_key_rules(static_keys[i], explanation, sizeof(explanation));
        printf("    Explanation: %s\n", explanation);
    }
}

void test_shader_only_keys(void) {
    printf("\n=== TEST 3: Shader-Only Keys ===\n");

    const char *shader_keys[] = {"shader_speed", "shader_fps", "vsync", "channels", "show_fps"};

    printf("Testing keys that only work with shaders:\n");

    for (int i = 0; i < 5; i++) {
        printf("\n  Key: %s\n", shader_keys[i]);

        /* Check applicability */
        bool static_ok = config_is_key_applicable(shader_keys[i], WALLPAPER_TYPE_STATIC);
        bool shader_ok = config_is_key_applicable(shader_keys[i], WALLPAPER_TYPE_SHADER);

        printf("    - With static wallpaper: %s\n", static_ok ? "✗ BUG!" : "✓ REJECTED");
        printf("    - With shader wallpaper: %s\n", shader_ok ? "✓ ALLOWED" : "✗ REJECTED");

        /* Get explanation */
        char explanation[512];
        config_explain_key_rules(shader_keys[i], explanation, sizeof(explanation));
        printf("    Explanation: %s\n", explanation);
    }
}

void test_universal_keys(void) {
    printf("\n=== TEST 4: Universal Keys ===\n");

    const char *universal_keys[] = {"duration"};

    printf("Testing keys that work with BOTH static and shader:\n");

    for (int i = 0; i < 1; i++) {
        printf("\n  Key: %s\n", universal_keys[i]);

        /* Check applicability */
        bool static_ok = config_is_key_applicable(universal_keys[i], WALLPAPER_TYPE_STATIC);
        bool shader_ok = config_is_key_applicable(universal_keys[i], WALLPAPER_TYPE_SHADER);

        printf("    - With static wallpaper: %s\n", static_ok ? "✓ ALLOWED" : "✗ REJECTED");
        printf("    - With shader wallpaper: %s\n", shader_ok ? "✓ ALLOWED" : "✗ REJECTED");

        if (strcmp(universal_keys[i], "duration") == 0) {
            printf("    Note: 'duration' works for directories of images OR shaders\n");
        }
    }
}

void test_applicable_keys_list(void) {
    printf("\n=== TEST 5: Listing Applicable Keys ===\n");

    const char *keys[32];
    size_t count;

    printf("\nKeys applicable to STATIC wallpapers:\n");
    count = config_get_applicable_keys(WALLPAPER_TYPE_STATIC, keys, 32);
    for (size_t i = 0; i < count; i++) {
        printf("  - %s\n", keys[i]);
    }

    printf("\nKeys applicable to SHADER wallpapers:\n");
    count = config_get_applicable_keys(WALLPAPER_TYPE_SHADER, keys, 32);
    for (size_t i = 0; i < count; i++) {
        printf("  - %s\n", keys[i]);
    }
}

void test_real_world_scenarios(void) {
    printf("\n=== TEST 6: Real-World Scenarios ===\n");

    neowall_state state = {0};
    char error[256];
    bool valid;

    /* Scenario 1: Valid static wallpaper config */
    printf("\nScenario 1: Setting up static wallpaper with transition\n");
    printf("  path: ~/Pictures/wallpaper.png\n");
    printf("  transition: fade\n");

    valid = config_validate_rules(&state, "DP-1", "transition", "fade", error, sizeof(error));
    printf("  Result: %s\n", valid ? "✓ VALID" : "✗ INVALID");
    if (!valid) printf("  Error: %s\n", error);

    /* Scenario 2: Invalid - trying to set shader_speed on static wallpaper */
    printf("\nScenario 2: Trying to set shader_speed on static wallpaper\n");
    printf("  path: ~/Pictures/wallpaper.png (already set)\n");
    printf("  shader_speed: 2.0 (trying to add)\n");

    valid = config_validate_rules(&state, "DP-1", "shader_speed", "2.0", error, sizeof(error));
    printf("  Result: %s\n", valid ? "✗ BUG - should reject!" : "✓ CORRECTLY REJECTED");
    if (!valid) printf("  Error: %s\n", error);

    /* Scenario 3: Valid shader config */
    printf("\nScenario 3: Setting up shader wallpaper\n");
    printf("  shader: matrix.glsl\n");
    printf("  shader_speed: 1.5\n");
    printf("  vsync: true\n");

    valid = config_validate_rules(&state, "DP-2", "shader_speed", "1.5", error, sizeof(error));
    printf("  Result: %s\n", valid ? "✓ VALID" : "✗ INVALID");
    if (!valid) printf("  Error: %s\n", error);

    /* Scenario 4: Invalid - trying to set transition on shader */
    printf("\nScenario 4: Trying to set transition on shader wallpaper\n");
    printf("  shader: plasma.glsl (already set)\n");
    printf("  transition: fade (trying to add)\n");

    valid = config_validate_rules(&state, "DP-2", "transition", "fade", error, sizeof(error));
    printf("  Result: %s\n", valid ? "✗ BUG - should reject!" : "✓ CORRECTLY REJECTED");
    if (!valid) printf("  Error: %s\n", error);

    /* Scenario 5: Valid shader-only setting */
    printf("\nScenario 5: Setting show_fps (shader-only)\n");
    printf("  show_fps: true\n");

    valid = config_validate_rules(&state, "DP-1", "show_fps", "true", error, sizeof(error));
    printf("  Result for static: %s\n", valid ? "✗ BUG - should reject!" : "✓ CORRECTLY REJECTED");
    if (!valid) printf("  Error: %s\n", error);

    valid = config_validate_rules(&state, "DP-2", "show_fps", "true", error, sizeof(error));
    printf("  Result for shader: %s\n", valid ? "✓ VALID" : "✗ INVALID");
}

void print_rules_summary(void) {
    printf("\n╔════════════════════════════════════════════════════════════════╗\n");
    printf("║         NEOWALL CONFIGURATION RULES SUMMARY                    ║\n");
    printf("╚════════════════════════════════════════════════════════════════╝\n");

    printf("\n┌─ FUNDAMENTAL RULE ─────────────────────────────────────────┐\n");
    printf("│ Choose ONE: path (static) XOR shader (animated)             │\n");
    printf("│ These are mutually exclusive - never use both together!     │\n");
    printf("└──────────────────────────────────────────────────────────────┘\n");

    printf("\n┌─ STATIC WALLPAPER ONLY (with 'path') ─────────────────────┐\n");
    printf("│ • mode                - How to display image                 │\n");
    printf("│ • transition          - Effect when changing                 │\n");
    printf("│ • transition_duration - How long transition takes           │\n");
    printf("└──────────────────────────────────────────────────────────────┘\n");

    printf("\n┌─ SHADER WALLPAPER ONLY (with 'shader') ───────────────────┐\n");
    printf("│ • shader_speed        - Animation speed multiplier          │\n");
    printf("│ • shader_fps          - Target rendering framerate          │\n");
    printf("│ • vsync               - Sync to monitor refresh             │\n");
    printf("│ • channels            - Texture inputs (iChannel0-3)        │\n");
    printf("│ • show_fps            - FPS counter overlay                  │\n");
    printf("└──────────────────────────────────────────────────────────────┘\n");

    printf("\n┌─ UNIVERSAL (works with both) ──────────────────────────────┐\n");
    printf("│ • duration            - Cycling interval (for directories)   │\n");
    printf("└──────────────────────────────────────────────────────────────┘\n");
}

/* ============================================================================
 * MAIN
 * ============================================================================ */

int main(int argc, char **argv) {
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════════╗\n");
    printf("║  NEOWALL CONFIGURATION RULES VALIDATION SYSTEM - TEST SUITE      ║\n");
    printf("╚══════════════════════════════════════════════════════════════════╝\n");

    printf("\nThis demonstrates the explicit, extensible validation system that\n");
    printf("ensures static wallpaper settings and shader settings are used correctly.\n");

    print_rules_summary();

    /* Run all tests */
    test_mutual_exclusion();
    test_static_only_keys();
    test_shader_only_keys();
    test_universal_keys();
    test_applicable_keys_list();
    test_real_world_scenarios();

    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════════╗\n");
    printf("║  SUMMARY                                                         ║\n");
    printf("╚══════════════════════════════════════════════════════════════════╝\n");
    printf("\nThe configuration rules system provides:\n");
    printf("  ✓ Explicit validation of key combinations\n");
    printf("  ✓ Clear error messages explaining what's wrong\n");
    printf("  ✓ Type-safe separation of static vs shader settings\n");
    printf("  ✓ Extensible - easy to add new rules\n");
    printf("  ✓ Self-documenting - rules ARE the documentation\n");

    printf("\nTo add a new shader-only setting:\n");
    printf("  1. Add to config_keys.h\n");
    printf("  2. Define rule in config_rules.h (RULE_*_SHADER_ONLY)\n");
    printf("  3. Add to CONFIG_RULES_LIST macro\n");
    printf("  4. Update SHADER_ONLY_KEYS and IS_SHADER_ONLY_KEY\n");
    printf("  → Validation happens automatically!\n");

    printf("\n");

    return 0;
}
