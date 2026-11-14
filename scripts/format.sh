#!/bin/sh
# Format our C sources, or with --check fail if any would change.
# Vendored code (stb, vibe.h) is left alone.
cd "$(dirname "$0")/.." || exit 1

files=$(git ls-files 'src/*.c' 'src/*.h' 'include/*.h' 'tests/*.c' 'tests/*.h' |
        grep -vE '/vendor/|core/vibe\.h$')

if [ "$1" = "--check" ]; then
    fail=0
    for f in $files; do
        if ! clang-format --dry-run --Werror "$f" >/dev/null 2>&1; then
            echo "needs formatting: $f"
            fail=1
        fi
    done
    exit $fail
fi

echo "$files" | xargs clang-format -i
