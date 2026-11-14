#!/bin/sh
# Fail if a module includes a header from a layer above it.
#
#   core     -> nothing (libc only)
#   runtime  -> core
#   app      -> anything (it's the top)
#
# The point of the layers: rt can be tested and reused without neowall,
# and core without either.
cd "$(dirname "$0")/.." || exit 2
fail=0
check() {  # check <layer> <allowed-regex> <paths...>
    layer=$1 allowed=$2; shift 2
    bad=$(git grep -n '#include "neowall/' -- "$@" | grep -vE "\"neowall/($allowed)/")
    if [ -n "$bad" ]; then
        echo "$layer reaches above itself:"
        echo "$bad" | sed 's/^/  /'
        fail=1
    fi
}
check core    'core'         src/core include/neowall/core
check runtime 'core|runtime' src/runtime include/neowall/runtime
[ $fail -eq 0 ] && echo "layers ok"
exit $fail
