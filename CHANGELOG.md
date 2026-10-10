# Changelog

All notable changes to foo_ui_panels (Panels UI, reborn). Versions are the `v*` git tags; the
GitHub release of each version has the downloadable `.fb2k-component`.

## [1.9.0](https://github.com/santiagorod92/foo_ui_panels/compare/v1.8.0...v1.9.0) (2026-10-10)

### Highlights
- **Windows on ARM**: Panels UI now runs natively on ARM64 Windows PCs (Snapdragon / Copilot+), not
  only under x64 emulation.
- **Component log and Diagnostics**: a log file, a new *Diagnostics* tab in Preferences and a
  one-click report to paste into a GitHub issue.
- **Issue forms** on GitHub for bug reports, feature requests and skin compatibility.

### New
- **Native ARM64 build (ARM64EC)** for Windows on ARM. The `.fb2k-component` is now multi-arch:
  `x64/` and `arm64ec/` DLLs plus the universal macOS bundle under `mac/`. foobar2000 v2 picks
  the right one by itself; nothing changes when installing.
- **Component log** `foo_ui_panels.log` in the foobar2000 profile folder
  (`%APPDATA%\foobar2000-v2\` / `~/Library/foobar2000-v2/`, or the portable `profile\`):
  - One line per session with the component and foobar2000 versions, OS, architecture and
    Wine version when running under Wine.
  - Warnings and errors always; with *Verbose logging* also the step-by-step detail (skin loads,
    panel creation, lyrics lookups, Navidrome album/tree loading, ratings, Preferences changes).
  - Rotates at 2 MB (the previous file is kept as `foo_ui_panels.log.1`).
  - Messages that also go to the foobar2000 console still appear there as `Panels UI: …`.
- **Preferences › Display › Panels UI (reborn) › Diagnostics** (Windows and macOS):
  - **Verbose logging (for bug reports)** checkbox.
  - **Copy Diagnostics**: copies a Markdown report ready to paste into an issue: component,
    foobar2000 and OS versions, build architecture, the active skin (folder, main script,
    `foo_ui_panels.ini`), player window / mini mode / wizard state, every panel with its
    kind, script problems, zoom and other settings, number of persistent variables, whether
    foo_navidrome is installed, the installed components and the recent log lines. Your home
    folder shows as `~` and your user name as `<user>`; variable values are never included,
    and nothing is sent anywhere.
  - **Open Log Folder** (Windows) / **Show Log in Finder** (macOS).
  - **Report an Issue…**: opens the bug report form on GitHub.
  - Shows the log file's location.
- **GitHub issue forms**: *Bug report* (walks through turning on verbose logging and pasting
  Copy Diagnostics), *Feature request* and *Skin compatibility* (for a legacy Panels UI skin
  that doesn't render or behave like it did).
- README: new *Reporting a problem* section.

### Changed
- Problems that used to be printed only to the foobar2000 console (script errors, missing skin
  files, hosted panels that can't be created) now also go to the component log.
- Release asset is labelled *multi-arch: Windows x64 + ARM64, macOS universal; foobar2000 v2*.

### For contributors
- Build for Windows on ARM with `WIN_ARCH=arm64ec ./build.sh` or `make build-arm64ec`
  (→ `build-arm64ec/`). xwin must now be splatted with `--arch x86_64,aarch64`. LLVM 22 is used
  in CI (installed from apt.llvm.org); ARM64EC needs a recent clang-cl.
- CI builds and uploads both Windows DLLs; the release job packages both.
- New SDK-free, unit-tested modules: `src/core/log` (logger: levels, rotation, ring buffer,
  console sink) and `src/core/diagnostics` (the report, home/user name redaction). Use
  `pui::log::info/warn/error/note` and `log::console(…)` instead of `console::print`.
- New platform services `ui::copy_to_clipboard`, `os_version`, `wine_version`, `home_dir`
  (Windows and macOS).
- Every script has a `usage()` help: `-h` / `--help` (`build.sh`, `deploy.sh`, `mac-build.sh`,
  `mac-vm-navidrome.sh`, `release-build.sh`, `ui-test.sh`, `version.sh`,
  `wizard-demo-library.sh`, `wizard-previews.sh`). `ui-test.sh` and `mac-vm-navidrome.sh` print it
  on a wrong command.
- Code style: the code carries no comments (sources, tests, scripts, tools, CMake, Makefile, CI);
  the reasons behind non-obvious code are in `CLAUDE.md` and the commit messages.
- Tests: 122 (new logger, diagnostics and Preferences model cases).

## [1.8.0](https://github.com/santiagorod92/foo_ui_panels/compare/v1.7.0...v1.8.0) (2026-10-09)

### Features
- **Screenshots in the layout wizard**: each built-in layout's card shows a real screenshot
  instead of a sketch of coloured boxes (the sketch stays as the fallback)
  ([#12](https://github.com/santiagorod92/foo_ui_panels/issues/12),
  [96e0cfc](https://github.com/santiagorod92/foo_ui_panels/commit/96e0cfc01993f96ecc4cb0fe077b1f804b3d34a1)).
  - Images under `res/` are built into the component; scripts draw them as `builtin:<path>`.
  - The previews are taken from a made-up demo library (`scripts/wizard-previews.sh`); no real
    music or cover art is in them.

### Bug Fixes
- Lyric Show finds `.lrc`/`.txt` sidecars of tracks with `file-relative://` paths (portable
  installs).

