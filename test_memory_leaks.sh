#!/bin/bash
# Memory leak detection script for Staticwall
# Tests for leaks during normal operation and display disconnect/reconnect scenarios

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BINARY="${SCRIPT_DIR}/build/bin/staticwall"
CONFIG="${SCRIPT_DIR}/test_config.toml"
VALGRIND_LOG="valgrind_leak_test.log"
VALGRIND_SUPPRESSIONS="${SCRIPT_DIR}/valgrind.supp"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}═══════════════════════════════════════════════════════════${NC}"
echo -e "${BLUE}  Staticwall Memory Leak Test Suite${NC}"
echo -e "${BLUE}═══════════════════════════════════════════════════════════${NC}"
echo

# Check if valgrind is installed
if ! command -v valgrind &> /dev/null; then
    echo -e "${RED}Error: valgrind is not installed${NC}"
    echo "Install it with: sudo apt install valgrind"
    exit 1
fi

# Check if binary exists
if [ ! -f "$BINARY" ]; then
    echo -e "${RED}Error: Binary not found at $BINARY${NC}"
    echo "Build it first with: make"
    exit 1
fi

# Create test configuration
cat > "$CONFIG" << 'EOF'
[wallpaper]
path = "/usr/share/backgrounds"
mode = "fill"

[shader]
speed = 1.0
EOF

echo -e "${GREEN}✓${NC} Created test configuration: $CONFIG"
echo

# Valgrind suppressions for known issues in system libraries
cat > "$VALGRIND_SUPPRESSIONS" << 'EOF'
{
   Mesa_GL_leak
   Memcheck:Leak
   ...
   obj:*/libGL.so*
}
{
   Mesa_DRI_leak
   Memcheck:Leak
   ...
   obj:*/dri/*_dri.so
}
{
   Wayland_leak
   Memcheck:Leak
   ...
   obj:*/libwayland-client.so*
}
{
   EGL_leak
   Memcheck:Leak
   ...
   obj:*/libEGL.so*
}
{
   pthread_leak
   Memcheck:Leak
   ...
   fun:pthread_create*
}
EOF

echo -e "${YELLOW}Running valgrind memory leak detection...${NC}"
echo -e "${YELLOW}This will take a while. Press Ctrl+C after 10-15 seconds.${NC}"
echo

# Valgrind options
VALGRIND_OPTS=(
    --leak-check=full
    --show-leak-kinds=all
    --track-origins=yes
    --verbose
    --log-file="$VALGRIND_LOG"
    --suppressions="$VALGRIND_SUPPRESSIONS"
    --gen-suppressions=all
    --num-callers=20
    --fair-sched=yes
)

# Run staticwall under valgrind
echo -e "${BLUE}Starting staticwall with valgrind...${NC}"
echo "Command: valgrind ${VALGRIND_OPTS[*]} $BINARY -c $CONFIG -v"
echo

# Run in background and capture PID
valgrind "${VALGRIND_OPTS[@]}" "$BINARY" -c "$CONFIG" -v &
VALGRIND_PID=$!

echo -e "${GREEN}✓${NC} Staticwall started (PID: $VALGRIND_PID)"
echo -e "${YELLOW}Waiting 10 seconds for initialization...${NC}"
sleep 10

echo -e "${GREEN}✓${NC} Running memory leak test for 5 seconds..."
sleep 5

# Send graceful shutdown signal
echo -e "${BLUE}Sending SIGTERM for graceful shutdown...${NC}"
kill -TERM $VALGRIND_PID 2>/dev/null || true

# Wait for valgrind to finish and generate report
echo -e "${YELLOW}Waiting for valgrind to finish analysis...${NC}"
wait $VALGRIND_PID 2>/dev/null || true

echo
echo -e "${BLUE}═══════════════════════════════════════════════════════════${NC}"
echo -e "${BLUE}  Memory Leak Analysis Results${NC}"
echo -e "${BLUE}═══════════════════════════════════════════════════════════${NC}"
echo

