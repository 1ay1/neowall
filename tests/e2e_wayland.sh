#!/bin/sh
# End to end: the real neowall daemon on a real display server (Wayland or,
# with NEOWALL_E2E_X11=1, X11 via Xvfb or rootful Xwayland), driven
# through every control command.
#
# Starts a private compositor (sway headless, or a nested Hyprland when
# NEOWALL_E2E_HYPRLAND=1), runs neowall against it with a throwaway HOME and
# runtime dir, and checks what the CLI answers and what the daemon does:
# start-up, ping, status, cycling (next/set/list), pause/resume, shader
# pause/resume, reload with a good and a bad config, a terminal wallpaper
# (spawn, swap, no orphaned children), unknown commands, kill, and a clean
# exit. Skips (77) when no suitable compositor is installed.
#
# Usage: e2e_wayland.sh <neowall binary> <source root>

set -u
nw=${1:?neowall binary}
src=${2:?source root}
SKIP=77

tmp=$(mktemp -d /tmp/nw-e2e-XXXXXX)
export HOME="$tmp/home" XDG_RUNTIME_DIR="$tmp/run" XDG_CONFIG_HOME="$tmp/home/.config"
export XDG_DATA_DIRS="$tmp/share" XDG_STATE_HOME="$tmp/state"
mkdir -p "$HOME" "$XDG_RUNTIME_DIR" "$XDG_STATE_HOME" "$tmp/share/neowall" "$tmp/walls"
chmod 700 "$XDG_RUNTIME_DIR"
# Software GL everywhere, so this behaves the same on every machine and CI.
export __EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/50_mesa.json
export LIBGL_ALWAYS_SOFTWARE=1 WLR_RENDERER=pixman WLR_BACKENDS=headless WLR_LIBINPUT_NO_DEVICES=1
# llvmpipe's worker threads aren't TSan-instrumented; keep its rendering on
# the calling thread so a sanitizer build reports only neowall's own races.
export LP_NUM_THREADS=0
ln -s "$src/examples/shaders" "$tmp/share/neowall/shaders"

comp_pid=""
nw_pid=""
cleanup() {
    [ -n "$nw_pid" ] && kill "$nw_pid" 2>/dev/null
    if [ -n "$comp_pid" ]; then
        # Ask nicely first; Hyprland aborts on a bare SIGTERM.
        if [ -n "${hypr_sig:-}" ]; then
            HYPRLAND_INSTANCE_SIGNATURE=$hypr_sig hyprctl dispatch exit >/dev/null 2>&1
        else
            kill "$comp_pid" 2>/dev/null
        fi
        n=50; while [ $n -gt 0 ] && kill -0 "$comp_pid" 2>/dev/null; do sleep 0.1; n=$((n - 1)); done
        kill -9 "$comp_pid" 2>/dev/null
    fi
    wait 2>/dev/null
    [ -n "${KEEP:-}" ] && cp "$tmp/comp.log" "$tmp/nw.log" /tmp/nwtest/ 2>/dev/null; cp "$tmp"/home/.cache/hyprland/hyprlandCrashReport*.txt /tmp/nwtest/crash.txt 2>/dev/null; rm -rf "$tmp"
}
trap cleanup EXIT INT TERM

fails=0
checks=0
ok()   { checks=$((checks + 1)); printf '  ok   %s\n' "$1"; }
bad()  { checks=$((checks + 1)); fails=$((fails + 1)); printf '  FAIL %s\n' "$1"; [ -n "${2:-}" ] && printf '       %s\n' "$2"; }
expect() {  # expect <desc> <want-rc> <grep-pattern> <cmd...>
    desc=$1 want=$2 pat=$3; shift 3
    out=$("$@" 2>&1); rc=$?
    if [ "$rc" -eq "$want" ] && printf '%s' "$out" | grep -q -E -- "$pat"; then ok "$desc"
    else bad "$desc" "rc=$rc out=$(printf '%s' "$out" | tr '\n' ' ' | cut -c1-200)"; fi
}
wait_for() {  # wait_for <seconds> <cmd...>
    n=$(( $1 * 10 )); shift
    while [ "$n" -gt 0 ]; do "$@" >/dev/null 2>&1 && return 0; sleep 0.1; n=$((n - 1)); done
    return 1
}

