#!/usr/bin/env bash
# ui-test.sh — drive the Panels UI running in the local Wine foobar2000 (dev aid, Hyprland host).
#   ui-test.sh click X Y [top:TITLE] [l|r|dbl|move|close|wheelN|key:VK|type:TEXT]
#   ui-test.sh shot FILE.png          screenshot of the Panels UI window
#   ui-test.sh pvars k=v ...          edit persisted pvars (foobar2000 must be stopped)
#   ui-test.sh restart [DLL]          graceful close, install DLL (default build/), relaunch + play
set -euo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$REPO/build/tools"; EXE="$OUT/wclick.exe"
PREFIX="${WINEPREFIX:-$HOME/.local/share/wineprefixes/foobar2000}"

build_wclick() {
  [ "$EXE" -nt "$REPO/tools/wclick.c" ] && return
  mkdir -p "$OUT"; local X="${XWIN:-$HOME/.xwin}"
  clang-cl --target=x86_64-pc-windows-msvc /nologo /O1 /MT /c "$REPO/tools/wclick.c" /Fo"$OUT/wclick.obj" \
    /imsvc"$X/crt/include" /imsvc"$X/sdk/include/ucrt" /imsvc"$X/sdk/include/um" /imsvc"$X/sdk/include/shared" 2>&1 | grep -v msvc-not-found || true
  lld-link /nologo "$OUT/wclick.obj" /libpath:"$X/crt/lib/x86_64" /libpath:"$X/sdk/lib/um/x86_64" \
    /libpath:"$X/sdk/lib/ucrt/x86_64" user32.lib /out:"$EXE"
}
geom() {
  hyprctl clients -j | python3 -c "import json,sys
for c in json.load(sys.stdin):
    if 'Panels UI' in c['title']: print('%d,%d %dx%d' % (c['at'][0], c['at'][1], c['size'][0], c['size'][1])); break"
}
click() { build_wclick; WINEPREFIX="$PREFIX" WINEDEBUG=-all wine "$EXE" "$@"; }

case "${1:-}" in
  click) shift; click "$@" ;;
  shot) grim -g "$(geom)" "$2" ;;
  pvars) shift; python3 - "$@" <<'PY'
import sqlite3, sys, os
db = os.path.expanduser('~/.foobar2000/profile/config.sqlite')
key = 'cfg_var.1B5C9A40-7E2D-4C8A-9F31-6A0B2D4E8C70'
c = sqlite3.connect(db)
(v,) = c.execute('select value from configStrings where name=?', (key,)).fetchone()
d = dict(l.split('=', 1) for l in v.split('\n') if '=' in l)
for a in sys.argv[1:]:
    k, val = a.split('=', 1); d[k] = val
c.execute('update configStrings set value=? where name=?', (''.join(f'{k}={d[k]}\n' for k in sorted(d)), key))
c.commit()
PY
  ;;
  restart)
    DLL="${2:-$REPO/build/foo_ui_panels.dll}"
    click 0 0 close >/dev/null 2>&1 || true
    for _ in $(seq 1 20); do pgrep -x foobar2000.exe >/dev/null || break; sleep 0.5; done
    pkill -x foobar2000.exe || true; sleep 1
    cp -f "$DLL" "$HOME/.foobar2000/profile/user-components-x64/foo_ui_panels/foo_ui_panels.dll"
    nohup foobar2000 >/dev/null 2>&1 & sleep 7
    foobar2000 -play >/dev/null 2>&1 & sleep 3 ;;
  *) sed -n '2,7p' "$0"; exit 2 ;;
esac
