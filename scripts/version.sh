#!/bin/bash
usage() {
  cat <<'USAGE'
Usage:
  scripts/version.sh                  print the version
  scripts/version.sh --header <path>  also write <path> as version_generated.h (untouched when current)
  scripts/version.sh --base           the last released x.y.z (for the mac Info.plist)
Order: $PUI_VERSION, then git describe against the last v* tag (1.6.0 on a clean tag,
1.6.0-dev.3+ef96f46[.dirty] after it), then version.txt (git archive), then 0.0.0-unknown.
USAGE
}
case "${1:-}" in -h|--help) usage; exit 0 ;; esac

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

archived_describe() {
    local v=""
    [ -f "$ROOT/version.txt" ] && v="$(tr -d '[:space:]' < "$ROOT/version.txt")"
    case "$v" in ''|*'$Format'*) return 1 ;; esac
    echo "$v"
}

describe() {
    git -C "$ROOT" describe --tags --long --match 'v[0-9]*' "$@" 2>/dev/null || archived_describe
}

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
    if [ ! -f "$OUT" ] || [ "$(cat "$OUT")" != "$CONTENT" ]; then
        printf '%s\n' "$CONTENT" > "$OUT"
    fi
fi

echo "$VERSION"
