#!/usr/bin/env bash
usage() {
  cat <<'USAGE'
Usage:
  mac-vm-navidrome.sh deploy [FILE.fb2k-component | TAG]   default: latest GitHub release
  mac-vm-navidrome.sh config [WINE_CONFIG.sqlite]          default: ~/.foobar2000/profile/config.sqlite
Installs foo_navidrome into the local macOS VM (../macos-devbox, mvm) and/or copies its
settings (server, credentials, custom headers, options) from the Wine foobar2000, so Panels UI
can be tested against a real Navidrome server. Values go over ssh into the guest's
config.sqlite and are never printed; foobar2000 is stopped in the guest for the write.
  MVM=/path/mvm   NAVIDROME_GH=owner/repo
USAGE
}
case "${1:-}" in -h|--help) usage; exit 0 ;; esac

set -euo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"
MVM="${MVM:-$(cd "$REPO/.." && pwd)/macos-devbox/mvm}"
[ -x "$MVM" ] || { echo "mvm not found at $MVM — clone macos-devbox next to this repo, or set MVM="; exit 1; }
GH_REPO="${NAVIDROME_GH:-santiagorod92/foo_navidrome}"
GUEST_DB='~/Library/foobar2000-v2/config.sqlite'
PREFIX='CFG_VAR.A1B2C3D4-1111-2222-AABB-CCDDEEFF01%'

case "${1:-}" in
  deploy)
    src="${2:-}"
    if [ -z "$src" ]; then exec "$MVM" deploy --gh "$GH_REPO" --launch
    elif [ -f "$src" ]; then exec "$MVM" deploy "$src" --launch
    else exec "$MVM" deploy --gh "$GH_REPO@$src" --launch
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
  *) usage >&2; exit 2 ;;
esac