# ---- display server -----------------------------------------------------------
before=$(ls "$XDG_RUNTIME_DIR" 2>/dev/null)
x11=0
if [ "${NEOWALL_E2E_X11:-0}" = 1 ]; then
    # The X11 backend: Xvfb, or a rootful Xwayland window in the caller's
    # Wayland session. Hotplug and occlusion are Wayland-only checks.
    x11=1
    dpy=":$(( $$ % 400 + 100 ))"
    if command -v Xvfb >/dev/null; then
        Xvfb "$dpy" -screen 0 640x360x24 +extension RANDR >"$tmp/comp.log" 2>&1 &
    elif command -v Xwayland >/dev/null && [ -n "${WAYLAND_DISPLAY:-}" ]; then
        parent="${REAL_XDG_RUNTIME_DIR:-/run/user/$(id -u)}/$WAYLAND_DISPLAY"
        ln -s "$parent" "$XDG_RUNTIME_DIR/parent-wayland"
        env -u __EGL_VENDOR_LIBRARY_FILENAMES -u LIBGL_ALWAYS_SOFTWARE WAYLAND_DISPLAY=parent-wayland \
            Xwayland "$dpy" -geometry 640x360 -noreset >"$tmp/comp.log" 2>&1 &
    else
        echo "skip: no Xvfb or Xwayland"; exit $SKIP
    fi
    comp_pid=$!
    if ! wait_for 15 sh -c "kill -0 $comp_pid && [ -S /tmp/.X11-unix/X${dpy#:} ]"; then
        echo "skip: X server didn't start"; tail -3 "$tmp/comp.log"; exit $SKIP
    fi
    export DISPLAY=$dpy
    unset WAYLAND_DISPLAY
    nw_env() { env -u WAYLAND_DISPLAY DISPLAY="$DISPLAY" "$@"; }
    echo "X server up on $DISPLAY"
elif [ "${NEOWALL_E2E_HYPRLAND:-0}" = 1 ] && command -v Hyprland >/dev/null && [ -n "${WAYLAND_DISPLAY:-}" ]; then
    # Nested in the caller's session: its socket must be reachable from ours,
    # and Hyprland needs the real GPU driver, not the forced software GL.
    parent="${REAL_XDG_RUNTIME_DIR:-/run/user/$(id -u)}/$WAYLAND_DISPLAY"
    # Under its own name, so the nested compositor's wayland-N is free.
    parent_name=parent-wayland
    ln -s "$parent" "$XDG_RUNTIME_DIR/$parent_name"
    before=$(ls "$XDG_RUNTIME_DIR" 2>/dev/null)
    printf 'monitor = , 640x360@60, 0x0, 1\nmisc {\n  disable_hyprland_logo = true\n  disable_splash_rendering = true\n}\n' > "$tmp/hypr.conf"
    env -u HYPRLAND_INSTANCE_SIGNATURE -u __EGL_VENDOR_LIBRARY_FILENAMES -u LIBGL_ALWAYS_SOFTWARE \
        -u WLR_RENDERER -u WLR_BACKENDS -u LP_NUM_THREADS WAYLAND_DISPLAY=$parent_name \
        Hyprland -c "$tmp/hypr.conf" >"$tmp/comp.log" 2>&1 &
    comp_pid=$!
elif command -v sway >/dev/null; then
    unset WAYLAND_DISPLAY DISPLAY
    printf 'output HEADLESS-1 mode 640x360\n' > "$tmp/sway.conf"
    sway -c "$tmp/sway.conf" >"$tmp/comp.log" 2>&1 &
    comp_pid=$!
else
    echo "skip: no sway (or Hyprland with NEOWALL_E2E_HYPRLAND=1)"
    exit $SKIP
fi
if [ "$x11" = 0 ]; then
sock_ready() {
    kill -0 "$comp_pid" 2>/dev/null || return 1   # never fall back to another compositor
    for s in "$XDG_RUNTIME_DIR"/wayland-*; do
        case "$s" in *.lock) continue ;; esac
        [ -S "$s" ] || [ -L "$s" ] || continue
        name=$(basename "$s")
        [ "$name" = "${parent_name:-}" ] && continue
        echo "$before" | grep -qx "$name" && continue
        echo "$name"; return 0
    done
    return 1
}
if ! wait_for 15 sock_ready; then
    echo "skip: compositor didn't start"; tail -3 "$tmp/comp.log"; exit $SKIP
fi
export WAYLAND_DISPLAY=$(sock_ready)

# The daemon must only ever see our compositor, never the user's session.
nw_env() { env -u DISPLAY WAYLAND_DISPLAY="$WAYLAND_DISPLAY" "$@"; }
if [ "${NEOWALL_E2E_HYPRLAND:-0}" = 1 ]; then
    hypr_sig=$(ls -t "$XDG_RUNTIME_DIR/hypr" 2>/dev/null | head -1)
fi
echo "compositor up on $WAYLAND_DISPLAY"
fi