### Maintenance
- Build warnings and CI deprecation notices silenced; semantic-release 25
  ([#13](https://github.com/santiagorod92/foo_ui_panels/issues/13)).

## [1.7.0](https://github.com/santiagorod92/foo_ui_panels/compare/v1.6.0...v1.7.0) (2026-10-08)

### Features
- **Layout wizard** with five built-in layouts (*Playlist and cover*, *Library*, *Album browser*,
  *Playlist*, *Now playing*), like the Default UI's *Quick Setup*. Picking one writes it out as a
  normal skin folder under `foo_ui_panels-skins` in the profile and makes it the skin; *Use my own
  skin folder…* loads any other skin. Opens from *View › Panels UI › Layout wizard…*, the welcome
  screen and Preferences
  ([#11](https://github.com/santiagorod92/foo_ui_panels/issues/11),
  [151a1ee](https://github.com/santiagorod92/foo_ui_panels/commit/151a1ee8f253ced625df441ba7f4f9220451cc5f)).
- **First run**: with nothing set up, Panels UI applies the default layout and opens the wizard.

### Bug Fixes
- **Dark Mode on the Windows Preferences page**: follows foobar2000's Dark Mode, live; with
  Panels UI as the UI module, *Auto* follows Windows' theme changes.
- Preferences skin preview no longer cuts off the folder note.
- Choosing a skin folder never gets stuck: the folder is always saved, a warning explains when it
  has no main script (or several), and the skin starts as soon as one appears.
- Welcome screen is generic (no skin-specific wording).

### Code Refactoring
- Testable panel logic, shared host input and golden render tests
  ([#7](https://github.com/santiagorod92/foo_ui_panels/issues/7),
  [2177a78](https://github.com/santiagorod92/foo_ui_panels/commit/2177a78139540d7ecf45b85a73adc68bb4b1d3e5)).
  macOS now offers *(the component's own folder)* as a skin, and both Preferences pages show
  where the single skin is looked for.

### Maintenance
- The component version comes from the git tags; dev builds show e.g.
  `1.7.0-dev.2+ef96f46` in *Preferences › Components*
  ([#9](https://github.com/santiagorod92/foo_ui_panels/issues/9)).
- Author credit in Preferences; contributor docs and Windows 11 VM targets
  ([#10](https://github.com/santiagorod92/foo_ui_panels/issues/10)).

## [1.6.0](https://github.com/santiagorod92/foo_ui_panels/compare/v1.5.0...v1.6.0) (2026-10-06)

### New
- **Welcome screen** when no skin is set up: shows where it looked and offers *Choose a skin
  folder…* and *Preferences…*.
- **Skin previews in Preferences** (`preview` in `foo_ui_panels.ini`, a `preview.png` /
  `screenshot.png`, or one taken the first time the skin is shown).
- **Script problems on the skin**: a red **!** on a panel whose script is missing or doesn't
  compile; hover for details, click to edit. *View › Panels UI › Show script problems* turns it off.
- **Better code editor**: syntax colouring, cursor position, a quick check in a status line,
  **Revert**, Ctrl+S / ⌘S to apply.
- **Cover menu**: right-click the now-playing cover to view it full size, open the file, show it in
  its folder or search for it online.
- **Views remembered**: playlist scroll positions and the album last picked come back after a
  restart.
- **Keyboard & screen readers**: Tab / Shift+Tab between panels with a focus outline; panels have
  accessible names.
- macOS Preferences has the same four tabs as Windows.
- New skin actions `PREFERENCES`, `URL:<address>`, `SKIN:CHOOSE_FOLDER`; `$drawstring` takes `wrap`.

### Faster
- ~6× faster skin rendering (pre-scaled big images, faster same-size draws).

### Fixed
- `$getpvar(...)` inside `$eval(...)` is substituted.
- `$gp_draw_rectangle` with a fully transparent pen draws nothing instead of an error marker.

### For skin authors and contributors
- `skin_lint` (`make skin-lint LINT_DIR=<skin folder>`) checks a skin without foobar2000.
- [docs/SCRIPT_FUNCTIONS.md](https://github.com/santiagorod92/foo_ui_panels/blob/main/docs/SCRIPT_FUNCTIONS.md)
  lists every script function, generated from the engine.

Full change: [#6](https://github.com/santiagorod92/foo_ui_panels/pull/6).

## [1.5.0](https://github.com/santiagorod92/foo_ui_panels/compare/v1.4.0...v1.5.0) (2026-10-06)

### Features
- **lyrics:** karaoke highlight and tap-to-sync
  ([#5](https://github.com/santiagorod92/foo_ui_panels/issues/5),
  [150bb42](https://github.com/santiagorod92/foo_ui_panels/commit/150bb42c6e5342d1aebacacadb77f901a9395090))

## [1.4.0](https://github.com/santiagorod92/foo_ui_panels/compare/v1.3.0...v1.4.0) (2026-10-02)

### Features
- **album-art:** cross-fade to the new cover on a track change
  ([5a0f448](https://github.com/santiagorod92/foo_ui_panels/commit/5a0f4489a6351c4fd7e2f91df9a467a4286ca6a2))
- **album-browser:** type to search, sort, keyboard navigation
  ([4f99430](https://github.com/santiagorod92/foo_ui_panels/commit/4f99430afb59d33d7b80f22d5e89ef240d1ff4a1))
- zoom, View > Panels UI menu, mini mode and skin commands
  ([3765c66](https://github.com/santiagorod92/foo_ui_panels/commit/3765c669d3367f7731be9091fc5e09fc77e9a2a5))

## [1.3.0](https://github.com/santiagorod92/foo_ui_panels/compare/v1.2.0...v1.3.0) (2026-10-01)


### Features

* native peak meter and album art, tray icon, playlist keyboard and drag & drop, macOS preferences and Retina ([976811c](https://github.com/santiagorod92/foo_ui_panels/commit/976811cb55ed82239591fe1c7277a9b8a34709de))


### Documentation

* refresh DESIGN.md and FORMAT.md to the current engine ([ff1cabe](https://github.com/santiagorod92/foo_ui_panels/commit/ff1cabe8b775c02bea48854b152773574f89f8cd))

## [1.2.0](https://github.com/santiagorod92/foo_ui_panels/compare/v1.1.0...v1.2.0) (2026-10-01)


### Features

* skin-agnostic engine, hot reload, diagnostics, tooltips and performance work ([3b63f1e](https://github.com/santiagorod92/foo_ui_panels/commit/3b63f1ee972f57bf03415d84ec9723a871249a6d))

## [1.1.0](https://github.com/santiagorod92/foo_ui_panels/compare/v1.0.0...v1.1.0) (2026-09-30)


### Features

* macOS support and platform-free core ([#2](https://github.com/santiagorod92/foo_ui_panels/issues/2)) ([607e1ca](https://github.com/santiagorod92/foo_ui_panels/commit/607e1ca9525d4de9f7e0ace984b28ecb4b72afdf))

## 1.0.0 (2026-09-29)


### Features

* first public release ([9636221](https://github.com/santiagorod92/foo_ui_panels/commit/96362219a083d854f8e2c0b14ce2de6baa0f15f2))


### Documentation

* extractor emits per-panel scripts ([71b69e2](https://github.com/santiagorod92/foo_ui_panels/commit/71b69e264f42322fed1a770f2e6cc9fb3665fab7))
* main menu integration pattern and WM_INITMENUPOPUP TODO ([cb29b0f](https://github.com/santiagorod92/foo_ui_panels/commit/cb29b0fd1638f315144f2bb2bcf1aebed2f47884))
* PanelHost and win_sdk.h conventions ([b241e75](https://github.com/santiagorod92/foo_ui_panels/commit/b241e75ad41c55a642588ce038ab962696d8bf97))
* point to tools/extract_fooava.py ([d3ad818](https://github.com/santiagorod92/foo_ui_panels/commit/d3ad818d9b091809b1668bddec386bd901910a87))
* record local-iteration / push-at-end convention ([e842bdc](https://github.com/santiagorod92/foo_ui_panels/commit/e842bdc0b679a4c557f4e40268d86c2682a4ab79))
* record repo conventions in CLAUDE.md ([b1b0757](https://github.com/santiagorod92/foo_ui_panels/commit/b1b07576b7c150b4ce9df2e9a41a0b0b8481da6a))
* SkinEngine interpreter architecture ([2b4f41a](https://github.com/santiagorod92/foo_ui_panels/commit/2b4f41ab4c55479e84cf67d857ced0a044480518))
* splitter engine and raw-Win32 layout decision ([e333b69](https://github.com/santiagorod92/foo_ui_panels/commit/e333b69216bb8687d870e39231166d1ea0a6e714))
* wallpaper asset setup + backgroundd=1 to enable background ([3db0fa3](https://github.com/santiagorod92/foo_ui_panels/commit/3db0fa340ab36c0aa4c59e2ff0589b735ff07ae1))
* Wine deploy/test path and keyboard-forwarding note ([db77311](https://github.com/santiagorod92/foo_ui_panels/commit/db773113ee924cc58c6fe1a16bc146df8a172ca5))
