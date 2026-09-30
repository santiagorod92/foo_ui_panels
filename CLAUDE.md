# foo_ui_panels — reborn (foobar2000 v2)

Ground-up reimplementation of discontinued **Panels UI** (`foo_ui_panels`, ~fb2k 0.9.5.2)
for **foobar2000 v2 — Windows x64 + macOS (universal)**. Original closed-source — skin compatibility goal, not recompile.
See `DESIGN.md` for roadmap/facts, `FORMAT.md` for legacy type→DUI mapping.

## Working rule: every change is cross-platform
Port started 2026-09-29 (foo_navidrome's rule, its CLAUDE.md "Working rule"): every feature/fix
lands on BOTH platforms, both builds + CI legs green, release zip = `mac/` bundle + `x64/` DLL.
- Logic (script engine, panels, models, actions) is platform-free C++ in `src/core` /
  `src/panels`, drawing via `gfx::Canvas` and windowing via `ui::View`/`ui::ViewHost`/
  `ui::MainWindow` + the `ui::` service functions. No `HWND`/`HDC`/Cocoa there — only
  `#include "../fb2k.h"`, never `win_sdk.h`. `make check-portable` (also in CI) enforces it.
- A new platform need = a new method/function in `src/gfx/canvas.h` or `src/ui/view.h`,
  implemented in BOTH `src/platform/win/` and `src/platform/mac/`.
- macOS integration: a `ui_element_mac` layout element ("Panels UI") hosting the whole canvas,
  not `user_interface` (whether fb2k Mac accepts a replacement UI module is still unverified).
- macOS runtime is untested so far: no Mac, and foo_navidrome's `scripts/mac-vm/` needs its
  one-time manual macOS install (VNC) before it can run anything.

## Key decisions
- Target service (Windows): full UI replacement via `user_interface` (`foobar2000/SDK/ui.h`, v1–v4),
  `FB2K_MAKE_SERVICE_INTERFACE_ENTRYPOINT`. Same slot as foo_ui_classic / Columns UI.
- Build (Windows): **cross-compile on Linux** (clang-cl + lld-link + xwin Windows SDK/CRT). No MSVC.
- Build (macOS): CMake `APPLE` branch, same SDK/pfc/shared source sets as the SDK's .xcodeproj
  targets. Native on a Mac/CI, or cross on Linux (clang + ld64.lld, `cmake/clang-macos.cmake`,
  MacOSX.sdk from an Xcode .xip via `scripts/extract-macos-sdk.py`).
- Reference skin / format spec source: **fooAvA 1.05** (dawxxx666). Its config XML = the
  Panels-UI format to reverse-engineer. Full fooAvA also needs `foo_chronflow` (dead, not replicated).
- Architecture: Panels UI is a scripted absolute-positioning canvas (`SkinEngine`), not a
  splitter tree. Hosted panels live in one name-keyed registry (`SkinEngine::Slot`).
- Transparent panels show what's beneath them from engine-kept frames, never by reading
  screen/sibling pixels: `snapshot_canvas`/`draw_canvas_snapshot` (master canvas) and
  `store_panel_frame`/`backdrop_for` (a panel's last frame, e.g. spectrum over the cover).
- Cross-component integration (e.g. `foo_navidrome`, same author): when a track needs something
  only another of our own fb2k components can do correctly (e.g. rating a `navidrome://` track —
  a normal file-tag write fails, no real file backs it), that component exposes a small
  `service_base` interface; we vendor a GUID-matched copy of just the contract header
  (`src/core/navidrome_rating_api.h`) and find it via `service_enum_t` at runtime — no build coupling,
  no-op if the other component isn't installed. See `skin_engine.cpp`'s `set_rating()`. Pattern +
  full consumer list documented in `foo_navidrome`'s own CLAUDE.md ("Cross-compatibility with
  other same-author components") — add a symmetric note there for any new interface.

## Repo conventions
- Remote: `origin` → github.com/santiagorod92/foo_ui_panels (private). Branch `main`.
- **Commits: author Santiago Rodriguez <santiagom9992@gmail.com> ONLY — no Co-Authored-By trailer.**
- **Don't push per-step.** Iterate locally; push only at end of an iteration round or on request.
- **This file is for stable rules/decisions only** — don't append per-iteration debugging
  narrative, root-cause essays, or session logs here. That goes in commit messages; durable
  non-obvious gotchas can get one terse bullet under the relevant section below, not a paragraph.
- Never commit: `.xwin-cache/` (MS binaries), `skins/` (legacy Panels UI DLLs + fooAvA art,
  CC BY-NC-SA by dawxxx666 — never published, not even in releases), `build/`,
  `*.fb2k-component`. All gitignored — keep local.
- Repo is public: the release ships ONLY our binaries (DLL + mac bundle); fooAvA is credited + linked (its
  DeviantArt page) in README, never redistributed.

## Code map
- `src/fb2k.h` — the SDK include for platform-free code (pulls `win_sdk.h` only on Windows).
- `src/gfx/canvas.h` — `gfx::Canvas` (shapes, UTF-8 text, glyph coverage for glow, images,
  snapshots) + `gfx::Image` + platform image decoding. Text flags mirror DrawText's.
- `src/ui/view.h` — `ui::View` (panel logic: paint/input/timers), `ui::ViewHost` (platform child
  window), `ui::MainWindow` (what scripts do to the window), text field, menus, track context
  menu, colour picker, editor window, embedded foreign UI element.
- `src/core/skin_engine.{h,cpp}` — the interpreter: titleformat hook, draw funcs, panel
  registry/dispatch, button actions, pvars, theme colour. Core architecture — read this first.
- `src/core/image_cache.{h,cpp}` — image cache, wildcard cover paths, album-art pipeline.
- `src/core/skin_paths.{h,cpp}` — skin folder settings/resolution (`component_dir()` is platform).
- `src/core/component.cpp` — `DECLARE_COMPONENT_VERSION`, built-in fallback skin.
- `src/core/button.h`, `fs_util.h` (UTF-8 paths/files), vendored `navidrome_*_api.h` (GUIDs
  are C++17 `inline` variables — `FOOGUIDDECL` is Windows-only `selectany`).
- `src/panels/` — native panels, one `ui::View` each: `track_display`, `seekbar`, `volume`,
  `playlist_view`, `spectrum` (painted on the host's render thread, `render_fps`), `popup`
  (settings popup), `album_list` ("Graphical Browser"/"Album list"/"Chronflow" — no stock DUI
  element; Media Library + Navidrome albums, grid or cover flow), `lyrics_panel` ("Lyric Show":
  LRC sync, tags → sidecar → cache → lrclib.net; `lyr.*` pvars), `quick_search`, `library_tree`
  ("Playlist switcher", self-drawn tree).
- `src/platform/win/` — `main.cpp` (`user_interface`, main window = `ui::MainWindow`, menus,
  keyboard), `win_view.cpp` (`WinViewHost` child HWNDs + every `ui::` service),
  `gdi_canvas.{h,cpp}` (GDI + GDI+), `panel_host` (hosts a DUI `ui_element` by name),
  `preferences.cpp` (Preferences page, plain Win32 — no ATL/WTL in our toolchain).
- `src/platform/mac/` — `mac_main.mm` (`ui_element_mac` "Panels UI", root canvas =
  `ui::MainWindow`; skins in `~/Library/foobar2000-v2/foo_ui_panels`), `mac_view.mm` (flipped
  NSView hosts + every `ui::` service), `mac_canvas.{h,mm}` (CoreGraphics/CoreText/ImageIO,
  1 canvas px = 1 pt, dpi 96). No Preferences page yet.
- Text drawing on Windows: **always wide APIs** (GdiCanvas converts UTF-8). `DrawTextA`
  mojibakes non-ASCII foobar strings (titleformat output, tags).
- fooAvA config decode tooling: `tools/extract_fooava.py` (s8.bin → `fooava.txt` + `panels/*.txt`,
  handles paren-balancing/pvar-seeding needed for fb2k titleformat to actually execute),
  `tools/recover_fooava_images.py` (ClickTeam SFX asset extraction). Don't reintroduce the old
  size-collision image-mapping heuristic — it mismapped same-size PNGs; offset-based mapping is exact.
- `tools/wclick.c` + `scripts/ui-test.sh` — drive the Wine foobar2000 UI (post clicks/keys/text
  to the Panels UI windows, screenshot it, edit pvars in config.sqlite, restart with a DLL).

## Build (Arch Linux cross-compile)
- Prereqs: `clang-cl`, `lld-link`, `cmake`, `ninja`; `cargo install xwin && xwin --accept-license splat --output ~/.xwin`.
- Build: `./build.sh` (or `make build`) → `build/foo_ui_panels.dll` (PE32+ x64, exports `foobar2000_get_interface`).
- macOS: `./scripts/mac-build.sh` (`make mac-build`) → `build-mac/foo_ui_panels.component`
  (universal; per-arch trees in `build-mac/<arch>`). `make check-portable` = core/panels compile
  with the host clang, no Windows headers.
- `make deploy` / `make run` — see Deploy/test below.
- Toolchain file: `cmake/clang-cl-win64.cmake`.
- SDK lives OUTSIDE the repo, as siblings of the checkout (same layout as foo_navidrome, which
  shares it): `../foobar2000/{SDK,foobar2000_component_client,shared}` + `../pfc`. `SDK_ROOT`
  (CMake cache var / build.sh env) overrides; build.sh fetches it if missing. Include SDK headers
  as `<foobar2000/SDK/...>` — never a relative `../sdk/...` path.
- SDK source: `reupen/foobar2000-sdk-unmodified` (not `razielanarki/foobar2000-sdk` — stale
  mirror). Currently SDK-2026-09-17. The 2026-09-16 sync bumped `pfc-lite.h` to require C++20 —
  `CMakeLists.txt`'s `CMAKE_CXX_STANDARD` must stay at 20.
- Version: `version.txt` (written by semantic-release) → `PUI_VERSION` define → main.cpp's
  `DECLARE_COMPONENT_VERSION`.

## CI / release
- `.github/workflows/build.yml` — build check on push/PR: Windows DLL + `check-portable`
  (ubuntu) and the universal mac bundle (macos-latest), both as artifacts.
- `.github/workflows/release.yml` — semantic-release (`.releaserc.json`, Conventional Commits,
  same config as foo_navidrome) on push to main; `scripts/release-build.sh <ver>` builds and
  packages `foo_ui_panels_<ver>.fb2k-component` (DLL under `x64/`, `scripts/package.py`); then
  `release-mac` builds the bundle from the tag and `release-package` re-uploads the asset with
  `mac/` added (`--clobber`) before `notify-n8n`. Commit type = release impact:
  `feat` minor, `fix`/`perf`/`refactor` patch, `chore`/`docs`/`ci`/`style`/`test` none.
- Shared CI setup: `.github/actions/fetch-sdk` (SDK sibling layout) and
  `.github/actions/setup-build` (that + clang-cl/xwin).
- `notify-n8n` job → n8n workflow (infra-foundations `n8n/`) that uploads to foobar2000.org
  componentsadmin. Gated on repo variable `FOOBAR_ORG_PUBLISH=true`; needs a homelab runner
  registered for this repo (infra-foundations `github_runner_instances`) + the
  `N8N_FOOBAR_RELEASE_WEBHOOK_URL/_SECRET` secrets.

### Cross-build gotchas (handled in build.sh/toolchain, don't re-discover)
- Static release CRT only: xwin ships no debug CRT → toolchain forces `/MT` Release.
- Header case-sensitivity: SDK includes cased headers; xwin lowercases — build.sh symlinks cased names.
- Include order in main.cpp: `WIN32_LEAN_AND_MEAN`+`<winsock2.h>` before `<windows.h>`, then
  `<objbase.h>` (COM `interface` macro) + `<mmsystem.h>` (`timeGetTime`).
- Link `shared-x64.lib` (fb2k `shared.dll` import lib, at `../foobar2000/shared/`).
- libPPUI rich controls deferred to Phase 2+ (needs WTL+ATL, not in xwin).

## Deploy/test (Wine)
- foobar2000 v2.25.9 at `~/.foobar2000/`.
- Deploy DLL to `~/.foobar2000/profile/user-components-x64/foo_ui_panels/foo_ui_panels.dll`.
  Fully restart fb2k to reload.
- Full UI replacement must forward `WM_KEYDOWN`/`WM_SYSKEYDOWN` to
  `keyboard_shortcut_manager::on_keydown_auto(wp)` or shortcuts break.
