# foo_ui_panels — reborn for foobar2000 v2

Goal: reimplement the discontinued **Panels UI** (`foo_ui_panels`, last seen ~fb2k 0.9.5.2)
as a native component for **foobar2000 v2 — Windows x64 + ARM64 (ARM64EC) and macOS (universal)**. Original was
closed-source — this is a ground-up reimplementation aiming for skin compatibility, not a recompile.

## Established facts

- **No original source exists.** Closed-source; docs site `panels.fooblog2000.com` is dead.
  The skin format was recovered from real skin files (fooAvA 1.05) and the original binaries —
  see `FORMAT.md`.
- **SDK**: reupen/foobar2000-sdk-unmodified, kept outside the repo as siblings of the checkout
  (`../foobar2000/`, `../pfc/`; see CLAUDE.md "Build"). Columns UI
  (https://github.com/reupen/columns_ui, LGPL-3.0) consulted as reference only.
- **Target service**: Windows — full UI replacement via `user_interface` (SDK
  `foobar2000/SDK/ui.h`), the slot `foo_ui_classic` / Columns UI occupy. macOS — the same
  `user_interface` module (Preferences › Display, owning its own window), plus a `ui_element_mac`
  layout element ("Panels UI") hosting the whole canvas inside the Default UI.
- **Engine shape**: not a splitter tree. A skin is a titleformat script re-evaluated on every
  repaint that places panels at absolute computed rects (`$panel`) and draws directly
  (`SkinEngine`, `src/core/skin_engine.cpp`).
- **Toolchain**: Windows DLL cross-compiled on Linux (clang-cl + lld-link + xwin); macOS bundle
  built natively in CI or cross on Linux (clang + ld64.lld). No MSVC/Xcode needed.

## Status

Done: skeleton DLL, scripted canvas engine, skin loader + per-skin `foo_ui_panels.ini`, native
panels (track display, seek bar, volume, grouped playlist, spectrum, peak meter, album art,
album browser / cover flow, playlist switcher, quick search, lyrics with karaoke and
tap-to-sync), tray icon, hot reload, skin diagnostics, zoom, mini mode, View › Panels UI menu,
macOS port (runtime-tested in a macOS VM), release pipeline.
User-facing detail: README "What's implemented".

## Open

- Accepted but not implemented: `$scplsetlayout` — it only appears in Single Column Playlist's
  own layout scripts, which the native playlist doesn't run.
- Panel types without a native view are hosted DUI elements by name, so on macOS they only work
  if a Mac element of that name exists.
- Only fooAvA has been tested; other Panels UI skins from the 0.9.x days are untested.
