#!/usr/bin/env bash
usage() {
  cat <<'USAGE'
Usage: ui-test.sh <platform> <scenario> [args]
       ui-test.sh compare <scenario> [platform...]

Drives the real foobar2000 with Panels UI through the same scenarios on every platform and
asserts on foo_ui_panels.log, the process and its crash reports; screenshots go to
build/ui-test/.

Platforms
  wine     the local Wine foobar2000 (~/.foobar2000); input posted with tools/wclick.c
  win11    the Windows 11 VM (../windows-devbox, wvm); real Windows, VNC input
  mac      the macOS VM: on a Mac terminal-mac (../foo_navidrome/scripts/terminal-mac.sh),
           elsewhere ../macos-devbox's mvm; MVM= picks another

Scenarios
  smoke [ARTIFACT]   turn verbose logging on, deploy, relaunch; assert the session banner, the
                     skin load, the main window, start playback, then no crash report, no ERROR
                     line, screenshot; the skin's own warnings are recorded as notes
  solo [ARTIFACT]    the sister component (SIBLING, default foo_navidrome) moved out of
                     user-components for the run (restored after): the smoke steps, plus the
                     session banner must report navidrome=no; Panels UI must work without it

ARTIFACT: what gets deployed first. wine, win11: a DLL (default build/foo_ui_panels.dll; win11
also re-copies skins/$SKIN like make win11-skin); mac: a .component (default
build-mac/foo_ui_panels.component). Pass "-" to skip the deploy.

Helpers
  log [N]            last N lines of foo_ui_panels.log on the platform
  shot [FILE]        screenshot
  wine click X Y [top:TITLE] [l|r|dbl|move|hover|close|wheelN|key:VK|type:TEXT|drag:DY|post:MSG:WP:LP|menukey:VK|bmclick]
                     raw tools/wclick.c call; hover moves the host's real pointer (tooltips need it)
  wine pvars k=v ... edit persisted pvars (foobar2000 must be stopped)
  wine restart [DLL] graceful close, install DLL (default build/), relaunch + play

compare <scenario> [platform...]
  runs the scenario on each platform (default: wine win11 mac) and prints a step-by-step
  matrix of ok / FAIL / not reached plus the notes each run recorded; PARALLEL=1 runs the
  platforms at the same time (separate machines, nothing shared)
USAGE
}
case "${1:-}" in -h|--help) usage; exit 0 ;; esac

set -uo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"
SHOTS="$REPO/build/ui-test"
VERBOSE_KEY='cfg_var.2F6C9A41-7D3E-4B58-91A7-0C5E3B82D416'
SIBLING="${SIBLING:-foo_navidrome}"

