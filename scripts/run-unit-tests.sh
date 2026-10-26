#!/usr/bin/env bash
usage() {
  cat <<'USAGE'
Usage: scripts/run-unit-tests.sh [host|win|win11-arm]
Builds and runs the unit tests (tests/*.cpp over the platform-free src/core sources).
  host       (default) native compiler ($CXX_HOST, default clang++) with ASan/UBSan: Linux, macOS
  win        clang-cl + xwin, run under wine (Linux)
  win11-arm  clang-cl + xwin on a Mac, run in the Windows 11 ARM VM
             (../foo_navidrome/scripts/win11-arm.sh, override with WIN11_ARM)
The Windows modes run the _WIN32 branches (fs_util.h's wide-path file access) that the host
build never compiles. tests/, docs/ and res/ are copied next to the exe: the tests read them
through repo-relative paths (golden files, docs/SCRIPT_FUNCTIONS.md, res/wizard thumbnails).
USAGE
}
case "${1:-}" in -h|--help) usage; exit 0 ;; esac

set -euo pipefail
cd "$(dirname "$0")/.."
MODE="${1:-host}"
python3 scripts/i18n-check.py
SRC=(tests/*.cpp src/core/script_util.cpp src/core/lyrics_parse.cpp src/core/skin_config.cpp src/core/ui_logic.cpp
     src/core/script_runtime.cpp src/core/skin_lint.cpp src/core/prefs_model.cpp src/core/list_logic.cpp
     src/core/builtin_skin.cpp src/core/skin_templates.cpp src/core/log.cpp src/core/diagnostics.cpp
     src/core/i18n.cpp src/core/i18n_table.cpp)

win_build() {
  . scripts/toolchain.sh
  command -v clang-cl >/dev/null || { echo "clang-cl not found (Linux: pacman -S clang lld; macOS: brew install llvm lld)" >&2; exit 1; }
  [ -f "$XWIN/crt/include/vcruntime.h" ] || { echo "xwin splat not found at $XWIN (see build.sh --help)" >&2; exit 1; }
  local out=build/tests-win tp=()
  mkdir -p "$out"
  for f in "${SRC[@]}"; do tp+=("/Tp$PWD/$f"); done
  echo "==> unit tests: clang-cl (Windows x64)" >&2
  clang-cl --target=x86_64-pc-windows-msvc -fuse-ld=lld-link /std:c++20 /EHsc /MT /GR /O1 /W3 /utf-8 \
    /D_CRT_SECURE_NO_WARNINGS "-I$PWD/src" \
    "/imsvc$XWIN/crt/include" "/imsvc$XWIN/sdk/include/ucrt" "/imsvc$XWIN/sdk/include/um" "/imsvc$XWIN/sdk/include/shared" \
    "${tp[@]}" "/Fe$out/run_tests.exe" "/Fo$out/" /link \
    "/libpath:$XWIN/crt/lib/x86_64" "/libpath:$XWIN/sdk/lib/ucrt/x86_64" "/libpath:$XWIN/sdk/lib/um/x86_64" >&2
  echo "$out/run_tests.exe"
}

case "$MODE" in
  host)
    mkdir -p build/tests
    "${CXX_HOST:-clang++}" -std=c++20 -g -O1 -Wall -Wextra -fsanitize=address,undefined -fno-omit-frame-pointer \
      -Isrc "${SRC[@]}" -o build/tests/run_tests
    ./build/tests/run_tests
    ;;
  win)
    exe="$(win_build)"
    command -v wine >/dev/null || { echo "wine not found" >&2; exit 1; }
    WINEDEBUG=-all wine "$exe"
    ;;
  win11-arm)
    exe="$(win_build)"
    vm="${WIN11_ARM:-$(cd .. && pwd)/foo_navidrome/scripts/win11-arm.sh}"
    [ -x "$vm" ] || { echo "no $vm (clone foo_navidrome next to this repo, or set WIN11_ARM)" >&2; exit 1; }
    for d in tests docs res; do "$vm" push "$d" AppData/Local/Temp; done
    "$vm" run "$exe"
    ;;
  *) usage; exit 2 ;;
esac
