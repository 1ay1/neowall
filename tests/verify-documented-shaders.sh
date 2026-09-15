#!/bin/sh
# Guard: every shader named in the README must actually ship.
#
# Issue #79: the README advertised `ocean_waves`, `aurora` and `plasma`, none of
# which existed in examples/shaders/. A user copied the name straight out of the
# README into their config and neowall failed to start. Documentation drift is
# invisible to a compiler, so it needs a test.
#
# Extracts every `backtick-quoted` name from the README's shader table and
# asserts a matching examples/shaders/<name>.glsl exists.
#
# Usage: verify-documented-shaders.sh [source-root]

set -eu

root="${1:-$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)}"
readme="$root/README.md"
shader_dir="$root/examples/shaders"

if [ ! -f "$readme" ]; then
    echo "FAIL: README not found at $readme" >&2
    exit 1
fi

if [ ! -d "$shader_dir" ]; then
    echo "FAIL: shader directory not found at $shader_dir" >&2
    exit 1
fi

# The shader table rows start with "| <Vibe> |" and list `name` entries. Pull
# the table out first so unrelated backticks elsewhere in the README (config
# keys, commands) are not mistaken for shader names.
table=$(awk '
    /^\| *Vibe *\| *A few *\|/ { in_table = 1; next }
    in_table && /^\|/          { print; next }
    in_table                   { exit }
' "$readme")

if [ -z "$table" ]; then
    echo "FAIL: could not locate the shader table in README.md" >&2
    echo "      (expected a table whose header row is '| Vibe | A few |')" >&2
    exit 1
fi

names=$(printf '%s\n' "$table" | grep -o '`[a-zA-Z0-9_]*`' | tr -d '`' | sort -u)

if [ -z "$names" ]; then
    echo "FAIL: shader table contains no \`shader\` names" >&2
    exit 1
fi

missing=""
count=0
for name in $names; do
    count=$((count + 1))
    if [ ! -f "$shader_dir/$name.glsl" ]; then
        missing="$missing $name"
    fi
done

if [ -n "$missing" ]; then
    echo "FAIL: README documents shaders that are not shipped:" >&2
    for name in $missing; do
        echo "  - $name (expected $shader_dir/$name.glsl)" >&2
    done
    echo "" >&2
    echo "Either ship the shader or correct the table in README.md." >&2
    exit 1
fi

echo "documented_shaders: $count shader(s) referenced by README, all present"
exit 0
