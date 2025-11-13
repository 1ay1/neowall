# NeoWall Configuration Rules Test Suite

This directory contains comprehensive tests for the NeoWall configuration rules validation system.

## Overview

The test suite validates that:
- Static wallpaper settings (mode, transition, etc.) only work with image wallpapers
- Shader wallpaper settings (shader_speed, vsync, etc.) only work with shader wallpapers
- Universal settings (duration) work with both types
- Path and shader are mutually exclusive
- Error messages are helpful and descriptive
- Multiple outputs can have different types independently

## Test Categories

### 1. **Type Detection Tests**
Verify that the system correctly identifies wallpaper types:
- Static wallpapers (using `path`)
- Shader wallpapers (using `shader`)
- Undefined/uninitialized wallpapers

### 2. **Key Applicability Tests**
Ensure keys are correctly categorized:
- Static-only keys: `mode`, `transition`, `transition_duration`
- Shader-only keys: `shader_speed`, `shader_fps`, `vsync`, `channels`, `show_fps`
- Universal keys: `duration` (for directory cycling)

### 3. **Validation Rules Tests**
Verify that validation correctly accepts/rejects configurations:
- Static wallpapers accept static keys
- Shader wallpapers accept shader keys
- Static wallpapers reject shader keys (with helpful errors)
- Shader wallpapers reject static keys (with helpful errors)
- Mutual exclusion of `path` and `shader`

### 4. **Applicable Keys Listing Tests**
Test the API that lists which keys work with each type:
- Static wallpaper applicable keys list
- Shader wallpaper applicable keys list
- Universal keys appear in both lists

### 5. **Documentation Tests**
Verify that the system can explain rules:
- `config_explain_key_rules()` provides helpful explanations
- Error messages mention relevant wallpaper types

### 6. **Edge Cases**
Handle corner cases gracefully:
- NULL pointers
- Empty strings
- Multiple outputs with different types
- Nonexistent outputs

### 7. **Real-World Scenarios**
Test common usage patterns:
- Setting up a static wallpaper with transitions
- Configuring a shader with speed and FPS controls
- Directory cycling for images
- Directory cycling for shaders
- `show_fps` only works with shaders

## Building and Running

### Using Make (Standalone)

The easiest way to build and run tests:

```bash
# Build and run all tests
make test

# Or step by step
make
./test_config_rules

# Clean and rebuild
make clean test

# Run with memory leak detection
make valgrind
```

### Using Meson

If you're building the full project:

```bash
# Configure if not already done
meson setup build

# Build and run tests
meson test -C build

# Run specific test suite
meson test -C build config_rules

# Run with verbose output
meson test -C build --verbose

# Run specific test suites
meson test -C build --suite validation
meson test -C build --suite edge_cases
meson test -C build --suite scenarios
```

## Test Output

### Success Output
When all tests pass, you'll see:
```
╔════════════════════════════════════════════════════════════════╗
║                        TEST SUMMARY                            ║
╚════════════════════════════════════════════════════════════════╝

  Total Tests:  28
  Passed:       28
  Failed:       0

  ✓ ALL TESTS PASSED!
```

### Failure Output
When tests fail:
```
TEST: Validation: Static wallpaper rejects shader keys
  ✓ 'shader_speed' should be INVALID for static wallpaper
  ✗ FAILED: Error message for 'shader_speed' should mention 'shader'
    at tests/test_config_rules.c:365

TEST SUMMARY
  Total Tests:  28
  Passed:       27
  Failed:       1

  ✗ SOME TESTS FAILED
```

## Test Structure

Each test follows this pattern:

```c
static bool test_something(void) {
    TEST_START("Human-readable test name");
    
    // Setup
    struct neowall_state *state = create_test_state();
    struct output_state *output = add_test_output(state, "DP-1");
    
    // Test conditions
    ASSERT_TRUE(condition, "Descriptive message");
    ASSERT_FALSE(condition, "Descriptive message");
    ASSERT_EQ(a, b, "Descriptive message");
    ASSERT_STR_CONTAINS(string, substring, "Descriptive message");
    
    // Cleanup
    free_test_state(state);
    TEST_PASS();
}
```

