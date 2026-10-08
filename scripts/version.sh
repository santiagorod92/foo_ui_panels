#!/bin/bash
# The version string a build stamps into PUI_VERSION (what foobar2000 shows under
# Preferences › Components). One resolver for every build path, so a DLL/bundle always
# says where it came from. Nothing here is maintained by hand: releases are git tags.
#
#   1. $PUI_VERSION, when set — a release build passes semantic-release's version
#      (the tag doesn't exist yet while it builds, so git can't know it).
#   2. git describe against the last v* tag:
#        on the tag, clean tree       -> 1.6.0
#        3 commits after it            -> 1.6.0-dev.3+ef96f46
#        uncommitted changes           -> 1.6.0-dev.3+ef96f46.dirty
#   3. version.txt — for a tree without .git. In the repo it holds a `$Format:…$`
#      placeholder that `git archive` (GitHub's source zips) replaces with the same
#      describe output (.gitattributes: export-subst), so it never goes stale.
#   4. 0.0.0-unknown.
#
# Usage:
#   scripts/version.sh                  print the version
#   scripts/version.sh --header <path>  also write <path> as version_generated.h
#                                       (left untouched when already up to date)
#   scripts/version.sh --base           the last released version (x.y.z): the mac
#                                       bundle's Info.plist wants a plain x.y.z
#
# Runs under macOS's bash 3.2 too (the mac build runs it natively).

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

# version.txt, unless it's still the unexpanded placeholder (a git checkout).
archived_describe() {
    local v=""
    [ -f "$ROOT/version.txt" ] && v="$(tr -d '[:space:]' < "$ROOT/version.txt")"
    case "$v" in ''|*'$Format'*) return 1 ;; esac
    echo "$v"
}

describe() {
    git -C "$ROOT" describe --tags --long --match 'v[0-9]*' "$@" 2>/dev/null || archived_describe
}

# v<tag>-<commits since>-g<sha>[-dirty] -> 1.2.3 / 1.2.3-dev.N+sha[.dirty].
# Also takes a bare tag (archived exactly on one: no -N-gsha part).
format() {
    local desc="$1" dirty="" tag count sha
    case "$desc" in *-dirty) dirty=".dirty"; desc="${desc%-dirty}" ;; esac
    case "$desc" in
        *-g*)
            sha="${desc##*-g}";  desc="${desc%-g*}"
            count="${desc##*-}"; tag="${desc%-*}" ;;
        *) tag="$desc"; count=0; sha="" ;;
    esac
    tag="${tag#v}"
    if [ "$count" = "0" ] && [ -z "$dirty" ]; then
        echo "$tag"
    else
        echo "${tag}-dev.${count}+${sha}${dirty}"
    fi
}

if [ "${1:-}" = "--base" ]; then
    desc="$(describe)" || { echo "0.0.0"; exit 0; }
    desc="$(format "$desc")"
    echo "${desc%%-*}"
    exit 0
fi

if [ -n "${PUI_VERSION:-}" ]; then
    VERSION="$PUI_VERSION"
elif desc="$(describe --dirty)"; then
    VERSION="$(format "$desc")"
else
    VERSION="0.0.0-unknown"
fi

if [ "${1:-}" = "--header" ]; then
    OUT="${2:?--header needs an output path}"
    CONTENT="$(printf '#pragma once\n#define PUI_VERSION "%s"' "$VERSION")"
    # Rewrite only on change: an untouched header doesn't make ninja recompile component.cpp.
    if [ ! -f "$OUT" ] || [ "$(cat "$OUT")" != "$CONTENT" ]; then
        printf '%s\n' "$CONTENT" > "$OUT"
    fi
fi

echo "$VERSION"
