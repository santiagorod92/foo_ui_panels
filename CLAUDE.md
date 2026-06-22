# foo_ui_panels — reborn (foobar2000 v2)

Ground-up reimplementation of the discontinued **Panels UI** (`foo_ui_panels`, ~fb2k 0.9.5.2)
for **foobar2000 v2 (64-bit)**. Original closed-source — aiming for skin compatibility, not recompile.
See `DESIGN.md` for full roadmap/facts.

## Key decisions
- Target service: full UI replacement via `user_interface` (`sdk/foobar2000/SDK/ui.h`, v1–v4),
  `FB2K_MAKE_SERVICE_INTERFACE_ENTRYPOINT`. Same slot as foo_ui_classic / Columns UI.
- Build: **cross-compile on Linux** (clang-cl + lld-link + xwin Windows SDK/CRT). No MSVC.
- Reference skin / format spec source: **fooAvA 1.05** (dawxxx666). Its internal config XML = the
  Panels-UI format to reverse-engineer. Full fooAvA also needs `foo_chronflow` (CoverFlow, also dead).

## Repo conventions
- Remote: `origin` → github.com/santiagorod92/foo_ui_panels (private). Branch `main`.
- **Commits: author Santiago Rodriguez <santiagom9992@gmail.com> ONLY — no Co-Authored-By trailer.**
- Never commit: `sdk/`, `columns_ui_ref/` (fetched by build.sh), `.xwin-cache/` (MS binaries),
  `skins/` (proprietary DLLs + CC-BY-NC-SA art), `build/`. All gitignored — keep local.

## Code
- `src/win_sdk.h` — canonical SDK include block (right order for clang-cl). Include this, not raw windows.h.
- `src/main.cpp` — `user_interface` impl: main window, menu, keyboard, hosts root splitter.
- `src/splitter.{h,cpp}` — `pui::Splitter`: layout engine in **raw Win32, no ATL/WTL** (deliberate —
  ATL/WTL not in xwin; only libPPUI rich controls need them, deferred). Splitters nest via child HWNDs.
- `src/panel_host.{h,cpp}` — `pui::PanelHost`: hosts a DUI `ui_element` by name inside a pane
  (derives `ui_element_instance_callback_receiver`, `instantiate()` → child HWND).
- `src/skin_engine.{h,cpp}` — `pui::SkinEngine`: the interpreter. Reuses fb2k `titleformat_compiler`
  (built-in `$if/$sub/$add/$puts/…` for free); a `titleformat_hook` supplies `%_width%/%_height%`,
  `$panel` (records rects), `$eval` (integer arithmetic, strips `{}`), `$getpvar/$setpvar`; draw
  funcs stubbed. `layout(w,h)` re-evaluates on resize → creates/positions hosted panels.
  Legacy type→DUI mapping in `map_type()` (see FORMAT.md). **This is the core architecture** —
  Panels UI is a scripted absolute-positioning canvas, not a splitter tree (splitter.cpp now unused).

## Layout
- `sdk/` — foobar2000 v2 SDK (razielanarki mirror): `foobar2000/`, `pfc/`, `libPPUI/`.
- `columns_ui_ref/` — Columns UI + `columns_ui-sdk`, **reference only (LGPL), do not copy**.

## Build (Arch Linux cross-compile)
- Prereqs: `clang-cl`, `lld-link`, `cmake`, `ninja`; `cargo install xwin && xwin --accept-license splat --output ~/.xwin`.
- Build: `./build.sh` → `build/foo_ui_panels.dll` (PE32+ x64, exports `foobar2000_get_interface`).
- Toolchain file: `cmake/clang-cl-win64.cmake`. Test the DLL in fb2k v2 (Wine, then real).

### Deploy/test (Wine)
- Install: foobar2000 v2.25.9 at `~/.foobar2000/`.
- Deploy DLL to `~/.foobar2000/profile/user-components-x64/foo_ui_panels/foo_ui_panels.dll`
  (v2 user-component layout: one subfolder per component). Fully restart fb2k to reload.
- A full UI replacement must forward `WM_KEYDOWN`/`WM_SYSKEYDOWN` to
  `keyboard_shortcut_manager::on_keydown_auto(wp)` or shortcuts (Ctrl+P…) won't work.
- Main menu: one `mainmenu_manager` per root group (`mainmenu_groups::file` etc.),
  `generate_menu_win32` into partitioned WM_COMMAND id ranges (`kSpan`-wide), routed back via
  `execute_command(id-base)`. TODO: rebuild on `WM_INITMENUPOPUP` for dynamic check/enable state.

### Cross-build gotchas (all handled, don't re-discover)
- **Static release CRT only**: xwin ships no debug CRT → toolchain forces `/MT` (`CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded`, Release). Debug build → `msvcrtd.lib` not found.
- **Header case-sensitivity**: SDK `#include`s cased headers (`<SDKDDKVer.h>` etc); xwin lowercases. `build.sh` symlinks cased names into `~/.xwin`.
- **Include order in main.cpp**: `WIN32_LEAN_AND_MEAN` + `<winsock2.h>` before `<windows.h>` (WS1/WS2 clash), then `<objbase.h>` (COM `interface` macro) + `<mmsystem.h>` (`timeGetTime` for pfc/timers.h).
- **Link `shared-x64.lib`**: import lib for fb2k `shared.dll` (provides `uBugCheck`, `stricmp_utf8`, `uPrintfV`…). At `sdk/foobar2000/shared/`.
- libPPUI (rich list controls) deferred to Phase 2+ — needs WTL+ATL, not in xwin.
