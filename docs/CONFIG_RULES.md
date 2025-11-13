# NeoWall Configuration Rules System

**Explicit, Extensible Validation for Static vs Shader Wallpapers**

---

## 📋 Overview

NeoWall's configuration rules system provides **explicit validation** to ensure that configuration keys are only used with the appropriate wallpaper type. This prevents invalid configurations like trying to set a transition effect on an animated shader or setting shader speed on a static image.

### Key Principle

**Static wallpapers and Shaders are FUNDAMENTALLY DIFFERENT:**

- **Static wallpapers** (using `path`) support: `mode`, `transition`, `transition_duration`, `duration`
- **Shaders** (using `shader`) support: `shader_speed`, `shader_fps`, `vsync`, `channels`
- **Universal settings** work with both: `show_fps`

---

## 🎯 Design Goals

1. **Explicit and Clear** - Every rule is explicitly documented with clear error messages
2. **Extensible** - Adding new settings and rules is straightforward
3. **Single Source of Truth** - All rules defined in `config_rules.h`
4. **Type-Safe** - Compile-time enforcement of rule definitions
5. **Self-Documenting** - Rules include human-readable descriptions

---

## 📁 File Structure

```
src/neowalld/config/
├── config_rules.h        # Rule definitions (SINGLE SOURCE OF TRUTH)
├── config_rules.c        # Rule validation implementation
├── config_keys.h         # Config key definitions
├── config_schema.h       # Schema and type definitions
└── config_schema.c       # Schema implementation
```

---

## 🔒 Configuration Rules

### Rule 1: Path and Shader Are Mutually Exclusive

**The most fundamental rule.**

```c
// VALID: Choose ONE
default {
    path ~/Pictures/wallpaper.png
}

// VALID: Choose the OTHER
default {
    shader matrix.glsl
}

// INVALID: Cannot use both!
default {
    path ~/Pictures/wallpaper.png
    shader matrix.glsl  // ERROR!
}
```

**Error Message:**
```
Cannot use both 'path' and 'shader'. Choose EITHER static wallpaper (path) 
OR animated shader (shader), not both.
```

---

### Static Wallpaper Only Rules

These settings **ONLY** work with `path` (static images):

#### Rule 2: Display Mode

```c
default {
    path ~/Pictures/wallpaper.png
    mode fill  // ✅ VALID - static images can be scaled
}

default {
    shader matrix.glsl
    mode fill  // ❌ INVALID - shaders always render fullscreen
}
```

**Error Message:**
```
'mode' only applies to static wallpapers (path). Shaders always render 
fullscreen and don't use display modes.
```

**Valid modes:** `center`, `stretch`, `fit`, `fill`, `tile`

---

#### Rule 3: Transition Effects

```c
default {
    path ~/Pictures/wallpapers/
    transition fade  // ✅ VALID - transitions between images
}

default {
    shader plasma.glsl
    transition fade  // ❌ INVALID - shaders are already animated
}
```

**Error Message:**
```
'transition' only applies to static wallpapers (path). Shaders are already 
animated and don't use transitions.
```

**Valid transitions:** `none`, `fade`, `slide-left`, `slide-right`, `glitch`, `pixelate`

---

#### Rule 4: Transition Duration

```c
default {
    path ~/Pictures/wallpapers/
    transition fade
    transition_duration 0.5  // ✅ VALID - how long the fade takes
}

default {
    shader cyberpunk.glsl
    transition_duration 0.5  // ❌ INVALID - no transitions for shaders
}
```

**Error Message:**
```
'transition_duration' only applies to static wallpapers (path). Shaders 
don't use transitions.
```

---

#### Rule 5: Cycling Duration

```c
default {
    path ~/Pictures/wallpapers/  # Directory of images
    duration 300  // ✅ VALID - cycle every 5 minutes
}

default {
    shader retro_wave.glsl
    duration 300  // ❌ INVALID - shaders animate continuously
}
```

**Error Message:**
```
'duration' (cycling) only applies to static wallpapers (path with directory). 
Shaders animate continuously and don't cycle.
```

---

### Shader Only Rules

These settings **ONLY** work with `shader` (animated shaders):

#### Rule 6: Shader Speed

```c
default {
    shader matrix.glsl
    shader_speed 2.0  // ✅ VALID - double speed animation
}

default {
    path ~/Pictures/wallpaper.png
    shader_speed 2.0  // ❌ INVALID - images have no animation
}
```

