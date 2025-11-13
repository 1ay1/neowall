#!/bin/bash
# NeoWall Schema Validation Script
# Ensures code implementation matches schema definitions

set -e

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SCHEMA_DIR="$PROJECT_ROOT/schema"
SRC_DIR="$PROJECT_ROOT/src"
INCLUDE_DIR="$PROJECT_ROOT/include"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}NeoWall Schema Validation${NC}"
echo -e "${BLUE}========================================${NC}"
echo ""

ERRORS=0
WARNINGS=0

# Function to report error
error() {
    echo -e "${RED}[ERROR]${NC} $1"
    ((ERRORS++))
}

# Function to report warning
warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
    ((WARNINGS++))
}

# Function to report success
success() {
    echo -e "${GREEN}[OK]${NC} $1"
}

# Function to report info
info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

# ============================================================================
# VALIDATE COMMANDS SCHEMA - CATEGORY WISE
# ============================================================================
REGISTRY_FILE="$SRC_DIR/neowalld/commands/registry.c"

# Extract all command names from code
CODE_COMMANDS=$(grep -E '^\s*\.name\s*=\s*"' "$REGISTRY_FILE" | \
    sed 's/.*"\(.*\)".*/\1/' | sort)

# Define categories and their commands
declare -A CATEGORIES
CATEGORIES[system]="ping version list status"
CATEGORIES[wallpaper]="next prev current"
CATEGORIES[cycling]="pause resume"
CATEGORIES[shader]="speed-up speed-down shader-pause shader-resume"
CATEGORIES[config]="reload"

# Validate each category
for category in system wallpaper cycling shader config; do
    info ""
    info "Validating ${category} commands..."

    CATEGORY_CMDS=${CATEGORIES[$category]}

    for cmd in $CATEGORY_CMDS; do
        # Check if command exists in code
        if echo "$CODE_COMMANDS" | grep -q "^$cmd$"; then
            success "[$category] Command '$cmd' implemented"
        else
            error "[$category] Command '$cmd' in schema but not in code"
        fi

        # Check if command exists in schema
        if grep -q "^\s*$cmd\s*{" "$SCHEMA_DIR/commands.vibe"; then
            : # Command found in schema
        else
            error "[$category] Command '$cmd' missing from schema"
        fi
    done
done

# Check for commands in code but not in any category
info ""
info "Checking for uncategorized commands..."
ALL_SCHEMA_CMDS=""
for category in system wallpaper cycling shader config; do
    ALL_SCHEMA_CMDS="$ALL_SCHEMA_CMDS ${CATEGORIES[$category]}"
done

while IFS= read -r cmd; do
    found=false
    for schema_cmd in $ALL_SCHEMA_CMDS; do
        if [ "$cmd" = "$schema_cmd" ]; then
            found=true
            break
        fi
    done

    if [ "$found" = false ]; then
        warn "Command '$cmd' in code but not in any schema category"
    fi
done <<< "$CODE_COMMANDS"

# ============================================================================
# VALIDATE COMMAND HANDLERS
# ============================================================================
info ""
info "Validating command handler functions exist..."

# Extract handler names from schema
SCHEMA_HANDLERS=$(grep "handler cmd_" "$SCHEMA_DIR/commands.vibe" | \
    sed 's/.*handler\s\+\(cmd_[a-z_]\+\).*/\1/' | sort -u)

# Check if each handler is defined in registry.c
while IFS= read -r handler; do
    if ! grep -q "^static command_result_t $handler(" "$REGISTRY_FILE"; then
        error "Handler '$handler' declared in schema but not found in $REGISTRY_FILE"
    else
        success "Handler '$handler' exists"
    fi
done <<< "$SCHEMA_HANDLERS"

# ============================================================================
# VALIDATE ERROR CODES
# ============================================================================
info ""
info "Validating error codes..."

# Extract error codes from schema
SCHEMA_ERRORS=$(grep -A2 "^\s*CMD_ERROR_" "$SCHEMA_DIR/commands.vibe" | \
    grep "^\s*CMD_" | sed 's/\s*{\s*$//' | sed 's/^\s*//' | sort)

# Check if error codes are defined in commands.h (as enum values)
COMMANDS_H="$INCLUDE_DIR/commands.h"
if [ ! -f "$COMMANDS_H" ]; then
    COMMANDS_H="$SRC_DIR/neowalld/commands/commands.h"
fi

if [ -f "$COMMANDS_H" ]; then
    while IFS= read -r error_code; do
        # Check for enum value (not #define)
        if grep -q "\b$error_code\b" "$COMMANDS_H"; then
            success "Error code '$error_code' defined"
        else
            warn "Error code '$error_code' in schema but not found in $COMMANDS_H"
        fi
    done <<< "$SCHEMA_ERRORS"
