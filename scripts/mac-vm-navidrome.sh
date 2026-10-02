#!/usr/bin/env bash
# mac-vm-navidrome.sh — install foo_navidrome (same author; Panels UI cooperates with it) into
# the local macOS VM (../macos-devbox, `mvm`), and/or copy its settings over from the local Wine
# foobar2000, so the album browser / ratings / playlist switcher can be tested against a real
# Navidrome server without typing credentials over VNC.
#
#   mac-vm-navidrome.sh deploy [FILE.fb2k-component | TAG]   default: latest GitHub release
#   mac-vm-navidrome.sh config [WINE_CONFIG.sqlite]          default: ~/.foobar2000/profile/config.sqlite
#
# config copies every foo_navidrome cfg_var (server URL, user, password, custom headers such as
# Cloudflare Access service-token headers, options) — the macOS build uses the same GUIDs as the
# Windows one. Values are piped over ssh straight into the guest's config.sqlite and never
# printed. foobar2000 is stopped in the guest for the write and started again afterwards.
set -euo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"
MVM="${MVM:-$(cd "$REPO/.." && pwd)/macos-devbox/mvm}"
[ -x "$MVM" ] || { echo "mvm not found at $MVM — clone macos-devbox next to this repo, or set MVM="; exit 1; }
GH_REPO="${NAVIDROME_GH:-santiagorod92/foo_navidrome}"
GUEST_DB='~/Library/foobar2000-v2/config.sqlite'
# foo_navidrome's cfg_var GUIDs: A1B2C3D4-1111-2222-AABB-CCDDEEFF01xx
PREFIX='CFG_VAR.A1B2C3D4-1111-2222-AABB-CCDDEEFF01%'

case "${1:-}" in
  deploy)
    src="${2:-}"
    if [ -z "$src" ]; then exec "$MVM" deploy --gh "$GH_REPO" --launch
    elif [ -f "$src" ]; then exec "$MVM" deploy "$src" --launch
    else exec "$MVM" deploy --gh "$GH_REPO@$src" --launch   # a release tag, e.g. v1.18.0
    fi ;;
  config)
    db="${2:-$HOME/.foobar2000/profile/config.sqlite}"
    [ -f "$db" ] || { echo "no foobar2000 config at $db"; exit 1; }
    sql="$(for t in config configInts configStrings configBlobs configReals; do
             sqlite3 "$db" ".mode insert $t" "select * from $t where upper(name) like '$PREFIX'"
           done | sed 's/^INSERT INTO/INSERT OR REPLACE INTO/')"
    [ -n "$sql" ] || { echo "no foo_navidrome settings in $db (configure it in the Wine foobar2000 first)"; exit 1; }
    echo "==> copying $(printf '%s\n' "$sql" | grep -c '^INSERT') foo_navidrome settings into the guest"
    "$MVM" fb2k stop
    printf '%s\n' "$sql" | "$MVM" ssh "sqlite3 $GUEST_DB"
    "$MVM" fb2k start ;;
  *) sed -n '2,13p' "$0"; exit 2 ;;
esac
