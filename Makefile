.PHONY: help build build-arm64ec deploy launch run kill clean check-portable test skin-lint docs mac-build mac-vm mac-vm-open mac-vm-test mac-vm-skin mac-vm-navidrome mac-vm-navidrome-config \
	ui win11 win11-open win11-test win11-release win11-skin

help:
	@echo "foo_ui_panels — make targets"
	@echo ""
	@echo "  build     cross-compile build/foo_ui_panels.dll (build.sh, Windows x64)"
	@echo "  build-arm64ec   cross-compile build-arm64ec/foo_ui_panels.dll (Windows on ARM, native ARM64EC)"
	@echo "  deploy    build, then install into the local Wine foobar2000"
	@echo "  run       build, deploy, and (re)launch foobar2000 — the main dev loop"
	@echo "  launch    kill + relaunch foobar2000 without rebuilding"
	@echo "  kill      kill any running foobar2000.exe (Wine)"
	@echo "  clean     remove build/, build-arm64ec/ and build-mac/"
	@echo "  check-portable  compile the platform-free sources (core/panels) for a non-Windows host"
	@echo "  test            build + run the host-side unit tests (tests/, ASan/UBSan)"
	@echo "  skin-lint       build build/tools/skin_lint; LINT_DIR=<folder> also runs it on that skin"
	@echo "  docs            regenerate docs/SCRIPT_FUNCTIONS.md from the engine's function table"
	@echo "  mac-build       build the macOS bundle (build-mac/foo_ui_panels.component, universal)"
	@echo "  ui              drive the Wine Panels UI: ARGS='click X Y' / 'shot FILE.png' / 'restart' (scripts/ui-test.sh)"
	@echo ""
	@echo "  macOS VM (sibling ../macos-devbox, mvm — one-time setup in its README):"
	@echo "  mac-vm          boot the VM, open its screen (VNC) in the browser, then mac-vm-test (VNC=0: no browser)"
	@echo "  mac-vm-open     open the VM screen (noVNC) in the browser once macOS has booted"
	@echo "  mac-vm-test     mac-build, deploy the bundle into the VM, relaunch foobar2000, screenshot"
	@echo "  mac-vm-skin     copy skins/\$$SKIN (default: the only/first one), or SKIN_DIR=<folder>, into the guest's skin folder"
	@echo "  mac-vm-navidrome         install foo_navidrome in the VM: latest release, NAVIDROME=v1.18.0 (tag)"
	@echo "                           or NAVIDROME=path/to.fb2k-component"
	@echo "  mac-vm-navidrome-config  copy foo_navidrome's settings (server, login, custom headers) from the"
	@echo "                           local Wine foobar2000 into the VM (WINE_DB=... for another config.sqlite)"
	@echo "  mac-vm-<cmd>    any mvm command: mac-vm-shot, -ssh, -down, -snapshot ARGS=name, -click ARGS='x y', ..."
	@echo ""
	@echo "  Windows 11 VM (sibling ../windows-devbox, wvm — one-time setup in its README):"
	@echo "  win11           boot the VM, open its screen (VNC) in the browser, then win11-test (VNC=0: no browser)"
	@echo "  win11-open      open the VM screen (noVNC) in the browser once the container's viewer answers"
	@echo "  win11-test      build, deploy the DLL + skins/\$$SKIN into the VM, relaunch foobar2000, screenshot"
	@echo "  win11-release   deploy a GitHub release [TAG=v1.6.0, default latest] + the skin, relaunch"
	@echo "  win11-skin      copy skins/\$$SKIN, or SKIN_DIR=<folder>, into the guest's component folder (default skin dir)"
	@echo "  win11-<cmd>     any wvm command: win11-shot, -ssh, -down, -theme ARGS=dark, -dpi ARGS=144, -click ARGS='x y', ..."

build:
	./build.sh

build-arm64ec:
	WIN_ARCH=arm64ec ./build.sh

deploy: build
	./scripts/deploy.sh

run: build
	./scripts/deploy.sh --launch

launch:
	./scripts/deploy.sh --launch

kill:
	pkill -f 'foobar2000.exe' 2>/dev/null || true

clean:
	rm -rf build build-arm64ec build-mac

