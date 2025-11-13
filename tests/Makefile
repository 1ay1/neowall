# NeoWall Configuration Rules Test Suite - Standalone Makefile
# Links against already-compiled objects from the main build

CC = gcc
CFLAGS = -Wall -Wextra -g -O0 -DTEST_BUILD
INCLUDES = -I../include -I../src -I../src/neowalld -I../src/neowalld/config
LDFLAGS = -pthread

# Paths to build directory objects
BUILD_DIR = ../build/src/neowalld/neowalld.p
CONFIG_RULES_OBJ = $(BUILD_DIR)/config_config_rules.c.o

# Test executable
TEST_BIN = test_config_rules

.PHONY: all clean test run help check-build

all: check-build $(TEST_BIN)

# Check if build directory exists
check-build:
	@if [ ! -d "$(BUILD_DIR)" ]; then \
		echo ""; \
		echo "Error: Build directory not found!"; \
		echo "Please build the main project first:"; \
		echo "  cd .. && meson setup build && meson compile -C build"; \
		echo ""; \
		exit 1; \
	fi
	@if [ ! -f "$(CONFIG_RULES_OBJ)" ]; then \
		echo ""; \
		echo "Error: config_rules.c.o not found!"; \
		echo "Please rebuild the main project:"; \
		echo "  cd .. && meson compile -C build"; \
		echo ""; \
		exit 1; \
	fi

# Build the test executable - link against pre-built config_rules.o
$(TEST_BIN): test_config_rules.c $(CONFIG_RULES_OBJ)
	@echo ""
	@echo "═══════════════════════════════════════════════════════════"
	@echo "  Building NeoWall Config Rules Test Suite"
	@echo "═══════════════════════════════════════════════════════════"
	@echo ""
	@echo "Compiling and linking test executable..."
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ test_config_rules.c $(CONFIG_RULES_OBJ) $(LDFLAGS)
	@echo ""
	@echo "✓ Successfully built $@"
	@echo ""

# Run tests
test: $(TEST_BIN)
	@echo ""
	@echo "═══════════════════════════════════════════════════════════"
	@echo "  Running NeoWall Config Rules Test Suite"
	@echo "═══════════════════════════════════════════════════════════"
	@echo ""
	./$(TEST_BIN)

# Alias for test
run: test

# Run with valgrind for memory leak detection
valgrind: $(TEST_BIN)
	@echo ""
	@echo "═══════════════════════════════════════════════════════════"
	@echo "  Running Tests with Valgrind"
	@echo "═══════════════════════════════════════════════════════════"
	@echo ""
	valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes \
		--verbose --log-file=valgrind-out.txt ./$(TEST_BIN)
	@echo ""
	@echo "Valgrind output saved to valgrind-out.txt"
	@echo ""

# Quick test (no output buffering)
quick: $(TEST_BIN)
	@./$(TEST_BIN) 2>&1 | cat

# Clean build artifacts
clean:
	@echo "Cleaning up test artifacts..."
	@rm -f $(TEST_BIN)
	@rm -f valgrind-out.txt
	@rm -f *.log
	@echo "✓ Clean complete"

# Clean everything including asking to rebuild main project
distclean: clean
	@echo ""
	@echo "To rebuild everything from scratch:"
	@echo "  cd .. && rm -rf build && meson setup build && meson compile -C build"
	@echo ""

# Help message
help:
	@echo ""
	@echo "╔════════════════════════════════════════════════════════════════╗"
	@echo "║  NeoWall Config Rules Test Suite                              ║"
	@echo "╚════════════════════════════════════════════════════════════════╝"
	@echo ""
	@echo "PREREQUISITES:"
	@echo "  The main project must be built first:"
	@echo "    cd .. && meson setup build && meson compile -C build"
	@echo ""
	@echo "USAGE:"
	@echo "  make           - Build test executable"
	@echo "  make test      - Build and run all tests"
	@echo "  make run       - Same as 'make test'"
	@echo "  make quick     - Run tests with unbuffered output"
	@echo "  make valgrind  - Run tests with memory leak detection"
	@echo "  make clean     - Remove test artifacts"
	@echo "  make distclean - Clean and show rebuild instructions"
	@echo "  make help      - Show this help message"
	@echo ""
	@echo "EXAMPLES:"
	@echo "  make clean test              - Clean and run tests"
	@echo "  make && ./test_config_rules  - Build and run manually"
	@echo "  make valgrind                - Check for memory leaks"
	@echo ""
	@echo "TROUBLESHOOTING:"
	@echo "  If you get 'Build directory not found' error:"
	@echo "    cd .. && meson setup build && meson compile -C build"
	@echo ""
	@echo "  If you get linking errors:"
	@echo "    cd .. && meson compile -C build"
	@echo "    cd tests && make clean && make"
	@echo ""