**Error Message:**
```
'shader_speed' only applies to shader wallpapers (shader). Static images 
(path) have no animation speed.
```

**Range:** 0.1 - 10.0 (multiplier)

---

#### Rule 7: Shader FPS

```c
default {
    shader plasma.glsl
    shader_fps 30  // ✅ VALID - render at 30 FPS
}

default {
    path ~/Pictures/wallpaper.png
    shader_fps 30  // ❌ INVALID - static images don't animate
}
```

**Error Message:**
```
'shader_fps' only applies to shader wallpapers (shader). Static images 
(path) don't need FPS control.
```

**Range:** 1 - 240 FPS

---

#### Rule 8: VSync

```c
default {
    shader matrix_rain.glsl
    vsync true  // ✅ VALID - sync to monitor refresh
}

default {
    path ~/Pictures/wallpaper.png
    vsync true  // ❌ INVALID - no continuous rendering
}
```

**Error Message:**
```
'vsync' only applies to shader wallpapers (shader). Static images (path) 
don't need vsync.
```

---

#### Rule 9: Shader Channels

```c
default {
    shader advanced.glsl
    channels ["gray-noise", "rgba-noise"]  // ✅ VALID - iChannel textures
}

default {
    path ~/Pictures/wallpaper.png
    channels ["gray-noise"]  // ❌ INVALID - no shader uniforms
}
```

**Error Message:**
```
'channels' (iChannel textures) only apply to shader wallpapers (shader). 
Static images (path) don't use shader uniforms.
```

**Built-in textures:** `gray-noise`, `rgba-noise`, `blue-noise`, `wood`, `abstract`

---

### Universal Rules

These settings work with **BOTH** static and shader wallpapers:

#### Show FPS Counter

```c
default {
    path ~/Pictures/wallpaper.png
    show_fps true  // ✅ VALID
}

default {
    shader plasma.glsl
    show_fps true  // ✅ VALID
}
```

This is the only visual overlay setting that applies to both types.

---

## 🔧 API Reference

### Validation Functions

#### `config_validate_rules()`

```c
bool config_validate_rules(
    struct neowall_state *state,
    const char *output_name,
    const char *key,
    const char *value,
    char *error_buf,
    size_t error_len
);
```

Validates that a key-value pair follows all configuration rules.

**Example:**
```c
char error[256];
if (!config_validate_rules(state, "DP-1", "shader_speed", "2.0", error, sizeof(error))) {
    log_error("Invalid config: %s", error);
    // Error: "'shader_speed' only applies to shader wallpapers (shader).
    //         Static images (path) have no animation speed."
}
```

---

#### `config_get_wallpaper_type()`

```c
wallpaper_type_t config_get_wallpaper_type(
    struct neowall_state *state,
    const char *output_name
);
```

Returns the current wallpaper type for an output.

**Returns:**
- `WALLPAPER_TYPE_STATIC` - Using static images (`path`)
- `WALLPAPER_TYPE_SHADER` - Using animated shader (`shader`)
- `WALLPAPER_TYPE_ANY` - Type not yet determined

**Example:**
```c
wallpaper_type_t type = config_get_wallpaper_type(state, "DP-1");
if (type == WALLPAPER_TYPE_SHADER) {
    printf("This output is using a shader\n");
}
```

---

#### `config_is_key_applicable()`

```c
bool config_is_key_applicable(
    const char *key,
    wallpaper_type_t wallpaper_type
);
```

Checks if a key is applicable to the current wallpaper type.

**Example:**
```c
wallpaper_type_t type = config_get_wallpaper_type(state, "DP-1");
if (!config_is_key_applicable("transition", type)) {
    printf("Transition not available for shader wallpapers\n");
}
```

---

#### `config_get_applicable_keys()`

```c
size_t config_get_applicable_keys(
    wallpaper_type_t wallpaper_type,
    const char **keys,
    size_t max_keys
);
```

Gets a list of keys applicable to a specific wallpaper type.

**Example:**
```c
const char *keys[32];
size_t count = config_get_applicable_keys(WALLPAPER_TYPE_SHADER, keys, 32);
printf("Shader settings:\n");
for (size_t i = 0; i < count; i++) {
    printf("  - %s\n", keys[i]);
}
// Output:
// Shader settings:
//   - shader
//   - shader_speed
//   - shader_fps
//   - vsync
//   - channels
//   - show_fps
```

