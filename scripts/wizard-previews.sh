#!/usr/bin/env bash
# wizard-previews.sh — regenerate the layout wizard's previews (res/wizard/<id>.png). Each built-in
# template is written as a skin and run in a throwaway portable foobar2000 (build/wizard-demo: the
# Wine install symlinked, its own Wine prefix, a fresh profile with only this component, playing the
# made-up library from wizard-demo-library.sh — no real music, art or personal playlists end up in
# the images); the window is screenshotted at SIZE and scaled to OUT. Dev aid (Hyprland host, like
# ui-test.sh). Your own foobar2000 and its prefix are never touched, but it must not be running (the
# shot and the close find foobar2000's windows by name). Rebuild afterwards: the PNGs are embedded.
#   wizard-previews.sh [id ...]      (default: every template)
#   SIZE=1170x600 OUT=600x308 WAIT=12 DLL=build/foo_ui_panels.dll FB2K=/usr/share/foobar2000
set -euo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"
SIZE="${SIZE:-1170x600}"; OUT="${OUT:-600x308}"; WAIT="${WAIT:-12}"
DLL="${DLL:-$REPO/build/foo_ui_panels.dll}"; FB2K="${FB2K:-/usr/share/foobar2000}"
DEMO="$REPO/build/wizard-demo"; FB="$DEMO/fb2k"; PROFILE="$FB/profile"; MUSIC="$DEMO/music"
export WINEPREFIX="$DEMO/prefix" WINEDEBUG=-all # a Wine prefix of its own too
UI="$REPO/scripts/ui-test.sh"

if pgrep -x foobar2000.exe >/dev/null; then echo "close foobar2000 first (one instance per Wine prefix)" >&2; exit 1; fi
[ -f "$DLL" ] || { echo "no $DLL — make build first" >&2; exit 1; }

TOOL="$REPO/build/tools/install_templates"
mkdir -p "$(dirname "$TOOL")"
"${CXX_HOST:-clang++}" -std=c++20 -O1 -I"$REPO/src" "$REPO/tools/install_templates.cpp" \
  "$REPO/src/core/skin_templates.cpp" "$REPO/src/core/skin_config.cpp" -o "$TOOL"
"$REPO/scripts/wizard-demo-library.sh" "$MUSIC"

# Hyprland (0.56+ Lua config): this window fully opaque, whatever translucency rule applies.
hyprctl eval 'hl.window_rule({ match = { class = "^foobar2000\\.exe$" }, tag = "-default-opacity", opacity = "1 1" })' >/dev/null 2>&1 || true
fb2k() { wine "$FB/foobar2000.exe" "$@" </dev/null >/dev/null 2>&1; }
stop_fb2k() {
  "$UI" click 0 0 close </dev/null >/dev/null 2>&1 || true
  for _ in $(seq 1 20); do pgrep -x foobar2000.exe >/dev/null || return 0; sleep 0.5; done
  pkill -x foobar2000.exe || true; sleep 1
}
trap stop_fb2k EXIT
sql() { sqlite3 "$PROFILE/config.sqlite" "$1"; }

# --- the Wine prefix: the skins' icon font (Segoe UI Symbol) mapped to a host font that has the glyphs
if [ ! -d "$WINEPREFIX" ]; then
  WINEDLLOVERRIDES="mscoree,mshtml=" wineboot -i >/dev/null 2>&1 # no Mono/Gecko prompts
  SYM="$(fc-list ':charset=23ee 23f8 2630 25b6 23f9' family | head -1 | cut -d, -f1)"
  [ -n "$SYM" ] && wine reg add 'HKCU\Software\Wine\Fonts\Replacements' /v 'Segoe UI Symbol' /d "$SYM" /f >/dev/null 2>&1
fi