else
    warn "Could not find commands.h to validate error codes"
fi

# ============================================================================
# VALIDATE CONFIG SCHEMA
# ============================================================================
info ""
info "Validating config schema..."

# Check that key config options exist in parser
# Note: channel0-3 are parsed dynamically, not as individual string checks
CONFIG_OPTIONS="path shader mode duration transition shader_speed shader_fps vsync show_fps"
CONFIG_C="$SRC_DIR/neowalld/config/config.c"

for option in $CONFIG_OPTIONS; do
    if grep -q "\"$option\"" "$CONFIG_C"; then
        success "Config option '$option' parsed in config.c"
    else
        error "Config option '$option' in schema but not parsed in $CONFIG_C"
    fi
done

# ============================================================================
# VALIDATE WALLPAPER MODES
# ============================================================================
info ""
info "Validating wallpaper modes..."

# Extract modes from schema
SCHEMA_MODES=$(grep -A1 "^\s*[a-z]\+\s*{" "$SCHEMA_DIR/config.vibe" | \
    grep -B1 "code [0-9]" | grep "^\s*[a-z]\+\s*{" | \
    sed 's/\s*{\s*$//' | sed 's/^\s*//' | grep -E "^(center|stretch|fit|fill|tile)$" | sort -u)

# Check mode_mappings in config.c
while IFS= read -r mode; do
    if grep -q "MODE_$(echo $mode | tr '[:lower:]' '[:upper:]')" "$CONFIG_C"; then
        success "Mode '$mode' defined in code"
    else
        error "Mode '$mode' in schema but not in config.c mode_mappings"
    fi
done <<< "$SCHEMA_MODES"

# ============================================================================
# VALIDATE TRANSITION TYPES
# ============================================================================
info ""
info "Validating transition types..."

# Extract transitions from schema
SCHEMA_TRANSITIONS=$(grep -A1 "^\s*[a-z-]\+\s*{" "$SCHEMA_DIR/config.vibe" | \
    grep -B1 "code [0-9]" | grep "^\s*[a-z-]\+\s*{" | \
    sed 's/\s*{\s*$//' | sed 's/^\s*//' | \
    grep -E "^(none|fade|slide-left|slide-right|glitch|pixelate)$" | sort -u)

# Check transition_mappings in config.c
while IFS= read -r transition; do
    ENUM_NAME="TRANSITION_$(echo $transition | tr '[:lower:]' '[:upper:]' | tr '-' '_')"
    if grep -q "$ENUM_NAME" "$CONFIG_C"; then
        success "Transition '$transition' defined in code"
    else
        error "Transition '$transition' in schema but not in config.c transition_mappings"
    fi
done <<< "$SCHEMA_TRANSITIONS"

# ============================================================================
# VALIDATE STATE STRUCTURE
# ============================================================================
info ""
info "Validating state structure atomic fields..."

# Check that atomic fields mentioned in schema exist in neowall.h
NEOWALL_H="$INCLUDE_DIR/neowall.h"
ATOMIC_FIELDS="next_requested prev_requested paused shader_paused shader_speed"

if [ -f "$NEOWALL_H" ]; then
    for field in $ATOMIC_FIELDS; do
        if grep -q "\b$field\b" "$NEOWALL_H"; then
            success "Atomic field '$field' exists in neowall.h"
        else
            error "Atomic field '$field' mentioned in schema but not found in neowall.h"
        fi
    done
else
    error "Could not find neowall.h"
fi

# ============================================================================
# SUMMARY
# ============================================================================
echo ""
echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}Validation Summary${NC}"
echo -e "${BLUE}========================================${NC}"

if [ $ERRORS -eq 0 ] && [ $WARNINGS -eq 0 ]; then
    echo -e "${GREEN}✓ All validations passed!${NC}"
    echo -e "${GREEN}✓ Code matches schema definitions${NC}"
    exit 0
elif [ $ERRORS -eq 0 ]; then
    echo -e "${YELLOW}⚠ $WARNINGS warning(s) found${NC}"
    echo -e "${GREEN}✓ No errors - code is valid${NC}"
    exit 0
else
    echo -e "${RED}✗ $ERRORS error(s) found${NC}"
    echo -e "${YELLOW}⚠ $WARNINGS warning(s) found${NC}"
    echo ""
    echo -e "${RED}Schema validation failed!${NC}"
    echo -e "${YELLOW}Please update schema or code to match.${NC}"
    exit 1
fi
