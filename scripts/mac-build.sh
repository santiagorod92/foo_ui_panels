#!/usr/bin/env bash
usage() {
  cat <<'USAGE'
Usage: scripts/mac-build.sh
Builds the macOS component bundle (universal arm64 + x86_64) -> build-mac/foo_ui_panels.component.
On Linux it cross-compiles with clang + ld64.lld against MACOS_SDK (default
~/.macos-sdk/MacOSX.sdk, see scripts/extract-macos-sdk.py); on a Mac it uses the native tools.
  SDK_ROOT=/path   parent of foobar2000/ + pfc/ (default: the checkout's parent)
USAGE
}
case "${1:-}" in -h|--help) usage; exit 0 ;; esac

set -euo pipefail
cd "$(dirname "$0")/.."
SDK_ROOT="${SDK_ROOT:-$(cd .. && pwd)}"
OUT=build-mac/foo_ui_panels.component
for arch in arm64 x86_64; do
  if [ "$(uname)" = Darwin ]; then
    cmake -S . -B "build-mac/$arch" -G Ninja -DCMAKE_BUILD_TYPE=Release -DSDK_ROOT="$SDK_ROOT" \
      -DCMAKE_OSX_ARCHITECTURES=$arch -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 >/dev/null
  else
    cmake -S . -B "build-mac/$arch" -G Ninja -DCMAKE_BUILD_TYPE=Release -DSDK_ROOT="$SDK_ROOT" \
      -DCMAKE_TOOLCHAIN_FILE=cmake/clang-macos.cmake -DMAC_ARCH=$arch >/dev/null
  fi
  ninja -C "build-mac/$arch" foo_ui_panels
done
rm -rf "$OUT"
cp -R build-mac/arm64/foo_ui_panels.component "$OUT"
LIPO=$(command -v lipo || command -v llvm-lipo)
"$LIPO" -create build-mac/arm64/foo_ui_panels.component/Contents/MacOS/foo_ui_panels \
  build-mac/x86_64/foo_ui_panels.component/Contents/MacOS/foo_ui_panels \
  -output "$OUT/Contents/MacOS/foo_ui_panels"
if command -v codesign >/dev/null; then codesign --force --sign - "$OUT"; fi
archs=$("$LIPO" -archs "$OUT/Contents/MacOS/foo_ui_panels")
for want in arm64 x86_64; do
  case " $archs " in
    *" $want "*) ;;
    *) echo "mac-build: missing $want slice (got: $archs)" >&2; exit 1 ;;
  esac
done
echo "built: $OUT ($archs)"