# ---- a config that cycles three shaders --------------------------------------
cfg="$tmp/config.vibe"
cat > "$cfg" <<EOF
default {
  shader $src/examples/shaders/
  duration 3600
  shuffle false
}
EOF
cat > "$tmp/bad.vibe" <<EOF
default {
  shader_speed -1
  shader garden.glsl
}
EOF

# ---- start ------------------------------------------------------------------
nw_env "$nw" -f -c "$cfg" >"$tmp/nw.log" 2>&1 &
nw_pid=$!
if wait_for 20 "$nw" ping; then ok "daemon starts and answers ping"
else bad "daemon starts and answers ping" "$(tail -5 "$tmp/nw.log" | tr '\n' ' ')"; exit 1; fi
# the daemon's own pid (env may sit between it and us)
nw_pid=$("$nw" ping | sed -n 's/.* pid \([0-9]*\).*/\1/p')
kill -0 "$comp_pid" 2>/dev/null && ok "test compositor still running" \
    || { bad "test compositor still running" "it died; refusing to test against anything else"; exit 1; }

expect "ping names the version and pid" 0 "neowall .* pid $nw_pid" "$nw" ping
wait_for 10 grep -q 'FPS \[' "$tmp/nw.log" && ok "frames are being drawn" || bad "frames are being drawn"
expect "status shows the output"        0 "Output:"                 "$nw" current

# ---- cycling ----------------------------------------------------------------
first=$(grep -o 'Setting shader for output [^:]*: .*' "$tmp/nw.log" | tail -1)
expect "next skips"                     0 "skipping to next"        "$nw" next
wait_for 5 sh -c "grep -o 'Setting shader for output [^:]*: .*' '$tmp/nw.log' | tail -1 | grep -v -F -x \"\$1\"" _ "$first" \
    && ok "next loads a different shader" || bad "next loads a different shader"
expect "list shows the cycle"           0 "garden"                  "$nw" list
expect "set 2 is accepted"              0 "setting wallpaper 2"     "$nw" set 2
expect "set out of range is refused"    1 "out of range"            "$nw" set 9999
expect "set non-number is refused"      1 "non-negative integer"    "$nw" set abc
expect "pause"                          0 "cycling paused"          "$nw" pause
expect "resume"                         0 "cycling resumed"         "$nw" resume
expect "pause-shader"                   0 "shader animation paused" "$nw" pause-shader
expect "resume-shader"                  0 "shader animation resumed" "$nw" resume-shader
expect "set-terminal without terminal"  1 "no output is running a terminal" "$nw" set-terminal htop

# ---- reload -----------------------------------------------------------------
expect "reload a good config"           0 "configuration reloaded"  "$nw" reload
cp "$cfg" "$tmp/good.vibe"
cp "$tmp/bad.vibe" "$cfg"
expect "reload a bad config is refused" 1 "previous configuration"  "$nw" reload
expect "daemon still alive after bad reload" 0 "pid $nw_pid"        "$nw" ping
cp "$tmp/good.vibe" "$cfg"

# ---- terminal wallpaper -------------------------------------------------------
# A real program under a PTY; set-terminal swaps it and the old child must be
# gone (no orphans), and leaving terminal mode stops the child too.
cat > "$cfg" <<EOF
default {
  terminal "sleep 600"
}
EOF
expect "reload into a terminal wallpaper" 0 "configuration reloaded" "$nw" reload
wait_for 10 pgrep -f '^sleep 600$' && ok "terminal child is running" || bad "terminal child is running"
old_child=$(pgrep -n -f '^sleep 600$')
expect "set-terminal swaps the program"  0 "swapping terminal"       "$nw" set-terminal "sleep 700"
wait_for 10 pgrep -f '^sleep 700$' && ok "new terminal child runs" || bad "new terminal child runs"
wait_for 10 sh -c "! kill -0 ${old_child:-0} 2>/dev/null" && ok "old terminal child is gone" \
    || bad "old terminal child is gone" "pid $old_child still alive"
new_child=$(pgrep -n -f '^sleep 700$')
cp "$tmp/good.vibe" "$cfg"
expect "reload back to shaders"         0 "configuration reloaded"  "$nw" reload
wait_for 10 sh -c "! kill -0 ${new_child:-0} 2>/dev/null" && ok "terminal child stopped on reload" \
    || bad "terminal child stopped on reload" "pid $new_child still alive"

# ---- monitor hotplug ----------------------------------------------------------
# Add an output while running: it must come up with its own config block, and
# removing it must not take the daemon down. Needs a compositor that can add
# outputs at runtime (hyprctl / swaymsg).
hot_cmd=""
if [ "$x11" = 1 ]; then
    :
