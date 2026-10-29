#!/bin/sh
# Fail if a non-static function in src/ has no caller anywhere in src/ or
# tests/. Such functions are dead code that still has to compile, be read and
# be kept in sync; delete them (or make them static if they're helpers).
#
# Usage: scripts/check-unused.sh [source root]
set -eu
cd "${1:-$(dirname "$0")/..}"

found=0
for f in $(git ls-files 'src/*.c' | grep -v -E '/vendor/'); do
    # function definitions at column 0 that aren't static
    grep -o -E '^[a-z][a-zA-Z_0-9 *]*\b[a-z_][a-z_0-9]*\(' "$f" \
        | grep -v -E '^(static |return|if|for|while|switch|else)' \
        | sed -E 's/.*\b([a-z_][a-z_0-9]*)\($/\1/' \
        | while read -r fn; do
            case "$fn" in main|LLVMFuzzerTestOneInput) continue ;; esac
            n=$(grep -rn -w "$fn" src tests include --include='*.c' --include='*.inc' --include='*.h' | wc -l)
            # a prototype in a header plus its definition is still unused, unless
            # a header macro calls it (nw_list_push -> nw_list_grow_)
            m=$(grep -rn -w "$fn" include src --include='*.h' | grep -c '#define\|\\$' || true)
            n=$(( n - $(grep -rln -w "$fn" include src --include='*.h' | wc -l) + m ))
            if [ "$n" -le 1 ]; then
                echo "unused: $fn ($f)"
            fi
        done
done > /tmp/nw-unused.$$

if [ -s /tmp/nw-unused.$$ ]; then
    cat /tmp/nw-unused.$$
    found=$(wc -l < /tmp/nw-unused.$$)
fi
rm -f /tmp/nw-unused.$$
[ "$found" -eq 0 ] && echo "no unused functions"
[ "$found" -eq 0 ]