## Adding New Tests

To add a new test:

1. **Write the test function** in `test_config_rules.c`:
   ```c
   static bool test_my_new_feature(void) {
       TEST_START("My New Feature");
       // ... test code ...
       TEST_PASS();
   }
   ```

2. **Register it** in the `all_tests` array:
   ```c
   static const test_case_t all_tests[] = {
       // ... existing tests ...
       {"My New Feature", test_my_new_feature},
   };
   ```

3. **Rebuild and run**:
   ```bash
   make clean test
   ```

## Test Coverage

### ✅ Currently Tested

- [x] Wallpaper type detection (static, shader, undefined)
- [x] Key applicability for all key types
- [x] Validation rules enforcement
- [x] Path/shader mutual exclusion
- [x] Static-only keys rejection on shaders
- [x] Shader-only keys rejection on static
- [x] Universal keys work on both types
- [x] Multiple outputs independence
- [x] Error message quality
- [x] NULL safety
- [x] Empty string handling
- [x] Real-world configuration scenarios
- [x] Directory cycling for images
- [x] Directory cycling for shaders
- [x] `show_fps` shader-only behavior

### 🔄 Future Test Ideas

- [ ] Integration with actual config parsing
- [ ] IPC command validation
- [ ] Config file round-trip (read → validate → write)
- [ ] Performance with many outputs
- [ ] Thread safety of validation
- [ ] Rule conflict detection
- [ ] Custom rule registration (extensibility)

## Helper Functions

### State Management
```c
struct neowall_state* create_test_state(void);
struct output_state* add_test_output(struct neowall_state *state, const char *name);
void set_static_wallpaper(struct output_state *output, const char *path);
void set_shader_wallpaper(struct output_state *output, const char *shader);
void free_test_state(struct neowall_state *state);
```

### Assertions
```c
ASSERT_TRUE(condition, message);
ASSERT_FALSE(condition, message);
ASSERT_EQ(a, b, message);
ASSERT_STR_EQ(str1, str2, message);
ASSERT_STR_CONTAINS(string, substring, message);
```

## Debugging Failed Tests

If a test fails:

1. **Look at the error message** - it should indicate what went wrong
2. **Check the line number** - shows where the assertion failed
3. **Run with verbose output**:
   ```bash
   ./test_config_rules 2>&1 | tee test.log
   ```
4. **Run under debugger**:
   ```bash
   gdb ./test_config_rules
   (gdb) run
   ```
5. **Check for memory issues**:
   ```bash
   make valgrind
   ```

## Contributing

When adding new rules or features to the config system:

1. Write tests for the new behavior **first**
2. Ensure all existing tests still pass
3. Add tests for edge cases
4. Update this README if adding new test categories

## Rules Being Tested

### Fundamental Rule
**Choose ONE:** `path` (static) **XOR** `shader` (animated)

These are mutually exclusive - never use both together!

### Static Wallpaper Only (with `path`)
- `mode` - How to display image (fill, fit, center, etc.)
- `transition` - Effect when changing wallpapers
- `transition_duration` - How long transition takes (milliseconds)

### Shader Wallpaper Only (with `shader`)
- `shader_speed` - Animation speed multiplier
- `shader_fps` - Target rendering framerate
- `vsync` - Sync to monitor refresh rate
- `channels` - Texture inputs (iChannel0-3)
- `show_fps` - Display FPS counter overlay

### Universal (works with both)
- `duration` - Cycling interval for directories (seconds)

## Exit Codes

- `0` - All tests passed
- `1` - One or more tests failed

## Files

```
tests/
├── README.md              # This file
├── Makefile              # Standalone build system
├── meson.build           # Meson integration
└── test_config_rules.c   # Test suite implementation
```

## Questions?

For questions about:
- **Test failures**: Check test output and error messages
- **Adding tests**: See "Adding New Tests" section above
- **Rules system**: See `docs/CONFIG_RULES.md`
- **Implementation**: See `src/neowalld/config/config_rules.{h,c}`

---

**Last Updated**: January 2025
**Status**: ✅ All 28 tests passing