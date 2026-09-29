# foo_ui_panels — reborn (foobar2000 v2)

Ground-up reimplementation of discontinued **Panels UI** (`foo_ui_panels`, ~fb2k 0.9.5.2)
for **foobar2000 v2 (64-bit), Windows** (macOS planned). Original closed-source — skin compatibility goal, not recompile.
See `DESIGN.md` for roadmap/facts, `FORMAT.md` for legacy type→DUI mapping.

## Platform scope: Windows now, macOS later (deferred)
Ships **Windows x64 only** for now. **macOS is a planned future goal** (README "Roadmap"): one
`.fb2k-component` for both, like `foo_navidrome` (its CLAUDE.md "Working rule"). Deferred by
the user on 2026-09-29 — don't start the port unprompted. Meanwhile, keep the door open:
- New logic (script engine, models, actions) goes in platform-free C++ where it costs nothing
  extra; keep `HWND`/`HDC`/GDI confined to drawing/window/menu code, don't spread it further.
- When the port starts, this becomes foo_navidrome's rule: every feature/fix lands on BOTH
  platforms (Win32 `.cpp` + Cocoa `.mm`), both builds + CI legs green, release zip = `mac/`
  bundle + `x64/` DLL. First step then: verify on the mac-vm (foo_navidrome `scripts/mac-vm/`)
  whether fb2k Mac allows a `user_interface` module (`init` returns an `NSWindow` per SDK
  `ui.h`) or needs `ui_element_mac` panels instead.

## Key decisions
- Target service: full UI replacement via `user_interface` (`foobar2000/SDK/ui.h`, v1–v4),
  `FB2K_MAKE_SERVICE_INTERFACE_ENTRYPOINT`. Same slot as foo_ui_classic / Columns UI.
- Build (Windows): **cross-compile on Linux** (clang-cl + lld-link + xwin Windows SDK/CRT). No MSVC.
- Reference skin / format spec source: **fooAvA 1.05** (dawxxx666). Its config XML = the
  Panels-UI format to reverse-engineer. Full fooAvA also needs `foo_chronflow` (dead, not replicated).
- Architecture: Panels UI is a scripted absolute-positioning canvas (`SkinEngine`), not a
  splitter tree — `splitter.{h,cpp}` exists but is unused by the current design.
- Cross-component integration (e.g. `foo_navidrome`, same author): when a track needs something
  only another of our own fb2k components can do correctly (e.g. rating a `navidrome://` track —
  a normal file-tag write fails, no real file backs it), that component exposes a small
  `service_base` interface; we vendor a GUID-matched copy of just the contract header
  (`src/navidrome_rating_api.h`) and find it via `service_enum_t` at runtime — no build coupling,
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
- Repo is headed public: the release ships ONLY the DLL; fooAvA is credited + linked (its
  DeviantArt page) in README, never redistributed.

## Code map
- `src/win_sdk.h` — canonical SDK include block (right order for clang-cl). Include this, not raw windows.h.
- `src/main.cpp` — `user_interface` impl: main window, menu, keyboard, hosts root splitter.
- `src/splitter.{h,cpp}` — unused legacy layout engine (raw Win32, no ATL/WTL).
- `src/panel_host.{h,cpp}` — hosts a DUI `ui_element` by name inside a pane.
- `src/skin_engine.{h,cpp}` — the interpreter: titleformat-based scripted canvas, draw funcs,
  panel dispatch, button/action handling, theme colour. Core architecture — read this first.
- `src/track_display.{h,cpp}`, `src/seekbar.{h,cpp}`, `src/volume.{h,cpp}`,
  `src/playlist_view.{h,cpp}`, `src/spectrum.{h,cpp}`, `src/popup.{h,cpp}`,
  `src/album_list.{h,cpp}` — native panel impls (`album_list` = "Graphical Browser"/"Album list"
  legacy panel types — no stock DUI element to host, so it's native like the others, not a
  pixel-exact fooAvA recreation; scans the Media Library, tile grid, click plays the album).
- `src/lyrics_panel.{h,cpp}` — native "Lyric Show" panel: cover-art background, LRC sync/highlight,
  tags → sidecar → cache → lrclib.net lookup; settings are `lyr.*` pvars edited from its right-click menu
  (also reachable from the CD-case frame).
- `src/navidrome_library_api.h` — vendored copy of `foo_navidrome`'s library-publishing contract
  (album list, cover bytes, play album); `album_list.cpp` merges those albums into the Media Library grid.
- `src/navidrome_rating_api.h` — vendored copy of `foo_navidrome`'s cross-component service
  contract (see "Cross-component integration" above). Interface + GUID only, no implementation.
- `src/image.{h,cpp}` — GDI+ image loading/drawing, cover art resolution (incl. wildcard paths).
- `src/button.h` — shared `Button`/`Placement` types (breaks skin_engine↔track_display include cycle).
- `src/preferences.cpp` — Preferences page (Display > Panels UI (reborn)): edits fooava.txt and
  the skin folder override (plain Win32 EDIT/BUTTON controls, no ATL/WTL — not in our toolchain).
  Applying rewrites the file but doesn't hot-reload; needs_restart tells fb2k to prompt for one.
- `src/skin_paths.h` — `resolve_skin_dir()`: skin folder override (from preferences.cpp) if set,
  else the component's own folder. Shared between main.cpp (initial load) and preferences.cpp.
- Text drawing: **always `DrawTextW`** (via `dtW` UTF-8→UTF-16 helper). `DrawTextA` mojibakes
  non-ASCII foobar strings (titleformat output, tags).
- fooAvA config decode tooling: `tools/extract_fooava.py` (s8.bin → `fooava.txt` + `panels/*.txt`,
  handles paren-balancing/pvar-seeding needed for fb2k titleformat to actually execute),
  `tools/recover_fooava_images.py` (ClickTeam SFX asset extraction). Don't reintroduce the old
  size-collision image-mapping heuristic — it mismapped same-size PNGs; offset-based mapping is exact.

## Build (Arch Linux cross-compile)
- Prereqs: `clang-cl`, `lld-link`, `cmake`, `ninja`; `cargo install xwin && xwin --accept-license splat --output ~/.xwin`.
- Build: `./build.sh` (or `make build`) → `build/foo_ui_panels.dll` (PE32+ x64, exports `foobar2000_get_interface`).
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
- `.github/workflows/build.yml` — build check + DLL artifact on push/PR.
- `.github/workflows/release.yml` — semantic-release (`.releaserc.json`, Conventional Commits,
  same config as foo_navidrome) on push to main; `scripts/release-build.sh <ver>` builds and
  packages `foo_ui_panels_<ver>.fb2k-component` (DLL under `x64/`). Commit type = release impact:
  `feat` minor, `fix`/`perf`/`refactor` patch, `chore`/`docs`/`ci`/`style`/`test` none.
- Shared CI setup: `.github/actions/setup-build` (SDK sibling layout + clang-cl/xwin).
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
