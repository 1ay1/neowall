# NeoWall Commands Reference

**Auto-generated documentation from command registry**

This document is generated from the live command registry, making it the single source of truth for all available commands.

## Command Categories


> **Note:** Start the neowall daemon to generate complete documentation with live command data.

### Template Structure

Each command includes:
- **Name**: Command identifier
- **Category**: Command category (wallpaper, cycling, shader, output, config, info)
- **Description**: What the command does
- **Handler**: C function that implements the command
- **File**: Source file containing implementation
- **Capabilities**: Required permissions and side effects

### Available Categories

- **wallpaper**: Wallpaper switching and management
- **cycling**: Automatic wallpaper cycling control
- **shader**: Shader animation control
- **output**: Per-output (multi-monitor) control
- **config**: Configuration queries
- **info**: System information and introspection

Run `neowall list-commands` for the current list of all commands.


## Command Usage

### Basic Commands

```bash
# Get daemon status
neowall status

# Switch wallpaper
neowall next
neowall prev

# Control cycling
neowall pause
neowall resume

# Multi-monitor control
neowall list-outputs
neowall next-output DP-1
neowall prev-output DP-1
```

### Introspection

```bash
# List all commands with metadata
neowall list-commands

# Filter by category
neowall list-commands --category=output

# Get command statistics
neowall command-stats
neowall command-stats next
```

## Implementation Details

All commands are registered in the centralized command registry:
- Core commands: `src/neowalld/commands/registry.c`
- Output commands: `src/neowalld/commands/output_commands.c`
- Config commands: `src/neowalld/commands/config_commands.c`

Each command is registered using type-safe macros that automatically:
- Generate handler function names
- Capture implementation file paths
- Link to handler functions
- Track statistics and performance metrics

## Command Statistics

The daemon tracks execution statistics for all commands:
- Total calls
- Success/failure counts
- Execution time (min/avg/max)
- Last called timestamp
- Last error message

Query with: `neowall command-stats [command-name]`

---

*Generated from live command registry*
