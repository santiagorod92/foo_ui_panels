.PHONY: help build deploy launch run kill clean check-portable test mac-build mac-vm mac-vm-test mac-vm-skin mac-vm-navidrome mac-vm-navidrome-config

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
	@echo "  test            build + run the host-side unit tests (tests/, ASan/UBSan)"
	@echo "  mac-build       build the macOS bundle (build-mac/foo_ui_panels.component, universal)"
	@echo ""
	@echo "  macOS VM (sibling ../macos-devbox, mvm — one-time setup in its README):"
	@echo "  mac-vm          boot the VM, then mac-vm-test"
	@echo "  mac-vm-test     mac-build, deploy the bundle into the VM, relaunch foobar2000, screenshot"
	@echo "  mac-vm-skin     copy skins/\$$SKIN (default: the only/first one), or SKIN_DIR=<folder>, into the guest's skin folder"
	@echo "  mac-vm-navidrome         install foo_navidrome in the VM: latest release, NAVIDROME=v1.18.0 (tag)"
	@echo "                           or NAVIDROME=path/to.fb2k-component"
	@echo "  mac-vm-navidrome-config  copy foo_navidrome's settings (server, login, custom headers) from the"
	@echo "                           local Wine foobar2000 into the VM (WINE_DB=... for another config.sqlite)"
	@echo "  mac-vm-<cmd>    any mvm command: mac-vm-shot, -ssh, -down, -snapshot ARGS=name, -click ARGS='x y', ..."

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

# Unit tests for the SDK-free parts of the engine, built with the host compiler. The include path
# deliberately has no foobar2000 SDK: a tested source that grows an SDK dependency fails here.
TEST_SRC = $(wildcard tests/*.cpp) src/core/script_util.cpp src/core/lyrics_parse.cpp src/core/skin_config.cpp src/core/ui_logic.cpp
TEST_BIN = build/tests/run_tests
test:
	@mkdir -p $(dir $(TEST_BIN))
	$(CXX_HOST) -std=c++20 -g -O1 -Wall -Wextra -fsanitize=address,undefined -fno-omit-frame-pointer \
	  -Isrc $(TEST_SRC) -o $(TEST_BIN)
	./$(TEST_BIN)

mac-build:
	./scripts/mac-build.sh

# macOS runtime test in the local VM (sibling repo ../macos-devbox, its `mvm` CLI). The bundle is
# cross-built here (universal, so it has the x86_64 slice the Intel guest needs) — no Xcode in the
# guest. Any other mvm command passes through: `make mac-vm-shot`, `make mac-vm-ssh`, ...
MVM ?= $(abspath ../macos-devbox/mvm)
SKIN ?= $(notdir $(firstword $(wildcard skins/*)))
# Any skin folder can be pushed instead, e.g. the one the local Wine foobar2000 runs:
# SKIN_DIR=~/.foobar2000/profile/user-components-x64/foo_ui_panels
SKIN_DIR ?= skins/$(SKIN)
MAC_SKIN_DIR = Library/foobar2000-v2/foo_ui_panels

mac-vm:
	$(MVM) up --wait
	$(MAKE) mac-vm-test

mac-vm-test: mac-build
	$(MVM) deploy build-mac/foo_ui_panels.component --launch
	@sleep 5; $(MVM) shot

# Single-skin mode: the skin's files go straight into the component's skin folder. The legacy
# Windows DLLs a skin may carry (dlls/, components/, a component folder's own *.dll) are useless
# on macOS and skipped.
mac-vm-skin:
	@test -d "$(SKIN_DIR)" || { echo "no skin — put one in skins/<name>, or pass SKIN=<name> / SKIN_DIR=<folder>"; exit 1; }
	tar -C "$(SKIN_DIR)" --exclude=./dlls --exclude=./components --exclude='*.dll' --exclude='*.bak' -cf - . | \
	  $(MVM) ssh 'rm -rf ~/$(MAC_SKIN_DIR) && mkdir -p ~/$(MAC_SKIN_DIR) && tar xf - -C ~/$(MAC_SKIN_DIR)'
	$(MVM) fb2k restart

# foo_navidrome in the VM (Panels UI's album browser, ratings and playlist switcher talk to it).
mac-vm-navidrome:
	./scripts/mac-vm-navidrome.sh deploy $(NAVIDROME)

mac-vm-navidrome-config:
	./scripts/mac-vm-navidrome.sh config $(WINE_DB)

mac-vm-%:
	$(MVM) $* $(ARGS)
