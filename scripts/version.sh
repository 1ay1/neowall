#!/usr/bin/env bash
#
# version.sh — the single source of truth for neowall's version number.
#
# meson.build owns the version. Everything else (flake.nix, packaging/PKGBUILD,
# the AUR packages, README badges) is DERIVED from it. This script both reads
# that value and propagates it, so a release can never again ship with three
# different version numbers in three different files.
#
# Usage:
#   scripts/version.sh get               print the current version (e.g. 0.5.3)
#   scripts/version.sh set <x.y.z>       set meson.build + propagate everywhere
#   scripts/version.sh sync              re-propagate the meson.build version
#   scripts/version.sh check             exit 1 if anything is out of sync
#
set -euo pipefail

repo_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
cd "$repo_root"

MESON=meson.build
FLAKE=flake.nix
PKGBUILD=packaging/PKGBUILD

die() { printf 'version.sh: %s\n' "$*" >&2; exit 1; }

get_meson_version() {
    # The project() version field — first `version: 'x.y.z'` in the file.
    sed -n "s/^[[:space:]]*version:[[:space:]]*'\([0-9][^']*\)'.*/\1/p" "$MESON" | head -1
}

get_flake_version() {
    sed -n 's/^[[:space:]]*version[[:space:]]*=[[:space:]]*"\([^"]*\)".*/\1/p' "$FLAKE" | head -1
}

get_pkgbuild_version() {
    sed -n 's/^pkgver=\(.*\)$/\1/p' "$PKGBUILD" | head -1
}

validate_semver() {
    case "$1" in
        [0-9]*.[0-9]*.[0-9]*) ;;
        *) die "'$1' is not a x.y.z version" ;;
    esac
}

# Rewrite every derived file from the given version.
propagate() {
    local v="$1"
    validate_semver "$v"

    # flake.nix: version = "x.y.z";
    sed -i "0,/version[[:space:]]*=[[:space:]]*\"[^\"]*\"/s//version = \"$v\"/" "$FLAKE"

    # packaging/PKGBUILD: pkgver=x.y.z (and reset pkgrel on a version bump)
    sed -i "s/^pkgver=.*/pkgver=$v/" "$PKGBUILD"
    sed -i "s/^pkgrel=.*/pkgrel=1/" "$PKGBUILD"

    printf 'propagated version %s to %s, %s\n' "$v" "$FLAKE" "$PKGBUILD"
}

cmd=${1:-check}
case "$cmd" in
    get)
        v=$(get_meson_version)
        [ -n "$v" ] || die "could not read version from $MESON"
        printf '%s\n' "$v"
        ;;

    set)
        [ $# -ge 2 ] || die "usage: version.sh set <x.y.z>"
        new=${2#v}
        validate_semver "$new"
        # Only touch the project() version line, never a dependency's version:.
        awk -v ver="$new" '
            !done && /^[[:space:]]*version:[[:space:]]*'\''[0-9]/ {
                sub(/'\''[^'\'']*'\''/, "'\''" ver "'\''"); done = 1
            }
            { print }
        ' "$MESON" > "$MESON.tmp" && mv "$MESON.tmp" "$MESON"
        [ "$(get_meson_version)" = "$new" ] || die "failed to set $MESON version"
        printf 'meson.build version -> %s\n' "$new"
        propagate "$new"
        ;;

    sync)
        v=$(get_meson_version)
        [ -n "$v" ] || die "could not read version from $MESON"
        propagate "$v"
        ;;

    check)
        v=$(get_meson_version)
        [ -n "$v" ] || die "could not read version from $MESON"
        rc=0
        fv=$(get_flake_version)
        pv=$(get_pkgbuild_version)
        printf 'meson.build      : %s\n' "$v"
        printf 'flake.nix        : %s\n' "$fv"
        printf 'packaging/PKGBUILD: %s\n' "$pv"
        [ "$fv" = "$v" ] || { printf '\nMISMATCH: flake.nix has %s, expected %s\n' "$fv" "$v" >&2; rc=1; }
        [ "$pv" = "$v" ] || { printf 'MISMATCH: packaging/PKGBUILD has %s, expected %s\n' "$pv" "$v" >&2; rc=1; }
        if [ "$rc" -ne 0 ]; then
            printf "\nRun 'scripts/version.sh sync' to fix.\n" >&2
        else
            printf '\nall version references are in sync\n'
        fi
        exit "$rc"
        ;;

    *)
        die "unknown command '$cmd' (want: get | set <x.y.z> | sync | check)"
        ;;
esac
