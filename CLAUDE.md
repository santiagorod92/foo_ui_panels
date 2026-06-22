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

## Layout
- `sdk/` — foobar2000 v2 SDK (razielanarki mirror): `foobar2000/`, `pfc/`, `libPPUI/`.
- `columns_ui_ref/` — Columns UI + `columns_ui-sdk`, **reference only (LGPL), do not copy**.

## Build (Arch Linux cross-compile)
- Prereqs: `clang-cl`, `lld-link`, `cmake`, `ninja`; `cargo install xwin && xwin --accept-license splat --output ~/.xwin`.
- Build: `./build.sh` → `build/foo_ui_panels.dll` (PE32+ x64, exports `foobar2000_get_interface`).
- Toolchain file: `cmake/clang-cl-win64.cmake`. Test the DLL in fb2k v2 (Wine, then real).

### Cross-build gotchas (all handled, don't re-discover)
- **Static release CRT only**: xwin ships no debug CRT → toolchain forces `/MT` (`CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded`, Release). Debug build → `msvcrtd.lib` not found.
- **Header case-sensitivity**: SDK `#include`s cased headers (`<SDKDDKVer.h>` etc); xwin lowercases. `build.sh` symlinks cased names into `~/.xwin`.
- **Include order in main.cpp**: `WIN32_LEAN_AND_MEAN` + `<winsock2.h>` before `<windows.h>` (WS1/WS2 clash), then `<objbase.h>` (COM `interface` macro) + `<mmsystem.h>` (`timeGetTime` for pfc/timers.h).
- **Link `shared-x64.lib`**: import lib for fb2k `shared.dll` (provides `uBugCheck`, `stricmp_utf8`, `uPrintfV`…). At `sdk/foobar2000/shared/`.
- libPPUI (rich list controls) deferred to Phase 2+ — needs WTL+ATL, not in xwin.