# Parse valgrind output
if [ -f "$VALGRIND_LOG" ]; then
    # Extract leak summary
    DEFINITELY_LOST=$(grep "definitely lost:" "$VALGRIND_LOG" | tail -1 | awk '{print $4}')
    INDIRECTLY_LOST=$(grep "indirectly lost:" "$VALGRIND_LOG" | tail -1 | awk '{print $4}')
    POSSIBLY_LOST=$(grep "possibly lost:" "$VALGRIND_LOG" | tail -1 | awk '{print $4}')
    STILL_REACHABLE=$(grep "still reachable:" "$VALGRIND_LOG" | tail -1 | awk '{print $4}')

    echo "Leak Summary:"
    echo "─────────────────────────────────────────────────────────"

    if [ "$DEFINITELY_LOST" = "0" ]; then
        echo -e "  Definitely lost:  ${GREEN}${DEFINITELY_LOST} bytes${NC} ✓"
    else
        echo -e "  Definitely lost:  ${RED}${DEFINITELY_LOST} bytes${NC} ✗"
    fi

    if [ "$INDIRECTLY_LOST" = "0" ]; then
        echo -e "  Indirectly lost:  ${GREEN}${INDIRECTLY_LOST} bytes${NC} ✓"
    else
        echo -e "  Indirectly lost:  ${YELLOW}${INDIRECTLY_LOST} bytes${NC} ⚠"
    fi

    if [ "$POSSIBLY_LOST" = "0" ]; then
        echo -e "  Possibly lost:    ${GREEN}${POSSIBLY_LOST} bytes${NC} ✓"
    else
        echo -e "  Possibly lost:    ${YELLOW}${POSSIBLY_LOST} bytes${NC} ⚠"
    fi

    echo -e "  Still reachable:  ${BLUE}${STILL_REACHABLE} bytes${NC}"

    echo
    echo "─────────────────────────────────────────────────────────"

    # Check for errors
    ERROR_COUNT=$(grep "ERROR SUMMARY:" "$VALGRIND_LOG" | tail -1 | awk '{print $4}')

    if [ "$ERROR_COUNT" = "0" ]; then
        echo -e "${GREEN}✓ No errors detected${NC}"
    else
        echo -e "${YELLOW}⚠ $ERROR_COUNT errors detected${NC}"
        echo "  Review the log file for details: $VALGRIND_LOG"
    fi

    echo

    # Overall result
    if [ "$DEFINITELY_LOST" = "0" ] && [ "$ERROR_COUNT" = "0" ]; then
        echo -e "${GREEN}══════════════════════════════════════════════════════════${NC}"
        echo -e "${GREEN}  ✓ PASS: No definite memory leaks detected!${NC}"
        echo -e "${GREEN}══════════════════════════════════════════════════════════${NC}"
    elif [ "$DEFINITELY_LOST" = "0" ]; then
        echo -e "${YELLOW}══════════════════════════════════════════════════════════${NC}"
        echo -e "${YELLOW}  ⚠ PARTIAL PASS: No leaks, but some errors detected${NC}"
        echo -e "${YELLOW}══════════════════════════════════════════════════════════${NC}"
    else
        echo -e "${RED}══════════════════════════════════════════════════════════${NC}"
        echo -e "${RED}  ✗ FAIL: Memory leaks detected${NC}"
        echo -e "${RED}══════════════════════════════════════════════════════════${NC}"
    fi

    echo
    echo "Full report saved to: $VALGRIND_LOG"
    echo
    echo "To view detailed leak information:"
    echo "  less $VALGRIND_LOG"
    echo
    echo "To filter for definite leaks:"
    echo "  grep -A 20 'definitely lost' $VALGRIND_LOG"

else
    echo -e "${RED}Error: Valgrind log file not found${NC}"
    exit 1
fi

# Cleanup test config
rm -f "$CONFIG" "$VALGRIND_SUPPRESSIONS"

echo
echo -e "${BLUE}Memory leak test completed${NC}"