SDK_ROOT ?= $(abspath ..)
CXX_HOST ?= clang++
check-portable:
	@set -e; for f in src/core/*.cpp src/panels/*.cpp; do \
	  $(CXX_HOST) -std=c++20 -fsyntax-only -DNDEBUG -isystem $(SDK_ROOT) -isystem $(SDK_ROOT)/foobar2000 $$f; \
	done; echo "portable: ok"

TEST_SRC = $(wildcard tests/*.cpp) src/core/script_util.cpp src/core/lyrics_parse.cpp src/core/skin_config.cpp src/core/ui_logic.cpp src/core/script_runtime.cpp src/core/skin_lint.cpp src/core/prefs_model.cpp src/core/list_logic.cpp src/core/builtin_skin.cpp src/core/skin_templates.cpp src/core/log.cpp src/core/diagnostics.cpp
TEST_BIN = build/tests/run_tests
test:
	@mkdir -p $(dir $(TEST_BIN))
	$(CXX_HOST) -std=c++20 -g -O1 -Wall -Wextra -fsanitize=address,undefined -fno-omit-frame-pointer \
	  -Isrc $(TEST_SRC) -o $(TEST_BIN)
	./$(TEST_BIN)

LINT_SRC = tools/skin_lint.cpp src/core/script_runtime.cpp src/core/script_util.cpp src/core/skin_config.cpp src/core/skin_lint.cpp
LINT_BIN = build/tools/skin_lint
$(LINT_BIN): $(LINT_SRC) $(wildcard src/core/*.h)
	@mkdir -p $(dir $(LINT_BIN))
	$(CXX_HOST) -std=c++20 -O1 -Wall -Wextra -Isrc $(LINT_SRC) -o $(LINT_BIN)
skin-lint: $(LINT_BIN)
	@if [ -n "$(LINT_DIR)" ]; then ./$(LINT_BIN) "$(LINT_DIR)"; fi

docs: $(LINT_BIN)
	@mkdir -p docs
	./$(LINT_BIN) --functions-md > docs/SCRIPT_FUNCTIONS.md

mac-build:
	./scripts/mac-build.sh

MVM ?= $(abspath ../macos-devbox/mvm)
SKIN ?= $(notdir $(firstword $(wildcard skins/*)))
SKIN_DIR ?= skins/$(SKIN)
MAC_SKIN_DIR = Library/foobar2000-v2/foo_ui_panels

VNC ?= 1
MVM_ENV = $(dir $(MVM))mvm.env

mac-vm:
	$(MVM) up
	@if [ "$(VNC)" != 0 ]; then $(MAKE) --no-print-directory mac-vm-open; fi
	$(MVM) up --wait
	$(MAKE) mac-vm-test

mac-vm-open:
	@eval "$$( [ -f "$(MVM_ENV)" ] && grep -E '^MVM_(WEB|SSH)_PORT=' "$(MVM_ENV)" )"; \
	  web=http://127.0.0.1:$${MVM_WEB_PORT:-8006}; ssh=$${MVM_SSH_PORT:-50922}; \
	  echo "waiting for macOS to boot (sshd on :$$ssh) before opening $$web ..."; \
	  for i in $$(seq 1 180); do \
	    timeout 6 bash -c "exec 3<>/dev/tcp/127.0.0.1/$$ssh && head -c4 <&3" 2>/dev/null | grep -q '^SSH-' && break; \
	    sleep 5; \
	  done; \
	  curl -fs -o /dev/null "$$web" || { echo "VM screen not answering at $$web (make mac-vm-logs)"; exit 1; }; \
	  $(MVM) web

mac-vm-test: mac-build
	$(MVM) deploy build-mac/foo_ui_panels.component --launch
	@sleep 5; $(MVM) shot

mac-vm-skin:
	@test -d "$(SKIN_DIR)" || { echo "no skin — put one in skins/<name>, or pass SKIN=<name> / SKIN_DIR=<folder>"; exit 1; }
	tar -C "$(SKIN_DIR)" --exclude=./dlls --exclude=./components --exclude='*.dll' --exclude='*.bak' -cf - . | \
	  $(MVM) ssh 'rm -rf ~/$(MAC_SKIN_DIR) && mkdir -p ~/$(MAC_SKIN_DIR) && tar xf - -C ~/$(MAC_SKIN_DIR)'
	$(MVM) fb2k restart

mac-vm-navidrome:
	./scripts/mac-vm-navidrome.sh deploy $(NAVIDROME)

mac-vm-navidrome-config:
	./scripts/mac-vm-navidrome.sh config $(WINE_DB)

mac-vm-%:
	$(MVM) $* $(ARGS)

ui:
	./scripts/ui-test.sh $(ARGS)

WVM ?= $(abspath ../windows-devbox/wvm)
WVM_ENV = $(dir $(WVM))wvm.env
WIN_COMPONENT_DIR = AppData/Roaming/foobar2000-v2/user-components-x64/foo_ui_panels

win11:
	$(WVM) up
	@if [ "$(VNC)" != 0 ]; then $(MAKE) --no-print-directory win11-open; fi
	$(WVM) up --wait
	$(MAKE) win11-test

win11-open:
	@eval "$$( [ -f "$(WVM_ENV)" ] && grep -E '^WVM_WEB_PORT=' "$(WVM_ENV)" )"; \
	  web=http://127.0.0.1:$${WVM_WEB_PORT:-8007}; \
	  echo "waiting for the VM screen at $$web ..."; \
	  for i in $$(seq 1 60); do curl -fs -o /dev/null "$$web" && break; sleep 2; done; \
	  curl -fs -o /dev/null "$$web" || { echo "VM screen not answering at $$web ($(WVM) logs)"; exit 1; }; \
	  $(WVM) web

win11-test: build
	$(WVM) deploy build/foo_ui_panels.dll
	$(MAKE) --no-print-directory win11-skin
	@sleep 5; $(WVM) shot

win11-release:
	$(WVM) deploy --gh santiagorod92/foo_ui_panels$(if $(TAG),@$(TAG))
	$(MAKE) --no-print-directory win11-skin

win11-skin:
	@test -d "$(SKIN_DIR)" || { echo "no skin — put one in skins/<name>, or pass SKIN=<name> / SKIN_DIR=<folder>"; exit 1; }
	@stage=$$(mktemp -d); trap 'rm -rf "$$stage"' EXIT; \
	  mkdir -p "$$stage/pui-skin"; \
	  tar -C "$(SKIN_DIR)" --exclude=./dlls --exclude=./components --exclude='*.dll' --exclude='*.bak' -cf - . | tar -C "$$stage/pui-skin" -xf -; \
	  find "$$stage/pui-skin" -depth -name '*[\\*?:"<>|]*' -exec rm -rf {} +; \
	  $(WVM) fb2k stop; \
	  $(WVM) push "$$stage/pui-skin" AppData/Local/Temp; \
	  $(WVM) ps "Copy-Item -Recurse -Force \"\$$HOME\\AppData\\Local\\Temp\\pui-skin\\*\" \"\$$HOME\\$(subst /,\\,$(WIN_COMPONENT_DIR))\"; Remove-Item -Recurse -Force \"\$$HOME\\AppData\\Local\\Temp\\pui-skin\""
	$(WVM) fb2k start

win11-%:
	$(WVM) $* $(ARGS)
