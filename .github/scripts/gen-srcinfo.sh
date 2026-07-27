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

docker run --rm -v "$scratch:/pkg" -w /pkg archlinux:base-devel bash -c '
  set -e
  useradd -m build
  chown -R build:build /pkg
  sudo -u build makepkg --printsrcinfo > /pkg/.SRCINFO
'

[ -s "$scratch/.SRCINFO" ] || { echo "gen-srcinfo: makepkg produced nothing" >&2; exit 1; }

# The container ran as a foreign UID; normalise ownership before it lands in
# the checkout so `git add` can read it.
cp "$scratch/.SRCINFO" "$dir/.SRCINFO"
chmod 644 "$dir/.SRCINFO"

echo "gen-srcinfo: wrote $dir/.SRCINFO"
