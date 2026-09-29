#!/usr/bin/env bash
# deploy.sh — install the built foo_ui_panels.dll into the local Wine
# foobar2000's user-components-x64 dir. Optionally relaunch foobar2000 after.
#
# Usage:
#   ./scripts/deploy.sh            install only
#   ./scripts/deploy.sh --launch   install, then kill+relaunch foobar2000

set -euo pipefail

LAUNCH=0
for arg in "$@"; do
  case "$arg" in
    --launch) LAUNCH=1 ;;
    *) echo "unknown arg: $arg" >&2; exit 2 ;;
  esac
done

REPO="$(cd "$(dirname "$0")/.." && pwd)"
BUILT_DLL="$REPO/build/foo_ui_panels.dll"
DEST_DIR="$HOME/.foobar2000/profile/user-components-x64/foo_ui_panels"

[ -f "$BUILT_DLL" ] || { echo "ERROR: $BUILT_DLL not found — build first: ./build.sh" >&2; exit 1; }

mkdir -p "$DEST_DIR"
cp -f "$BUILT_DLL" "$DEST_DIR/foo_ui_panels.dll"
echo "deployed: $DEST_DIR/foo_ui_panels.dll"

if [ "$LAUNCH" = "1" ]; then
  echo "==> relaunching foobar2000 ..."
  pkill -f 'foobar2000.exe' 2>/dev/null || true
  sleep 1
  nohup foobar2000 >/dev/null 2>&1 &
  disown
  echo "==> launched."
else
  echo "Restart foobar2000 to load it."
fi
