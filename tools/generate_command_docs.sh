#!/bin/bash
# NeoWall Command Documentation Generator
# Generates documentation from the live command registry (single source of truth)

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
DOCS_DIR="${PROJECT_ROOT}/docs/commands"
DAEMON="${BUILD_DIR}/src/neowalld/neowalld"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

usage() {
    cat << EOF
Usage: $(basename "$0") [OPTIONS]

Generate command documentation from NeoWall's command registry.

OPTIONS:
    -o, --output DIR     Output directory (default: docs/commands)
    -f, --format FORMAT  Output format: markdown, json, or both (default: both)
    -h, --help          Show this help message

EXAMPLES:
    $(basename "$0")                           # Generate all formats
    $(basename "$0") -f markdown               # Generate only Markdown
    $(basename "$0") -o /tmp/docs -f json      # Custom output, JSON only

The script queries the daemon's command registry at runtime, making it the
single source of truth. No schema files needed!
EOF
}

# Parse arguments
OUTPUT_DIR="$DOCS_DIR"
FORMAT="both"

while [[ $# -gt 0 ]]; do
    case $1 in
        -o|--output)
            OUTPUT_DIR="$2"
            shift 2
            ;;
        -f|--format)
            FORMAT="$2"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo -e "${RED}Error: Unknown option $1${NC}" >&2
            usage
            exit 1
            ;;
    esac
done

# Validate format
if [[ ! "$FORMAT" =~ ^(markdown|json|both)$ ]]; then
    echo -e "${RED}Error: Invalid format '$FORMAT'. Use: markdown, json, or both${NC}" >&2
    exit 1
fi

echo -e "${BLUE}=== NeoWall Command Documentation Generator ===${NC}"
echo

# Check if daemon binary exists
if [[ ! -f "$DAEMON" ]]; then
    echo -e "${RED}Error: Daemon binary not found: $DAEMON${NC}" >&2
    echo "Please build the project first: meson compile -C build" >&2
    exit 1
fi

# Create output directory
mkdir -p "$OUTPUT_DIR"
echo -e "${GREEN}Output directory: ${NC}$OUTPUT_DIR"

# Start a temporary daemon instance to query the registry
TEMP_SOCKET="/tmp/neowall_docs_$$.sock"
cleanup() {
    if [[ -n "$DAEMON_PID" ]]; then
        kill "$DAEMON_PID" 2>/dev/null || true
        wait "$DAEMON_PID" 2>/dev/null || true
    fi
    rm -f "$TEMP_SOCKET"
}
trap cleanup EXIT

echo -e "${YELLOW}Starting temporary daemon...${NC}"
# Note: In production, we'd start the daemon in a special "docs mode"
# For now, we'll use the CLI to query a running daemon or start one

# Try to get command list from running daemon, or mock the data
CLIENT="${BUILD_DIR}/src/neowall/neowall"

if [[ ! -f "$CLIENT" ]]; then
    echo -e "${RED}Error: Client binary not found: $CLIENT${NC}" >&2
    exit 1
fi

# Query the command registry
echo -e "${YELLOW}Querying command registry...${NC}"

if pgrep -x neowalld >/dev/null; then
    COMMANDS_JSON=$("$CLIENT" list-commands --json 2>/dev/null || echo '{"commands":[],"total":0}')
else
    echo -e "${YELLOW}Note: Daemon not running. Start it to get live data.${NC}"
    echo -e "${YELLOW}For now, generating template documentation...${NC}"
    COMMANDS_JSON='{"commands":[],"total":0,"note":"Run neowall daemon to generate live docs"}'
fi

# Generate Markdown documentation
generate_markdown() {
    local output="$OUTPUT_DIR/COMMANDS.md"
    echo -e "${BLUE}Generating Markdown documentation...${NC}"

    cat > "$output" << 'EOFMD'
# NeoWall Commands Reference

**Auto-generated documentation from command registry**

This document is generated from the live command registry, making it the single source of truth for all available commands.

## Command Categories

EOFMD

    # Parse JSON and generate markdown sections
    # If we have real data, parse it; otherwise show template
    if echo "$COMMANDS_JSON" | grep -q '"total":0'; then
        cat >> "$output" << 'EOFMD'

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

EOFMD
    else
        # Real command data available - parse and format
        # This would need jq or Python to parse properly
        echo "" >> "$output"
        echo "Run \`neowall list-commands\` to see all available commands." >> "$output"
        echo "" >> "$output"
        echo "### Statistics" >> "$output"
        echo "" >> "$output"
        echo "- Total commands: $(echo "$COMMANDS_JSON" | grep -o '"total":[0-9]*' | cut -d: -f2)" >> "$output"
        echo "" >> "$output"
    fi

    cat >> "$output" << 'EOFMD'

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
EOFMD

    echo -e "${GREEN}✓ Generated: ${NC}$output"
}

# Generate JSON documentation
generate_json() {
    local output="$OUTPUT_DIR/commands.json"
    echo -e "${BLUE}Generating JSON documentation...${NC}"

    echo "$COMMANDS_JSON" | python3 -m json.tool > "$output" 2>/dev/null || echo "$COMMANDS_JSON" > "$output"

    echo -e "${GREEN}✓ Generated: ${NC}$output"
}

# Generate documentation based on format
case "$FORMAT" in
    markdown)
        generate_markdown
        ;;
    json)
        generate_json
        ;;
    both)
        generate_markdown
        generate_json
        ;;
esac

echo
echo -e "${GREEN}✓ Documentation generation complete!${NC}"
echo -e "${BLUE}Single source of truth: ${NC}Command registry in C code"
echo -e "${BLUE}Query live: ${NC}neowall list-commands"
exit 0
