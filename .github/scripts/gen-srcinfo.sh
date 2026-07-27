#!/usr/bin/env bash
#
# gen-srcinfo.sh — write .SRCINFO next to a PKGBUILD, using makepkg.
#
# Usage: gen-srcinfo.sh <dir-containing-PKGBUILD>
#
# The AUR rejects a push whose .SRCINFO disagrees with its PKGBUILD, and
# `makepkg --printsrcinfo` is the only correct way to produce it. GitHub
# runners are Ubuntu, so makepkg comes from an Arch container.
#
# The subtlety this script exists for: makepkg REFUSES to run as root, and the
# obvious fix — bind-mounting the AUR checkout and `chown -R build:build` on it
# — silently hands the repo's .git directory to a UID that does not exist on
# the host. Every subsequent git command then dies with
#
#     fatal: Unable to create '.../.git/index.lock': Permission denied
#
# So we copy ONLY the PKGBUILD into a scratch directory, run makepkg there, and
# copy the resulting .SRCINFO back. The git checkout is never bind-mounted and
# never chowned.
set -euo pipefail

dir=${1:?usage: gen-srcinfo.sh <dir-containing-PKGBUILD>}
dir=$(cd "$dir" && pwd)

[ -f "$dir/PKGBUILD" ] || { echo "gen-srcinfo: no PKGBUILD in $dir" >&2; exit 1; }

scratch=$(mktemp -d)
trap 'rm -rf "$scratch"' EXIT
cp "$dir/PKGBUILD" "$scratch/PKGBUILD"
# The container runs as UID 1000; make sure it can read the PKGBUILD and write
# makepkg's scratch files into the mount.
chmod 777 "$scratch"
chmod 644 "$scratch/PKGBUILD"

# makepkg refuses to run as root. Rather than useradd + sudo inside the
# container (sudo can fail outright when the runtime sets no-new-privileges,
# and it was masking the real error), run the container directly as a non-root
# UID. makepkg only checks EUID != 0, so it does not care that the UID has no
# /etc/passwd entry; HOME just has to be somewhere writable.
#
# Output is captured on the HOST side: redirecting inside the container meant a
# failure in there could leave a silently empty file behind. Container stderr
# stays attached so the real diagnostic reaches the CI log.
if ! docker run --rm \
        -v "$scratch:/pkg" -w /pkg \
        --user 1000:1000 -e HOME=/tmp \
        archlinux:base-devel \
        makepkg --printsrcinfo > "$dir/.SRCINFO.tmp"; then
    echo "gen-srcinfo: makepkg failed (see container output above)" >&2
    rm -f "$dir/.SRCINFO.tmp"
    exit 1
fi

if [ ! -s "$dir/.SRCINFO.tmp" ]; then
    echo "gen-srcinfo: makepkg produced an empty .SRCINFO" >&2
    echo "gen-srcinfo: PKGBUILD was:" >&2
    sed 's/^/  | /' "$scratch/PKGBUILD" >&2
    rm -f "$dir/.SRCINFO.tmp"
    exit 1
fi

mv "$dir/.SRCINFO.tmp" "$dir/.SRCINFO"
chmod 644 "$dir/.SRCINFO"

echo "gen-srcinfo: wrote $dir/.SRCINFO"
head -4 "$dir/.SRCINFO"
