#!/usr/bin/env bash
# mac-build.sh — build the macOS component bundle (universal arm64 + x86_64).
# On Linux: cross-compiles with clang + ld64.lld against a MacOSX.sdk (MACOS_SDK, default
# ~/.macos-sdk/MacOSX.sdk — see scripts/extract-macos-sdk.py). On a Mac: native clang/Xcode tools.
# Output: build-mac/foo_ui_panels.component
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
# Ad-hoc signature (arm64 macOS refuses unsigned code). ld64.lld already signs each slice on
# Linux; on a Mac re-sign the whole bundle.
if command -v codesign >/dev/null; then codesign --force --sign - "$OUT"; fi
echo "built: $OUT"
