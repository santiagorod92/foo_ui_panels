#!/usr/bin/env bash
# Cross-build foo_ui_panels.dll (Windows x64) on Linux via clang-cl + xwin.
# Prereqs: clang-cl, lld-link, cmake, ninja, and `cargo install xwin && xwin --accept-license splat --output ~/.xwin`
#
# The foobar2000 SDK is NOT part of this repo: it lives next to the checkout, the same sibling
# layout foo_navidrome uses (and shares, when both are cloned side by side):
#
#   <parent>/
#     foobar2000/{SDK,foobar2000_component_client,shared,...}
#     pfc/
#     foo_ui_panels/        <- this repo
#
# If it's missing, it is fetched from reupen/foobar2000-sdk-unmodified (the unmodified official
# SDK mirror). Override the location with SDK_ROOT=/path, the xwin splat with XWIN=/path.
set -euo pipefail
cd "$(dirname "$0")"

SDK_ROOT="${SDK_ROOT:-$(cd .. && pwd)}"
XWIN="${XWIN:-$HOME/.xwin}"
export XWIN

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

# --- Fix Windows-SDK case-sensitivity: SDK sources #include caps-cased headers
# (e.g. <SDKDDKVer.h>) but xwin lowercases filenames. Symlink the cased names. ---
grep -rhoE '#include <[^>]+>' "$SDK_ROOT/pfc" "$SDK_ROOT/foobar2000/SDK" \
     "$SDK_ROOT/foobar2000/foobar2000_component_client" src 2>/dev/null \
  | sed -E 's/#include <([^>]+)>/\1/' | grep -E '[A-Z]' | sort -u \
  | while read -r inc; do
      base=$(basename "$inc")
      real=$(find "$XWIN" -type f -iname "$base" 2>/dev/null | head -1) || true
      [ -n "${real:-}" ] && [ ! -e "$(dirname "$real")/$base" ] && \
        ln -s "$(basename "$real")" "$(dirname "$real")/$base" || true
    done

cmake -S . -B build -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/clang-cl-win64.cmake \
  -DCMAKE_BUILD_TYPE=Release \
  -DSDK_ROOT="$SDK_ROOT"
ninja -C build
echo "built: build/foo_ui_panels.dll"
