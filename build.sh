#!/usr/bin/env bash
usage() {
  cat <<'USAGE'
Usage: ./build.sh
Cross-builds foo_ui_panels.dll (Windows) on Linux with clang-cl + lld-link + xwin.
  WIN_ARCH=x64      (default) -> build/foo_ui_panels.dll
  WIN_ARCH=arm64ec  Windows on ARM, native -> build-arm64ec/foo_ui_panels.dll
  SDK_ROOT=/path    parent of foobar2000/{SDK,shared,...} + pfc/ (default: the checkout's parent;
                    fetched from reupen/foobar2000-sdk-unmodified when missing)
  XWIN=/path        xwin splat (default ~/.xwin)
Prereqs: clang-cl, lld-link, cmake, ninja, and
  cargo install xwin && xwin --accept-license --arch x86_64,aarch64 splat --output ~/.xwin
USAGE
}
case "${1:-}" in -h|--help) usage; exit 0 ;; esac

set -euo pipefail
cd "$(dirname "$0")"

SDK_ROOT="${SDK_ROOT:-$(cd .. && pwd)}"
XWIN="${XWIN:-$HOME/.xwin}"
export XWIN
WIN_ARCH="${WIN_ARCH:-x64}"
case "$WIN_ARCH" in
  x64) BUILD_DIR=build ;;
  arm64ec) BUILD_DIR=build-arm64ec ;;
  *) echo "WIN_ARCH must be x64 or arm64ec (got '$WIN_ARCH')" >&2; exit 1 ;;
esac

if [ ! -d "$SDK_ROOT/foobar2000/SDK" ] || [ ! -d "$SDK_ROOT/pfc" ]; then
  echo "foobar2000 SDK not found under $SDK_ROOT - fetching reupen/foobar2000-sdk-unmodified ..."
  staging="$(mktemp -d)"
  trap 'rm -rf "$staging"' EXIT
  git clone --depth 1 https://github.com/reupen/foobar2000-sdk-unmodified.git "$staging/sdk"
  mkdir -p "$SDK_ROOT/foobar2000"
  for d in SDK helpers shared foobar2000_component_client; do
    [ -e "$SDK_ROOT/foobar2000/$d" ] || mv "$staging/sdk/foobar2000/$d" "$SDK_ROOT/foobar2000/$d"
  done
  [ -e "$SDK_ROOT/pfc" ] || mv "$staging/sdk/pfc" "$SDK_ROOT/pfc"
fi

grep -rhoE '#include <[^>]+>' "$SDK_ROOT/pfc" "$SDK_ROOT/foobar2000/SDK" \
     "$SDK_ROOT/foobar2000/foobar2000_component_client" src 2>/dev/null \
  | sed -E 's/#include <([^>]+)>/\1/' | grep -E '[A-Z]' | sort -u \
  | while read -r inc; do
      base=$(basename "$inc")
      real=$(find "$XWIN" -type f -iname "$base" 2>/dev/null | head -1) || true
      [ -n "${real:-}" ] && [ ! -e "$(dirname "$real")/$base" ] && \
        ln -s "$(basename "$real")" "$(dirname "$real")/$base" || true
    done

cmake -S . -B "$BUILD_DIR" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/clang-cl-win64.cmake \
  -DCMAKE_BUILD_TYPE=Release \
  -DWIN_ARCH="$WIN_ARCH" \
  -DSDK_ROOT="$SDK_ROOT"
ninja -C "$BUILD_DIR"
echo "built: $BUILD_DIR/foo_ui_panels.dll ($WIN_ARCH)"