---

#### `config_explain_key_rules()`

```c
void config_explain_key_rules(
    const char *key,
    char *buf,
    size_t buf_size
);
```

Gets human-readable explanation of rules for a specific key.

**Example:**
```c
char explanation[512];
config_explain_key_rules("shader_speed", explanation, sizeof(explanation));
printf("%s\n", explanation);

// Output:
// shader_speed:
//   - 'shader_speed' only applies to shader wallpapers (shader).
//     Static images (path) have no animation speed.
//
// Applies to: Shader wallpapers only (when using 'shader')
```

---

#### `config_get_all_rules()`

```c
const config_rule_t *config_get_all_rules(void);
```

Returns array of all configuration rules (for introspection/documentation).

**Example:**
```c
const config_rule_t *rules = config_get_all_rules();
printf("Configuration Rules:\n");
printf("====================\n\n");

for (int i = 0; rules[i].key1 != NULL; i++) {
    printf("Rule %d: %s\n", i+1, rules[i].error_message);
}
```

---

## 🛠️ Adding New Rules

### Example: Adding a New Shader Setting

Let's add a new `shader_quality` setting that only works with shaders.

#### Step 1: Add to `config_keys.h`

```c
/* Shader-specific keys */
#define CONFIG_KEY_DEFAULT_SHADER_QUALITY "default.shader_quality"
#define CONFIG_KEY_OUTPUT_SHADER_QUALITY  "output.shader_quality"
```

#### Step 2: Add to `CONFIG_KEYS_LIST` macro

```c
#define CONFIG_KEYS_LIST(X) \
    /* ... existing keys ... */ \
    X(CONFIG_KEY_DEFAULT_SHADER_QUALITY, "default", "shader_quality", CONFIG_TYPE_ENUM, CONFIG_SCOPE_GLOBAL) \
    X(CONFIG_KEY_OUTPUT_SHADER_QUALITY,  "output",  "shader_quality", CONFIG_TYPE_ENUM, CONFIG_SCOPE_OUTPUT)
```

#### Step 3: Define rule in `config_rules.h`

```c
/*
 * RULE 10: SHADER_QUALITY - Rendering quality for shaders
 * --------------------------------------------------------
 * Controls shader rendering quality (low/medium/high).
 * Only applies to shaders. Static images don't have quality settings.
 */
#define RULE_SHADER_QUALITY_SHADER_ONLY \
    { \
        .type = RULE_TYPE_ONLY_WITH_TYPE, \
        .key1 = "shader_quality", \
        .key2 = "shader", \
        .applies_to = WALLPAPER_TYPE_SHADER, \
        .error_message = "'shader_quality' only applies to shader wallpapers (shader). Static images (path) don't have quality settings." \
    }
```

#### Step 4: Add to `CONFIG_RULES_LIST` macro

```c
#define CONFIG_RULES_LIST(X) \
    X(RULE_PATH_SHADER_EXCLUSIVE) \
    /* ... existing rules ... */ \
    X(RULE_SHADER_QUALITY_SHADER_ONLY)
```

#### Step 5: Update `SHADER_ONLY_KEYS` macro

```c
#define SHADER_ONLY_KEYS(X) \
    X("shader_speed") \
    X("shader_fps") \
    X("vsync") \
    X("channels") \
    X("shader_quality")  /* New! */
```

#### Step 6: Update `IS_SHADER_ONLY_KEY` macro

```c
#define IS_SHADER_ONLY_KEY(key) \
    (strcmp(key, "shader_speed") == 0 || \
     strcmp(key, "shader_fps") == 0 || \
     strcmp(key, "vsync") == 0 || \
     strcmp(key, "channels") == 0 || \
     strcmp(key, "shader_quality") == 0)  /* New! */
```

#### Step 7: Add stubs to `config_schema.c`

