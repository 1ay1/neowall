#!/bin/sh
# The shell completions name every command and template the binary knows,
# so a new verb can't ship without them.
# usage: check-completions.sh <neowall> <source-root>
nw=$1 root=$2
cd "$root/packaging/completions" || exit 1
fail=0
cmds=$(XDG_RUNTIME_DIR=/nonexistent "$nw" --help | sed -n '/Daemon Control/,/^$/p' | awk 'NR > 1 && NF { print $1 }')
tmpls=$("$nw" new --list 2>&1 | awk 'NR > 1 && NF { print $1 }')
[ -n "$cmds" ] && [ -n "$tmpls" ] || { echo "couldn't read commands from $nw"; exit 1; }
for w in $cmds $tmpls new import docs render preview watch; do
    for f in neowall.bash _neowall neowall.fish; do
        grep -qw -- "$w" "$f" || { echo "$f is missing '$w'"; fail=1; }
    done
done
[ $fail -eq 0 ] && echo "completions cover $(echo $cmds $tmpls | wc -w) words"
exit $fail
