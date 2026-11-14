#!/bin/sh
# Fail if a module includes a header from a module it isn't allowed to see.
# The modules form a strict hierarchy (no cycles); each line lists what a
# module may include besides itself. Run in CI.
#
#   core                 libc only: log, time, paths, containers, vibe, clock
#   runtime              the event loop and control socket
#   gfx textures image   GL objects, generated textures, image decoding
#   config               parse and validate the file (no outputs)
#   source shader        live values; shader model and engine
#   egl surface state    GL context; one window surface; shared daemon state
#   proto                what the CLI and the daemon agree on
#   terminal             VT emulator
#   output               a monitor and what it shows (incl. render, transitions)
#   occlusion backends   compositor/X11 implementations
#   app cli main         the program
cd "$(dirname "$0")/.." || exit 2

allowed() {  # allowed <module> -> space-separated list it may include
    case $1 in
    core)       echo "" ;;
    runtime)    echo "core" ;;
    gfx)        echo "core" ;;
    textures)   echo "core" ;;
    image)      echo "core config" ;;
    config)     echo "core" ;;
    source)     echo "core" ;;
    shader)     echo "core gfx image source textures" ;;
    egl)        echo "core state" ;;
    surface)    echo "core egl" ;;
    state)      echo "core" ;;
    proto)      echo "core state" ;;
    terminal)   echo "core" ;;
    output)     echo "core config egl image shader source state surface terminal textures" ;;
    occlusion)  echo "core state surface" ;;
    backends)   echo "core config egl output shader source state surface" ;;
    cli)        echo "core output proto runtime shader state" ;;
    app|main)   echo "*" ;;
    *)          echo "?" ;;
    esac
}

module_of() {  # path -> module
    case $1 in
    include/neowall/*) p=${1#include/neowall/} ;;
    src/*)             p=${1#src/} ;;
    esac
    case $p in
    main.c) echo main ;;
    */*)    echo "${p%%/*}" ;;
    *)      echo root ;;
    esac
}

fail=0
for f in $(git ls-files 'src/*.c' 'src/*.h' 'include/neowall/*.h' | grep -v /vendor/); do
    m=$(module_of "$f")
    ok=$(allowed "$m")
    [ "$ok" = "*" ] && continue
    if [ "$ok" = "?" ]; then echo "$f: module '$m' isn't in the layer list"; fail=1; continue; fi
    for inc in $(sed -n 's/^#include "neowall\/\([a-z_]*\)\/.*/\1/p' "$f"); do
        [ "$inc" = "$m" ] && continue
        case " $ok " in *" $inc "*) ;; *)
            echo "$f: $m may not include $inc"; fail=1 ;;
        esac
    done
done
[ $fail -eq 0 ] && echo "layers ok"
exit $fail