elif [ -n "${hypr_sig:-}" ]; then
    # nested Hyprland gives "headless" outputs no mode; a wayland one gets a real size
    hot_add()    { HYPRLAND_INSTANCE_SIGNATURE=$hypr_sig hyprctl output create wayland NW-HOT >/dev/null; }
    hot_remove() { HYPRLAND_INSTANCE_SIGNATURE=$hypr_sig hyprctl output remove NW-HOT >/dev/null; }
    hot_cmd=hyprctl
elif command -v swaymsg >/dev/null; then
    hot_add()    { swaymsg create_output >/dev/null; }
    hot_remove() { swaymsg output HEADLESS-2 unplug >/dev/null; }
    hot_cmd=swaymsg
fi
if [ -n "$hot_cmd" ]; then
    cat > "$cfg" <<EOF
default {
  shader $src/examples/shaders/
  duration 3600
  shuffle false
}
output {
  NW-HOT { shader $src/examples/shaders/plasma_touch.glsl }
  HEADLESS-2 { shader $src/examples/shaders/plasma_touch.glsl }
}
EOF
    expect "reload with a block for the new output" 0 "configuration reloaded" "$nw" reload
    hot_add
    if wait_for 15 grep -q 'Reconnect: output .* online' "$tmp/nw.log"; then
        ok "hotplugged output comes online"
    else
        bad "hotplugged output comes online" "$(grep -E 'Reconnect|New output' "$tmp/nw.log" | tail -3 | tr '\n' ' ')"
    fi
    wait_for 10 grep -q -E '\[output\.(NW-HOT|HEADLESS-2)\] SHADER MODE: .*plasma_touch' "$tmp/nw.log" \
        && ok "it gets its own config block" || bad "it gets its own config block"
    expect "status lists both outputs"   0 "NW-HOT|HEADLESS-2"        "$nw" current
    hot_remove
    sleep 1
    expect "daemon survives the unplug"  0 "pid $nw_pid"              "$nw" ping
    hot_add
    wait_for 15 sh -c "[ \$(grep -c 'Reconnect: output .* online' '$tmp/nw.log') -ge 2 ]" \
        && ok "replugged output comes back" || bad "replugged output comes back"
    hot_remove
    # An output that shows up before it has a size (a headless output in
    # nested Hyprland stays 0x0 until a mode is set) must wait, then come up
    # once the compositor sizes it, not be dropped.
    if [ -n "${hypr_sig:-}" ]; then
        H() { HYPRLAND_INSTANCE_SIGNATURE=$hypr_sig hyprctl "$@" >/dev/null; }
        before_online=$(grep -c 'Reconnect: output .* online' "$tmp/nw.log")
        H output create headless NW-LATE
        wait_for 10 grep -q 'NW-LATE has no size yet' "$tmp/nw.log" \
            && ok "unsized output waits" || bad "unsized output waits"
        H output remove NW-LATE
        H output create wayland NW-LATE
        wait_for 15 sh -c "[ \$(grep -c 'Reconnect: output .* online' '$tmp/nw.log') -gt $before_online ]" \
            && ok "it comes up once sized" || bad "it comes up once sized"
        H output remove NW-LATE
    fi
    cp "$tmp/good.vibe" "$cfg"
    expect "daemon still answers"        0 "pid $nw_pid"              "$nw" ping
else
    echo "  skip hotplug: no hyprctl/swaymsg (or X11)"
fi

# ---- errors and stop ----------------------------------------------------------
expect "unknown command"                1 "Unknown command"         "$nw" frobnicate
expect "kill"                           0 "stopping|Stopping|killed|Sent" "$nw" kill
if wait_for 10 sh -c "! kill -0 $nw_pid"; then ok "daemon exits after kill"; else bad "daemon exits after kill"; fi
grep -q 'NeoWall terminated successfully' "$tmp/nw.log" && ok "clean shutdown" \
    || bad "clean shutdown" "$(tail -3 "$tmp/nw.log" | tr '\n' ' ')"
nw_pid=""
expect "ping with no daemon"            1 "."                       "$nw" ping
[ ! -e "$XDG_RUNTIME_DIR/neowall.sock" ] && ok "control socket removed" || bad "control socket removed"
grep -q -E 'ERROR: AddressSanitizer|runtime error|ThreadSanitizer' "$tmp/nw.log" \
    && bad "no sanitizer reports" "$(grep -m1 -E 'Sanitizer|runtime error' "$tmp/nw.log")" \
    || ok "no sanitizer reports"

echo "e2e_wayland: $checks checks, $fails failures"
[ "$fails" -eq 0 ] && kill -0 "$comp_pid" 2>/dev/null || { [ "$fails" -eq 0 ] && echo "compositor died during the run"; exit 1; }