# --- a fresh portable foobar2000 with only this component ----------------------------------------
rm -rf "$FB"; mkdir -p "$FB"
cp -rs "$FB2K/"* "$FB/"; rm -rf "$FB/profile"
mkdir -p "$PROFILE/user-components-x64/foo_ui_panels"
cp "$DLL" "$PROFILE/user-components-x64/foo_ui_panels/"
fb2k & # creates the profile; stops at "New user interface module(s) found"
for _ in $(seq 1 40); do [ -s "$PROFILE/config.sqlite" ] && break; sleep 0.5; done
sleep 2; pkill -x foobar2000.exe || true; sleep 1
rm -f "$PROFILE/running"; touch "$PROFILE/nobadshutdown" # no "terminated abnormally" prompt after that
GUID=2C1B0A6F4E3D504F9A1B2C3D4E5F6071 # Panels UI's user_interface GUID (platform/win/main.cpp), as stored
DUI=DEE8BC6E5CA5094E9EFEA8B6A66D69FD  # Default UI's
for k in 22AF8F41-D22D-4529-B0ED-437BE9AEC30C 094A28DA-C6E8-4392-BAF1-AD0F29E5B303; do # the UI to run
  sql "insert or replace into configBlobs(name,value) values('cfg_var.$k',x'$GUID')"
done
sql "insert or replace into configBlobs(name,value) values('cfg_var.AD13B1D4-A6F7-425E-A93B-2A3C7DC37A93',x'02000000$GUID$DUI')" # known UIs
# Media Library: the demo library as its one folder, in library-v2.0 as foobar2000 writes it
# (strings are u32 length + UTF-8). folders: count, then per folder its path (a portable install's
# is relative to itself) and a u32 0. filters: the default restrict/exclude masks — without them the
# folder list is ignored.
python3 - "$PROFILE/library-v2.0" <<'PY'
import struct, sys, os
s = lambda b: struct.pack('<I', len(b)) + b
os.makedirs(sys.argv[1], exist_ok=True)
open(os.path.join(sys.argv[1], 'folders'), 'wb').write(struct.pack('<I', 1) + s(b'file-relative://..\\music\\') + struct.pack('<I', 0))
open(os.path.join(sys.argv[1], 'filters'), 'wb').write(s(b'*') + s(b'*.CUE'))
PY
# Skins: every template under one root; the active one picked per shot (skin_paths.cpp's cfg_strings).
"$TOOL" "$DEMO/skins.new" > "$DEMO/list"; rm -rf "$DEMO/skins"; mv "$DEMO/skins.new" "$DEMO/skins"
sed -i "s#$DEMO/skins.new#$DEMO/skins#" "$DEMO/list"
sql "insert or replace into configStrings(name,value) values('cfg_var.1F8A6C2E-4D9B-4E71-8C3A-5B2D9E0F6A17','$(winepath -w "$DEMO/skins")')"
# The window: SIZE (Panels UI's saved placement, platform/win/main.cpp), no menu bar (pvar).
sql "insert or replace into configStrings(name,value) values('cfg_var.5E7A9C31-2B4D-4F6E-8A1C-3D5B7F9E0A24','40 40 $((40 + ${SIZE%x*})) $((40 + ${SIZE#*x})) 1')"
sql "insert or replace into configStrings(name,value) values('cfg_var.1B5C9A40-7E2D-4C8A-9F31-6A0B2D4E8C70','menubar=0
')"
# A first run: the Media Library scans the folder, the playlist gets the whole demo library.
sql "insert or replace into configStrings(name,value) values('cfg_var.2A7E4C11-8B3F-4D6A-9E52-1C8F3A6B0D24','$(head -1 "$DEMO/list" | cut -f2 | xargs -d '\n' basename)')"
fb2k & sleep 7
W=(); while IFS= read -r t; do W+=("$(winepath -w "$t")"); done < <(find "$MUSIC" -name '*.mp3' | sort)
fb2k /add "${W[@]}"; sleep 8
stop_fb2k

# --- one shot per template --------------------------------------------------------------------------
mkdir -p "$REPO/res/wizard"
while IFS=$'\t' read -r id dir; do
  [ $# -gt 0 ] && [[ " $* " != *" $id "* ]] && continue
  sql "insert or replace into configStrings(name,value) values('cfg_var.2A7E4C11-8B3F-4D6A-9E52-1C8F3A6B0D24','$(basename "$dir" | sed "s/'/''/g")')"
  fb2k & sleep 7
  fb2k /play & sleep 1
  sleep "$WAIT"
  "$UI" shot "$DEMO/$id.png" </dev/null
  magick "$DEMO/$id.png" -filter Lanczos -resize "$OUT!" -strip -define png:compression-level=9 "$REPO/res/wizard/$id.png"
  echo "res/wizard/$id.png ($(stat -c %s "$REPO/res/wizard/$id.png") bytes)"
  stop_fb2k
done < "$DEMO/list"