```c
/* Description */
static inline const char *get_description_for_default_shader_quality(void) {
    return "Shader rendering quality (low/medium/high)";
}

/* Example */
static inline const char *get_example_for_default_shader_quality(void) {
    return "high";
}

/* Default */
static inline const char *get_default_for_default_shader_quality(void) {
    return "medium";
}

/* Constraints */
static inline config_constraints_t get_constraints_for_default_shader_quality(void) {
    static const char *quality_values[] = {"low", "medium", "high", NULL};
    return (config_constraints_t){.enum_values = {.values = quality_values, .count = 3}};
}

/* Apply functions */
static inline bool (*get_apply_for_default_shader_quality(void))(struct neowall_state *, const char *) {
    return NULL;
}

static inline bool (*get_apply_output_for_default_shader_quality(void))(struct output_state *, const char *) {
    return NULL;
}

/* Repeat for output_shader_quality... */
```

### That's It!

The validation system will now automatically:
- ✅ Reject `shader_quality` when using `path`
- ✅ Allow it when using `shader`
- ✅ Include it in introspection queries
- ✅ Generate proper error messages
- ✅ Show it in applicable keys list for shaders

---

## 📊 Quick Reference Tables

### Static Wallpaper Keys (only with `path`)

| Key | Description | Example |
|-----|-------------|---------|
| `mode` | Display mode | `fill`, `fit`, `center` |
| `transition` | Transition effect | `fade`, `slide-left` |
| `transition_duration` | Transition time | `0.3` (seconds) |
| `duration` | Cycling interval | `300` (seconds) |

### Shader Wallpaper Keys (only with `shader`)

| Key | Description | Example |
|-----|-------------|---------|
| `shader_speed` | Animation speed | `1.5` (multiplier) |
| `shader_fps` | Target framerate | `60` (FPS) |
| `vsync` | Vertical sync | `true` / `false` |
| `channels` | Texture inputs | `["gray-noise"]` |

### Universal Keys (work with both)

| Key | Description | Example |
|-----|-------------|---------|
| `path` / `shader` | Type selector | Mutually exclusive |
| `show_fps` | FPS counter | `true` / `false` |

---

## 🎓 Best Practices

### 1. Always Validate Before Applying

```c
char error[256];
if (!config_validate_rules(state, output_name, key, value, error, sizeof(error))) {
    // Show error to user, don't apply the setting
    return false;
}
```

### 2. Check Applicability in UI

```c
wallpaper_type_t type = config_get_wallpaper_type(state, output_name);

// Show only applicable settings in UI
if (config_is_key_applicable("transition", type)) {
    show_transition_setting();
}

if (config_is_key_applicable("shader_speed", type)) {
    show_shader_speed_setting();
}
```

### 3. Use Introspection for Dynamic UIs

```c
const char *keys[32];
size_t count = config_get_applicable_keys(current_type, keys, 32);

// Build UI dynamically based on applicable keys
for (size_t i = 0; i < count; i++) {
    add_setting_to_ui(keys[i]);
}
```

---

## 🐛 Common Errors and Solutions

### Error: "Cannot use both 'path' and 'shader'"

**Problem:** Trying to set both path and shader.

**Solution:** Choose one or clear the other first:
```bash
neowall set-config default.path ""
neowall set-config default.shader matrix.glsl
```

---

### Error: "'transition' only applies to static wallpapers"

**Problem:** Trying to set transition on a shader.

**Solution:** Transitions only work with static images:
```bash
neowall set-config default.path ~/Pictures/wallpapers/
neowall set-config default.transition fade
```

---

### Error: "'shader_speed' only applies to shader wallpapers"

**Problem:** Trying to set shader speed on static image.

**Solution:** Shader speed only works with shaders:
```bash
neowall set-config default.shader plasma.glsl
neowall set-config default.shader_speed 2.0
```

---

## 📝 Summary

The NeoWall configuration rules system provides:

1. **Explicit Validation** - Clear rules about what works together
2. **Better Error Messages** - Explains why something doesn't work
3. **Type Safety** - Prevents invalid configurations at parse time
4. **Extensibility** - Easy to add new settings and rules
5. **Self-Documentation** - Rules are the documentation

This ensures users get clear feedback and prevents confusing "silent failures" where settings are ignored.

---

## 📚 See Also

- [Configuration Schema](../schema/config.vibe) - Full schema documentation
- [Config Keys Reference](../include/config_keys.h) - All available keys
- [Usage Guide](USAGE_GUIDE.md) - How to use NeoWall
- [IPC Commands](commands/COMMANDS.md) - All available commands

---

**Made with ❤️ for the Linux desktop**