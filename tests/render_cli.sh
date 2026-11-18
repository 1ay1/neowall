#!/bin/sh
# `neowall render` end to end: renders fixtures offscreen and checks the PNG,
# the deterministic output, --at, --set, --passes, --bench and --check.
# Usage: render_cli.sh <neowall-binary> <source-root>      (exit 77 = no GPU/EGL)
set -u
NW=$1
FIX=$2/tests/fixtures/shaders
T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT
export XDG_STATE_HOME="$T/state"   # prove we never write the state file
# On Mesa's llvmpipe (CI), rasterise on one thread like tests/gl_headless.c:
# llvmpipe's worker threads race internally, which TSan reports (not ours).
# No effect on hardware drivers.
export LP_NUM_THREADS=0
fail() { echo "FAIL: $*"; exit 1; }

"$NW" render "$FIX/render_ok.glsl" -s 64x36 -t 1 -o "$T/a.png" -q
rc=$?
[ $rc -eq 77 ] && { echo "skip: no headless EGL/GL 3.3"; exit 77; }
[ $rc -eq 0 ] || fail "render exited $rc"
[ -s "$T/a.png" ] || fail "no PNG written"
head -c 8 "$T/a.png" | od -An -c | grep -q 'P   N   G' || fail "not a PNG"

# deterministic: same command (time of day pinned with --at), same bytes
"$NW" render "$FIX/render_ok.glsl" -s 64x36 -t 1 --at 12:00 -o "$T/a.png" -q || fail "render --at"
"$NW" render "$FIX/render_ok.glsl" -s 64x36 -t 1 --at 12:00 -o "$T/b.png" -q || fail "second render"
cmp -s "$T/a.png" "$T/b.png" || fail "two identical renders differ"

# time of day and pinned uniforms change the picture
"$NW" render "$FIX/render_ok.glsl" -s 64x36 -t 1 --at 03:00 -o "$T/night.png" -q || fail "--at"
"$NW" render "$FIX/render_ok.glsl" -s 64x36 -t 1 --at 21:00 -o "$T/eve.png" -q || fail "--at 2"
cmp -s "$T/night.png" "$T/eve.png" && fail "--at did not change iTimeOfDay"
"$NW" render "$FIX/render_ok.glsl" -s 64x36 -t 1 --at 03:00 --set iAudioBeat=1 -o "$T/beat.png" -q || fail "--set"
cmp -s "$T/night.png" "$T/beat.png" && fail "--set iAudioBeat=1 had no effect"

# the manifest by bare name, passes dumped, bench and check reports
cp "$FIX/render_ok.glsl" "$FIX/render_ok.neowall" "$T/"
( cd "$T" && "$NW" render render_ok --passes -s 64x36 -o p.png -q ) || fail "bare name / manifest"
[ -s "$T/p.buffer_a.png" ] || fail "--passes did not write Buffer A"
"$NW" render "$FIX/render_ok.glsl" -s 64x36 --bench --check -o "$T/c.png" > "$T/report" || fail "bench/check on a clean shader"
grep -q "Buffer A" "$T/report" && grep -q "clean" "$T/report" && grep -q "total" "$T/report" ||
    fail "bench/check report incomplete: $(cat "$T/report")"

# the NaN fixture: found, in Buffer A, exit 2
"$NW" render "$FIX/render_nan.glsl" -s 128x72 -t 0 --check -o "$T/n.png" > "$T/nan" 2>&1
rc=$?
[ $rc -eq 2 ] || fail "--check exit $rc on a NaN shader (want 2): $(cat "$T/nan")"
grep -E "Buffer A +[0-9]+ NaN" "$T/nan" >/dev/null || fail "NaN not attributed to Buffer A: $(cat "$T/nan")"

# errors are friendly
"$NW" render definitely_not_a_shader -q 2> "$T/err"; [ $? -eq 1 ] || fail "missing shader exit"
grep -q "can't find" "$T/err" || fail "missing-shader message: $(cat "$T/err")"

# a manifest uniform the author declared too: compiles, and the value arrives
"$NW" render "$FIX/render_uniform.neowall" -s 8x8 -o "$T/u.png" -q ||
    fail "self-declared manifest uniform does not compile"
"$NW" render "$FIX/render_uniform.neowall" -s 8x8 --set uLevel=1 -o "$T/u1.png" -q || fail "--set manifest uniform"
cmp -s "$T/u.png" "$T/u1.png" && fail "--set did not override a manifest uniform"

[ -e "$T/state/neowall/state" ] && fail "render wrote the daemon state file"
echo "ok - neowall render"
