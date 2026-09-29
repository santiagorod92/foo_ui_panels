#!/usr/bin/env bash
# release-build.sh <version> — build + package a release. Called by semantic-release
# (.releaserc.json prepareCmd) with the version it computed; also runnable locally.
#
# 1. Writes <version> to version.txt (single source of truth; CMakeLists.txt stamps it into
#    the DLL's DECLARE_COMPONENT_VERSION, semantic-release commits it back to main).
# 2. Exports released_version to $GITHUB_OUTPUT (when running in Actions) so later jobs
#    (notify-n8n) know which release was cut.
# 3. Cross-builds build/foo_ui_panels.dll (./build.sh).
# 4. Packages foo_ui_panels_<version>.fb2k-component in the repo root (scripts/package.py):
#    the DLL under x64/ — foobar2000 v2's layout for 64-bit binaries.
set -euo pipefail

VERSION="${1:?usage: release-build.sh <version>}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

echo "$VERSION" > version.txt

if [ -n "${GITHUB_OUTPUT:-}" ]; then
  echo "released_version=$VERSION" >> "$GITHUB_OUTPUT"
fi

./build.sh

ASSET="foo_ui_panels_${VERSION}.fb2k-component"
rm -f "$ASSET"
# Windows only here; the release workflow adds the macOS bundle (mac/) in a later job.
python3 scripts/package.py "$ASSET" --dll build/foo_ui_panels.dll
echo "packaged: $ASSET"