CLEANUPS=()
on_exit() { CLEANUPS+=("$1"); trap run_cleanups EXIT; }
run_cleanups() {
  local i
  for (( i = ${#CLEANUPS[@]} - 1; i >= 0; i-- )); do eval "${CLEANUPS[$i]}" >/dev/null 2>&1 || true; done
}

say()  { printf '\033[1;36m==> %s\033[0m\n' "$*"; }
ok()   { printf '\033[1;32m  ok\033[0m %s\n' "$*"; record ok "$*"; }
warn() { printf '\033[1;33m  !!\033[0m %s\n' "$*"; }
note() { printf '\033[1;35m  ..\033[0m %s: %s\n' "$1" "$2"; [ -n "${RESULTS:-}" ] && printf 'note\t%s\t%s\n' "$1" "$2" >> "$RESULTS"; return 0; }
fail() {
  printf '\033[1;31mFAIL\033[0m %s\n' "$*"; record FAIL "$*"
  d shot "$SHOTS/$PLATFORM-fail-$(date +%H%M%S).png" >/dev/null 2>&1 || true
  exit 1
}
record() { [ -n "${RESULTS:-}" ] && printf '%s\t%s\n' "$1" "${2%% — *}" >> "$RESULTS"; return 0; }

wine_init() {
  PREFIX="${WINEPREFIX:-$HOME/.local/share/wineprefixes/foobar2000}"
  PROFILE="$HOME/.foobar2000/profile"
  LOG="$PROFILE/foo_ui_panels.log"
  WCLICK_DIR="$REPO/build/tools"; WCLICK="$WCLICK_DIR/wclick.exe"
}
build_wclick() {
  [ "$WCLICK" -nt "$REPO/tools/wclick.c" ] && return
  mkdir -p "$WCLICK_DIR"; local X="${XWIN:-$HOME/.xwin}"
  clang-cl --target=x86_64-pc-windows-msvc /nologo /O1 /MT /c "$REPO/tools/wclick.c" /Fo"$WCLICK_DIR/wclick.obj" \
    /imsvc"$X/crt/include" /imsvc"$X/sdk/include/ucrt" /imsvc"$X/sdk/include/um" /imsvc"$X/sdk/include/shared" 2>&1 | grep -v msvc-not-found || true
  lld-link /nologo "$WCLICK_DIR/wclick.obj" /libpath:"$X/crt/lib/x86_64" /libpath:"$X/sdk/lib/um/x86_64" \
    /libpath:"$X/sdk/lib/ucrt/x86_64" user32.lib /out:"$WCLICK"
}
wclick() { build_wclick; WINEPREFIX="$PREFIX" WINEDEBUG=-all wine "$WCLICK" "$@"; }
geom() {
  hyprctl clients -j | python3 -c "import json,sys
cs = [c for c in json.load(sys.stdin) if c['class'].lower() == 'foobar2000.exe']
if not cs: sys.exit(1)
c = max(cs, key=lambda c: c['size'][0] * c['size'][1])
print('%d,%d %dx%d' % (c['at'][0], c['at'][1], c['size'][0], c['size'][1]))"
}
wine_running() { pgrep -x foobar2000.exe >/dev/null; }
wine_stop() {
  wine_running || return 0
  wclick 0 0 close >/dev/null 2>&1 || true
  for _ in $(seq 1 20); do wine_running || break; sleep 0.5; done
  pkill -x foobar2000.exe || true; sleep 1
}
wine_start() { nohup foobar2000 >/dev/null 2>&1 & for _ in $(seq 1 30); do wine_running && break; sleep 0.5; done; }
wine_deploy() {
  local dll="${1:-$REPO/build/foo_ui_panels.dll}"
  [ "$dll" = - ] && return 0
  [ -f "$dll" ] || fail "no $dll — make build first"
  say "deploy $(basename "$dll")"
  wine_stop
  cp -f "$dll" "$PROFILE/user-components-x64/foo_ui_panels/foo_ui_panels.dll"
}
wine_sibling_disable() {
  wine_stop
  local on="$PROFILE/user-components-x64/$1" off="$PROFILE/.solo-off-$1"
  [ -d "$off" ] && [ ! -d "$on" ] && return 0
  [ -d "$on" ] || return 1
  mv "$on" "$off"
}
wine_sibling_enable() {
  wine_stop
  local on="$PROFILE/user-components-x64/$1" off="$PROFILE/.solo-off-$1"
  [ -d "$off" ] && [ ! -d "$on" ] && mv "$off" "$on"
  return 0
}
wine_play() { foobar2000 -play >/dev/null 2>&1 & }
wine_log_size() { wc -c < "$LOG" 2>/dev/null || echo 0; }
wine_log_from() { tail -c +"$1" "$LOG" 2>/dev/null || true; }
wine_crash_count() { ls "$PROFILE/crash reports"/*.txt 2>/dev/null | wc -l; }
wine_cfg_sql() { wine_stop; sqlite3 "$PROFILE/config.sqlite" "$1"; }
wine_shot() { local g; mkdir -p "$(dirname "$1")"; g="$(geom)" || { echo "no foobar2000 window"; return 1; }; grim -g "$g" "$1" && echo "$1"; }
wine_pvars() {
  python3 - "$@" <<'PY'
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
}
wine_restart() { wine_deploy "${1:-}"; wine_start; sleep 7; wine_play; sleep 3; }

win11_init() {
  WVM="${WVM:-$(cd "$REPO/.." && pwd)/windows-devbox/wvm}"
  [ -x "$WVM" ] || { echo "wvm not found at $WVM (clone windows-devbox next to this repo)"; exit 1; }
  GUEST_PROFILE='AppData/Roaming/foobar2000-v2'
  LOG_CACHE="$(mktemp -d)"
}
win11_running() { "$WVM" ps 'if (Get-Process foobar2000 -ErrorAction SilentlyContinue) { exit 0 } else { exit 1 }' 2>/dev/null; }
win11_stop() { "$WVM" fb2k stop >/dev/null 2>&1 || true; }
win11_start() { "$WVM" fb2k start >/dev/null; for _ in $(seq 1 30); do win11_running && break; sleep 1; done; }
win11_deploy() {
  local dll="${1:-$REPO/build/foo_ui_panels.dll}"
  [ "$dll" = - ] && return 0
  [ -f "$dll" ] || fail "no $dll — make build first"
  say "deploy $(basename "$dll") + skin"
  "$WVM" deploy "$dll" >/dev/null
  make -C "$REPO" --no-print-directory win11-skin >/dev/null
  win11_stop
}
win11_play() { "$WVM" fb2k cmd "Play" >/dev/null 2>&1 || true; }
win11_pull_log() { rm -f "$LOG_CACHE/foo_ui_panels.log"; "$WVM" pull "$GUEST_PROFILE/foo_ui_panels.log" "$LOG_CACHE" >/dev/null 2>&1 || true; }
win11_log_size() { win11_pull_log; wc -c < "$LOG_CACHE/foo_ui_panels.log" 2>/dev/null || echo 0; }
win11_log_from() { win11_pull_log; tail -c +"$1" "$LOG_CACHE/foo_ui_panels.log" 2>/dev/null || true; }
win11_crash_count() {
  "$WVM" ps "(Get-ChildItem \"\$env:APPDATA\\foobar2000-v2\\crash reports\" -Filter *.txt -ErrorAction SilentlyContinue | Measure-Object).Count" | tr -d '\r '
}
win11_cfg_sql() {
  local tmp; tmp="$(mktemp -d)"
  win11_stop
  "$WVM" pull "$GUEST_PROFILE/config.sqlite" "$tmp" >/dev/null 2>&1 || { rm -rf "$tmp"; fail "no guest config.sqlite (start foobar2000 in the guest once)"; }
  sqlite3 "$tmp/config.sqlite" "$1"
  case "$1" in SELECT*) ;; *) "$WVM" push "$tmp/config.sqlite" "$GUEST_PROFILE" >/dev/null ;; esac
  rm -rf "$tmp"
}
win11_sibling_disable() {
  win11_stop
  "$WVM" ps "\$base = \"\$env:APPDATA\\foobar2000-v2\"; \$on = \"\$base\\user-components-x64\\$1\"; \$off = \"\$base\\.solo-off-$1\";
    if ((Test-Path \$off) -and -not (Test-Path \$on)) { exit 0 }
    if (-not (Test-Path \$on)) { exit 1 }
    Move-Item \$on \$off; exit 0" >/dev/null 2>&1
}
win11_sibling_enable() {
  win11_stop
  "$WVM" ps "\$base = \"\$env:APPDATA\\foobar2000-v2\"; \$on = \"\$base\\user-components-x64\\$1\"; \$off = \"\$base\\.solo-off-$1\";
    if ((Test-Path \$off) -and -not (Test-Path \$on)) { Move-Item \$off \$on }" >/dev/null 2>&1 || true
}
win11_shot() { mkdir -p "$(dirname "$1")"; "$WVM" shot "$1" >/dev/null && echo "$1"; }

mac_init() {
  local vm="$(cd "$REPO/.." && pwd)/macos-devbox/mvm"
  [ "$(uname)" = Darwin ] && vm="$(cd "$REPO/.." && pwd)/foo_navidrome/scripts/terminal-mac.sh"
  MVM="${MVM:-$vm}"
  [ -x "$MVM" ] || { echo "no macOS VM driver at $MVM"; exit 1; }
  LOG='~/Library/foobar2000-v2/foo_ui_panels.log'
  GUEST_CFG='~/Library/foobar2000-v2/config.sqlite'
  PLAYBACK_MENU_X=314; PLAYBACK_MENU_Y=11
}
g() { "$MVM" ssh "$@"; }
mac_running() { g 'pgrep -x foobar2000 >/dev/null'; }
mac_stop() { "$MVM" fb2k stop >/dev/null 2>&1 || true; for _ in $(seq 1 10); do mac_running || break; sleep 1; done; }
mac_start() { "$MVM" fb2k start >/dev/null; for _ in $(seq 1 30); do mac_running && break; sleep 1; done; }
mac_deploy() {
  local comp="${1:-$REPO/build-mac/foo_ui_panels.component}"
  [ "$comp" = - ] && return 0
  [ -e "$comp" ] || fail "no $comp — make mac-build first"
  say "deploy $(basename "$comp")"
  mac_stop
  "$MVM" deploy "$comp" >/dev/null
  mac_stop
}
mac_front() {
  g "osascript -e 'tell application \"foobar2000\" to activate'" >/dev/null 2>&1
  for _ in $(seq 1 20); do
    g 'lsappinfo info -only name "$(lsappinfo front)"' | grep -q foobar2000 && break
    sleep 0.5
  done
  sleep 1
}
mac_local_tracks() {
  g "test -f ~/Music/fb2k-test/silence-1.wav" || return 0
  g "open -a foobar2000 ~/Music/fb2k-test/silence-1.wav ~/Music/fb2k-test/silence-2.wav" >/dev/null
  sleep 4
  mac_stop
}
mac_play() {
  local attempt
  for attempt in 1 2 3; do
    mac_front
    "$MVM" click "$PLAYBACK_MENU_X" "$PLAYBACK_MENU_Y" >/dev/null; sleep 1
    "$MVM" type "Play" >/dev/null; "$MVM" key enter >/dev/null
    for _ in $(seq 1 6); do
      since_mark | grep -Eq 'INFO +playback .*new track' && return 0
      sleep 0.5
    done
    "$MVM" key esc >/dev/null
  done
}
mac_log_size() { g "wc -c < $LOG 2>/dev/null || echo 0" | tr -d ' '; }
mac_log_from() { g "tail -c +$1 $LOG 2>/dev/null" || true; }
mac_crash_count() { g 'ls ~/Library/Logs/DiagnosticReports 2>/dev/null | grep -c "^foobar2000" || true'; }
mac_cfg_sql() { mac_stop; g "sqlite3 $GUEST_CFG \"$1\""; }
mac_sibling_disable() {
  mac_stop
  g "on=~/Library/foobar2000-v2/user-components/$1; off=~/Library/foobar2000-v2/.solo-off-$1;
     if [ -d \$off ] && [ ! -d \$on ]; then exit 0; fi
     [ -d \$on ] || exit 1
     mv \$on \$off"
}
mac_sibling_enable() {
  mac_stop
  g "on=~/Library/foobar2000-v2/user-components/$1; off=~/Library/foobar2000-v2/.solo-off-$1;
     if [ -d \$off ] && [ ! -d \$on ]; then mv \$off \$on; fi; true"
}
mac_shot() { local src; src="$("$MVM" shot | tail -1)"; mkdir -p "$(dirname "$1")"; cp "$src" "$1" && echo "$1"; }

d() { local fn="${PLATFORM}_$1"; shift; "$fn" "$@"; }
running() { d running; }

mark() { MARK="$(d log_size)"; }
since_mark() {
  local size; size="$(d log_size)"
  [ "${size:-0}" -lt "${MARK:-0}" ] && MARK=0
  d log_from "$(( ${MARK:-0} + 1 ))"
}
wait_log() {
  local re="$1" deadline=$(( SECONDS + ${2:-20} ))
  until since_mark | grep -Eq -- "$re"; do
    [ "$SECONDS" -lt "$deadline" ] || return 1
    running || fail "foobar2000 exited while waiting for /$re/ — crashed?"
    sleep 1
  done
}
expect() { wait_log "$1" "$2" && ok "$3" || { since_mark | tail -15; fail "$3 — no /$1/ in foo_ui_panels.log within $2s"; }; }

scenario_smoke() {
  d deploy "${1:-}"
  smoke_steps
}

scenario_solo() {
  d deploy "${1:-}"
  say "move $SIBLING out of user-components (restored on exit)"
  if d sibling_disable "$SIBLING"; then
    on_exit "d sibling_enable '$SIBLING'"
    ok "$SIBLING moved out"
  else
    warn "$SIBLING isn't installed on $PLATFORM; Panels UI already runs alone"
  fi
  if declare -F "${PLATFORM}_local_tracks" >/dev/null; then
    say "local tracks in the active playlist (a $SIBLING track can't play without it)"
    d local_tracks
  fi
  smoke_steps 'navidrome=no'
}

smoke_steps() {
  say "verbose logging on"
  d cfg_sql "INSERT OR REPLACE INTO configInts VALUES ('$VERBOSE_KEY', 1);"
  say "relaunch foobar2000"
  d stop
  mark; SESSION_MARK="$MARK"
  CRASHES0="$(d crash_count)"
  d start
  running || fail "foobar2000 started — it didn't"
  expect 'NOTE +env .*foo_ui_panels .*verbose=on' 30 "session banner, verbose on"
  [ -n "${1:-}" ] && expect "NOTE +env .*$1" 5 "session banner reports $1"
  note "version" "$(since_mark | grep -oE 'foo_ui_panels [^ ]+ \([^)]+\)' | tail -1)"
  expect 'NOTE +skin .*loading skin' 20 "skin loading"
  expect 'INFO +ui .*main window created' 20 "main window created"
  sleep 3
  local warns; warns="$(since_mark | grep -cE '^[0-9:.]+ +WARN ' || true)"
  note "skin warnings" "${warns:-0}"
  say "start playback"
  mark; d play
  expect 'INFO +playback .*new track' 20 "playback started"
  sleep 2

  say "post-checks"
  running || fail "foobar2000 still running"
  ok "foobar2000 still running"
  [ "$(d crash_count)" = "$CRASHES0" ] && ok "no new crash report" || fail "no new crash report — foobar2000 wrote one"
  MARK="$SESSION_MARK"
  local errs; errs="$(since_mark | grep -cE '^[0-9:.]+ +ERROR ' || true)"
  [ "${errs:-0}" = 0 ] && ok "no ERROR lines in the log" \
    || { since_mark | grep -E '^[0-9:.]+ +ERROR ' | tail -5; fail "no ERROR lines in the log — found $errs"; }
  d shot "$SHOTS/$PLATFORM-smoke-$(date +%Y%m%d-%H%M%S).png" >/dev/null && ok "screenshot"
}

compare() {
  local scenario="${1:-}"; shift || true
  [ -n "$scenario" ] || { usage; exit 2; }
  local platforms=("$@"); [ ${#platforms[@]} -gt 0 ] || platforms=(wine win11 mac)
  local dir; dir="$(mktemp -d)"
  if [ "${PARALLEL:-0}" = 1 ]; then
    for p in "${platforms[@]}"; do
      ( RESULTS="$dir/$p.tsv" "$0" "$p" "$scenario" > "$dir/$p.out" 2>&1; echo "$?" > "$dir/$p.exit" ) &
    done
    wait
    for p in "${platforms[@]}"; do
      printf '\n\033[1;36m######## %s: %s ########\033[0m\n' "$p" "$scenario"; cat "$dir/$p.out"
    done
  else
    for p in "${platforms[@]}"; do
      printf '\n\033[1;36m######## %s: %s ########\033[0m\n' "$p" "$scenario"
      RESULTS="$dir/$p.tsv" "$0" "$p" "$scenario"; echo "$?" > "$dir/$p.exit"
    done
  fi
  printf '\n\033[1;36m==> %s: comparison\033[0m\n' "$scenario"
  python3 - "$dir" "${platforms[@]}" <<'PY'
import os, sys
d, plats = sys.argv[1], sys.argv[2:]
steps, notes, res = [], [], {}
for p in plats:
    res[p] = {}
    path = os.path.join(d, p + ".tsv")
    for line in (open(path).read().splitlines() if os.path.exists(path) else []):
        parts = line.split("\t")
        if parts[0] == "note":
            key = "note: " + parts[1]
            if key not in notes: notes.append(key)
            res[p][key] = parts[2]
        else:
            if parts[1] not in steps: steps.append(parts[1])
            res[p][parts[1]] = parts[0]
w = max([len(s) for s in steps + notes] + [10])
cw = max([len(p) for p in plats] + [8])
print("  " + "step".ljust(w) + "  " + "  ".join(p.ljust(cw) for p in plats))
for s in steps:
    cells = [res[p].get(s, "-") for p in plats]
    print(("* " if len(set(cells)) > 1 else "  ") + s.ljust(w) + "  " + "  ".join(c.ljust(cw) for c in cells))
for n in notes:
    cells = [res[p].get(n, "-") for p in plats]
    print(("* " if len(set(cells)) > 1 else "  ") + n.ljust(w) + "  " + " | ".join(cells))
codes = [open(os.path.join(d, p + ".exit")).read().strip() for p in plats]
print("\n  " + "  ".join("%s: %s" % (p, "passed" if c == "0" else "FAILED") for p, c in zip(plats, codes)))
print("  (* = the platforms differ on this row; - = step not reached)")
sys.exit(0 if all(c == "0" for c in codes) else 1)
PY
  local rc=$?; rm -rf "$dir"; return $rc
}

[ "${1:-}" = compare ] && { shift; compare "$@"; exit $?; }
PLATFORM="${1:-}"; shift || true
case "$PLATFORM" in
  wine|win11|mac) "${PLATFORM}_init" ;;
  *) usage >&2; exit 2 ;;
esac
CMD="${1:-}"; shift || true
case "$CMD" in
  smoke|solo) "scenario_$CMD" "$@"; say "$PLATFORM $CMD passed" ;;
  log)     d log_from 1 | tail -n "${1:-50}" ;;
  shot)    d shot "${1:-$SHOTS/$PLATFORM-$(date +%H%M%S).png}" ;;
  click)   [ "$PLATFORM" = wine ] || fail "click is wine only (use wvm/mvm click)"; wclick "$@" ;;
  pvars)   [ "$PLATFORM" = wine ] || fail "pvars is wine only"; wine_pvars "$@" ;;
  restart) [ "$PLATFORM" = wine ] || fail "restart is wine only (use smoke)"; wine_restart "$@" ;;
  *) usage >&2; exit 2 ;;
esac
