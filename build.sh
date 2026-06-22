#!/usr/bin/env bash
# Cross-build foo_ui_panels.dll (Windows x64) on Linux via clang-cl + xwin.
# Prereqs: clang-cl, lld-link, cmake, ninja, and `cargo install xwin && xwin --accept-license splat --output ~/.xwin`
set -euo pipefail
cd "$(dirname "$0")"

# --- Fix Windows-SDK case-sensitivity: SDK sources #include caps-cased headers
# (e.g. <SDKDDKVer.h>) but xwin lowercases filenames. Symlink the cased names. ---
grep -rhoE '#include <[^>]+>' sdk/pfc sdk/foobar2000/SDK sdk/foobar2000/foobar2000_component_client src 2>/dev/null \
  | sed -E 's/#include <([^>]+)>/\1/' | grep -E '[A-Z]' | sort -u \
  | while read -r inc; do
      base=$(basename "$inc")
      real=$(find "$HOME/.xwin" -type f -iname "$base" 2>/dev/null | head -1) || true
      [ -n "${real:-}" ] && [ ! -e "$(dirname "$real")/$base" ] && \
        ln -s "$(basename "$real")" "$(dirname "$real")/$base" || true
    done

cmake -S . -B build -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/clang-cl-win64.cmake \
  -DCMAKE_BUILD_TYPE=Release
ninja -C build
echo "built: build/foo_ui_panels.dll"
