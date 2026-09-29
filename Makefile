.PHONY: help build deploy launch run kill clean check-portable mac-build

help:
	@echo "foo_ui_panels — make targets"
	@echo ""
	@echo "  build     cross-compile build/foo_ui_panels.dll (build.sh)"
	@echo "  deploy    build, then install into the local Wine foobar2000"
	@echo "  run       build, deploy, and (re)launch foobar2000 — the main dev loop"
	@echo "  launch    kill + relaunch foobar2000 without rebuilding"
	@echo "  kill      kill any running foobar2000.exe (Wine)"
	@echo "  clean     remove build/ and build-mac/"
	@echo "  check-portable  compile the platform-free sources (core/panels) for a non-Windows host"
	@echo "  mac-build       build the macOS bundle (build-mac/foo_ui_panels.component, universal)"

build:
	./build.sh

deploy: build
	./scripts/deploy.sh

run: build
	./scripts/deploy.sh --launch

launch:
	./scripts/deploy.sh --launch

kill:
	pkill -f 'foobar2000.exe' 2>/dev/null || true

clean:
	rm -rf build build-mac

# Platform-free code must build without <windows.h>: syntax-check it with the host compiler
# against the SDK headers (no _WIN32). Catches Win32 leaking into src/core or src/panels.
SDK_ROOT ?= $(abspath ..)
CXX_HOST ?= clang++
check-portable:
	@set -e; for f in src/core/*.cpp src/panels/*.cpp; do \
	  $(CXX_HOST) -std=c++20 -fsyntax-only -DNDEBUG -I$(SDK_ROOT) -I$(SDK_ROOT)/foobar2000 $$f; \
	done; echo "portable: ok"

mac-build:
	./scripts/mac-build.sh
