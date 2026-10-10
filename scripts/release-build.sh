#!/usr/bin/env bash
usage() {
  cat <<'USAGE'
Usage: scripts/release-build.sh <version>
Builds both Windows DLLs (x64 + ARM64EC) with PUI_VERSION=<version> and packages
foo_ui_panels_<version>.fb2k-component (x64/ + arm64ec/) in the repo root.
Called by semantic-release (.releaserc.json prepareCmd); writes released_version to
$GITHUB_OUTPUT when run in Actions.
USAGE
}
case "${1:-}" in -h|--help) usage; exit 0 ;; esac

set -euo pipefail

[ $# -ge 1 ] || { usage >&2; exit 2; }
VERSION="$1"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

export PUI_VERSION="$VERSION"

if [ -n "${GITHUB_OUTPUT:-}" ]; then
  echo "released_version=$VERSION" >> "$GITHUB_OUTPUT"
fi

WIN_ARCH=x64 ./build.sh
WIN_ARCH=arm64ec ./build.sh

ASSET="foo_ui_panels_${VERSION}.fb2k-component"
rm -f "$ASSET"
python3 scripts/package.py "$ASSET" --dll build/foo_ui_panels.dll \
  --arm64ec build-arm64ec/foo_ui_panels.dll
echo "packaged: $ASSET"
