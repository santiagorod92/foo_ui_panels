# foo_ui_panels — reborn for foobar2000 v2

Goal: reimplement the discontinued **Panels UI** (`foo_ui_panels`, last seen ~fb2k 0.9.5.2)
as a native component for **foobar2000 v2 (64-bit)**. Original was closed-source — this is a
ground-up reimplementation aiming for skin compatibility, not a recompile.

## Established facts (Phase 0)

- **No original source exists.** Closed-source; docs site `panels.fooblog2000.com` is dead.
  Skin-format spec must be recovered from archive.org + real old skin files.
- **SDK**: reupen/foobar2000-sdk-unmodified, kept outside the repo as siblings of the checkout
  (`../foobar2000/`, `../pfc/`; see CLAUDE.md "Build"). Columns UI
  (https://github.com/reupen/columns_ui, LGPL-3.0) consulted as reference only.
- **Target service**: full UI replacement via `user_interface` (SDK `foobar2000/SDK/ui.h`,
  versions v1–v4), registered with `FB2K_MAKE_SERVICE_INTERFACE_ENTRYPOINT`.
  Same slot `foo_ui_classic` / Columns UI occupy.
- **Engine shape** (from archived overview): nested layout = splitters → sub-layouts → panels;
  scripting panels; buttons; transparency; "setup panel"; mini-player mode. Title-format driven.

## Open blocker

- **Toolchain**: dev host is Arch Linux. fb2k components need MSVC + Windows SDK.
  Decision pending: Windows machine/VM vs Linux `clang-cl` cross-build.

## Roadmap

0. Foundation — SDK + spec recovery + toolchain  ← current
1. Skeleton DLL: loads in v2, registers `user_interface`, shows in Preferences
2. Layout engine: splitter/panel tree, resize, persist
3. Skin loader: parse original format → build tree (load one real old skin)
4. Built-in panels: playlist, album art, spectrum, buttons, text display
5. Title-format eval, fonts/colors, packaging
