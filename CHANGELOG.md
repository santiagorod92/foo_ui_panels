# Changelog

All notable changes to foo_ui_panels (Panels UI, reborn). Versions are the `v*` git tags; the
GitHub release of each version has the downloadable `.fb2k-component`.

## [1.13.4](https://github.com/santiagorod92/foo_ui_panels/compare/v1.13.3...v1.13.4) (2026-10-19)

### Highlights
- **Dark Mode now covers the whole main window on Windows**: title bar, menu bar and menus.

### Fixed
- **Windows: the main window stayed light in Dark Mode**: only the Preferences page followed
  foobar2000's Dark Mode, so the title bar, the menu bar (File, Edit, View…) and its menus, and
  the right-click menus stayed light. They now follow it too, including the switch when Windows'
  app theme changes while foobar2000 is running (Dark Mode on *Auto*). Reported in
  [#18](https://github.com/santiagorod92/foo_ui_panels/issues/18).

### For contributors
- **Every Windows build runs in the Windows 11 ARM VM**: ARM64EC natively, x64 in the same ARM
  foobar2000, x86 in a portable 32-bit foobar2000 at `C:\fb2k-x86`
  (`make win11-arm-provision ARCH=x86` once, then `make win11-arm-deploy ARCH=x86`).

## [1.13.3](https://github.com/santiagorod92/foo_ui_panels/compare/v1.13.2...v1.13.3) (2026-10-18)

### Fixed
- **Album browser: Recently added didn't sort Navidrome albums**: they had no date and stayed in
  artist order. With foo_navidrome 1.31 or later they now sort by the date they were added to the
  server, mixed with local albums. Reported in
  [#21](https://github.com/santiagorod92/foo_ui_panels/issues/21).

## [1.13.2](https://github.com/santiagorod92/foo_ui_panels/compare/v1.13.1...v1.13.2) (2026-10-18)

### Highlights
- **Smooth scrolling in the album browser with large cover art**.

### Fixed
- **Album browser / cover flow: scrolling lagged with high-resolution covers**: every cover that
  scrolled back into view was decoded again at full size, which stalled the window while it
  scrolled. The album browser, cover flow and the playlist's group headers now load covers in the
  background and keep a small copy, so scrolling stays smooth and a cover is only read once
  (the Album Art panel and the full-size viewer still use the original). Reported in
  [#18](https://github.com/santiagorod92/foo_ui_panels/issues/18).

## [1.13.1](https://github.com/santiagorod92/foo_ui_panels/compare/v1.13.0...v1.13.1) (2026-10-18)

### Highlights
- **The album browser no longer keeps reloading its covers**: with large cover art foobar2000
  stayed busy after start-up and clicks and keys lagged. Fixed.

### Fixed
- **Album browser / cover flow with high-resolution covers** (embedded art or `cover.jpg` of
  ~2000 px and up): the covers on screen kept disappearing and coming back, one CPU core stayed
  busy and the rest of the window (playlist, buttons) reacted late or not at all, as if a folder
  scan never ended. Covers that are on screen now stay loaded, so the grid loads once and
  foobar2000 goes idle. Reported in
  [#18](https://github.com/santiagorod92/foo_ui_panels/issues/18).

## [1.13.0](https://github.com/santiagorod92/foo_ui_panels/compare/v1.12.0...v1.13.0) (2026-10-18)

### Highlights
- **Panels UI works on Windows on ARM again**: the native ARM64 build never loaded on ARM PCs
  (Snapdragon / Copilot+), so foobar2000 fell back to its Default UI. Fixed.
- **Sort the album browser by decade**.

### New
- **Album browser / cover flow: *Sort by › Decade (newest first)***: albums grouped by decade,
  the newest first, and by artist inside each decade (albums without a year go last). It works
  for Media Library and Navidrome albums alike, and is remembered like the other sort orders
  (right-click empty space in the grid). Asked for in
  [foo_navidrome#37](https://github.com/santiagorod92/foo_navidrome/issues/37).

### Fixed
- **Windows on ARM**: foobar2000 for ARM showed *Failed to load component ... A dynamic link
  library (DLL) initialization routine failed* for `foo_ui_panels.dll` and started the Default UI
  instead, since the ARM64 build was introduced in 1.9.0. The ARM64 build now uses the C runtime
  that foobar2000 for ARM ships with, and loads. Nothing to do but update; on ARM PCs pick
  **Panels UI (reborn)** again under *Preferences › Display* if foobar2000 switched away from it.

## [1.12.0](https://github.com/santiagorod92/foo_ui_panels/compare/v1.11.1...v1.12.0) (2026-10-15)

### Highlights
- **Lyrics from your Navidrome server**: with [foo_navidrome](https://github.com/santiagorod92/foo_navidrome)
  installed, the lyrics panel shows a Navidrome track's lyrics straight from the server, synced
  ones scrolling and highlighting the current line.
- **fooAvA looks right again**: no column header over its playlist, the lyrics view inside the
  CD case, no stray copy of the case behind it, and a working settings window.

### New
- **Navidrome lyrics in the lyrics panel**: for a `navidrome://` track the panel asks
  foo_navidrome for the lyrics as soon as the track plays — no need to turn on online search, it
  is your own server. Synced lyrics scroll, highlight the current line and work with the karaoke
  highlight, timing offsets and *Sync lyrics by tapping*; they are kept in the lyrics cache like
  lrclib.net results. When the server has none, lrclib.net is tried if online search is on, or
  from the panel's *Search online* link. While looking them up the panel says *Searching
  Navidrome...*. Works on Windows and macOS; without foo_navidrome nothing changes.

### Changed
- **Playlist column header is now off by default**: the sortable Album / Title / Rating / Length
  header added in 1.11.0 showed up on every skin, on top of designs that never had it (fooAvA's
  playlist). Skins turn it on with `playlist.header = 1` in their `foo_ui_panels.ini`; layouts
  made by the layout wizard do. A layout made with the wizard before this version needs that
  line added (or the layout made again) to keep the header.
- **fooAvA's lyrics view inside the CD case** (`tools/setup_fooava.py`): the left column's **L**
  view now shows the CD case like the main view, with the lyrics over the cover inside it,
  instead of lyrics across the whole column. Run the setup again to get it in an existing
  fooAvA folder.

### Fixed
- **A copy of a button's image at the panel's top-left corner**: a skin button whose normal or
  hover image is a sized `$imageabs2(...)` (fooAvA's CD case is one) also drew that image at the
  panel's corner, which looked like a shadow of the CD case shifted up and to the left. Button
  images are now only drawn on the button. Fixed for every skin, on Windows and macOS.
- **fooAvA settings window** (`tools/setup_fooava.py`): its background was drawn over the
  options, washing them out, and *CHOOSE FONT* was cut off at the right edge. Run the setup
  again to get the fixed window.

### For contributors
- **`navidrome_lyrics_api`**: vendored GUID-matched contract (`src/core/navidrome_lyrics_api.h`),
  found with `service_enum_t` and called from a worker thread; the lines become LRC through the
  new SDK-free `lyrics_text()` (unit-tested).
- **`defer_button_images()`** (`script_util`, unit-tested): foobar2000 evaluates a hook
  function's arguments before calling it, so every skin-script compile now quotes the
  `$imageabs2` state images of `$button`/`$button2` into literal text. The synthetic skin has a
  button with sized state images and its golden output checks they land on the button.

## [1.11.1](https://github.com/santiagorod92/foo_ui_panels/compare/v1.11.0...v1.11.1) (2026-10-15)

### Fixed
- **Verbose log missed the start of every session**: with *Verbose logging* on, the INFO lines
  written before the log file opened (the main window being created, among others) were
  dropped. They are now kept until the file opens and written if verbose logging is on, so a
  log attached to an issue shows the whole startup.
- **macOS log**: the creation of the main window is logged on macOS too, like on Windows.
- **Playback in the log**: each new track now writes an INFO line (`playback  new track`, no
  file path), so a verbose log shows when playback started.

### For contributors
- **One UI test script for every platform**: `scripts/ui-test.sh <wine|win11|mac> <scenario>`
  replaces the Wine-only driver and the screenshot-only VM targets. The `smoke` scenario turns
  verbose logging on, deploys, relaunches and checks the session banner, the skin load, the
  main window and playback, then that there's no crash report or ERROR line, and saves a
  screenshot. `make ui-smoke`, `make win11-smoke`, `make mac-vm-smoke`.
- **Compare platforms**: `make ui-compare ARGS='smoke'` runs a scenario on Wine, the Windows 11
  VM and the macOS VM (`PARALLEL=1` at once) and prints a step matrix marking where they
  differ. Wine's `click`, `shot`, `pvars` and `restart` helpers are kept (`make ui ARGS=…`).

## [1.11.0](https://github.com/santiagorod92/foo_ui_panels/compare/v1.10.0...v1.11.0) (2026-10-12)

### Highlights
- **Seek bar, volume and skin buttons that feel like a modern player**: the time under the
  pointer, drag to scrub, the mouse wheel, the keyboard, and hover/press feedback on buttons.
- **Sortable playlist**: a column header to sort by album, title, rating or length.
- **fooAvA in one step**: `tools/setup_fooava.py` turns the original installer into a ready
  skin folder (in your Documents by default), and a skin there now loads cleanly from scratch.
- **Resizing the window under Wine** by dragging its edge now works, also on compositors such
  as Hyprland.

### New
- **Playlist column header** (Album, Title, Rating, Length): click a column to sort the
  playlist by it, click again to reverse; an arrow shows the column and direction. Rating
  starts with the best rated. Right-click the header for the same choices. The order before
  sorting is kept for foobar2000's undo. Sorting by anything but the album splits the album
  groups, as in any grouped view. Skins can hide it with `playlist.header = 0`.
- **Seek bar**:
  - Hover to see the time under the pointer (`1:23 / 4:56`) and a marker line.
  - Drag to scrub; the track seeks when you release (a plain click still seeks there).
  - Mouse wheel: 5 seconds per notch.
  - Keyboard, once focused with Tab: ←/→ seek 5 seconds (Shift: 30), Home restarts the track.
- **Volume**:
  - Mouse wheel and arrow keys change it in 2.5 dB steps, matching the bar.
  - Home mutes, End sets full volume, double-click toggles mute.
  - Hover shows the level (`-6.0 dB`, `Muted`).
- **Skin buttons**: a hand pointer over them; buttons with no hover art of their own light up
  under the pointer and darken while pressed. `button.feedback = 0` in the skin's
  `foo_ui_panels.ini` turns the highlight off.
- **`tools/setup_fooava.py`** (Python 3, any OS): unpacks the fooAvA 1.05 installer (without
  running it), copies its images, converts the layout and panel scripts, writes fooAvA's
  `foo_ui_panels.ini` and puts each popup window (settings, playlist, about) where the skin's
  buttons look for it. Output goes to `Documents/fooava` unless another folder is given; it
  never overwrites an existing skin. An already unpacked installer folder works too.
- New skin `foo_ui_panels.ini` keys: `playlist.header`, `button.feedback`.

### Changed
- **Choosing a skin folder on Windows** uses the standard folder dialog, with an address bar,
  Documents and other places, and a box to paste a path (it was a small folder tree that
  started at the drive root). It no longer starts inside the profile folder the layout wizard
  writes to.
- **Preferences › Browse…** (Windows and macOS) accepts either a skin's own folder or a folder
  with one skin per subfolder, like the welcome screen and the wizard already did.
- The **Active skin** list only shows subfolders that are skins (have a main script or a
  `foo_ui_panels.ini`), so a Documents folder doesn't list unrelated folders.
- The seek bar and the volume are now **Tab stops**, between the other panels that take keys.
- The **release asset** is labelled `foo_ui_panels <version> (macOS + Windows x86/x64/ARM64)`,
  the same format as foo_navidrome.

### Fixed
- Picking a skin's own folder with **Preferences › Browse…** made it the skins root: the list
  showed its `images` and `panels` subfolders and the skin didn't load.
- **fooAvA conversion**:
  - The `ChronTIM` panel script (cover-flow track info) was lost, leaving a red "!" marker; it
    is recovered.
  - Eleven stray `panels/*.txt` files with nonsense names (some invalid on Windows, such as
    `*Graphical Browser*.txt`) are no longer written; only scripts the skin uses are.
- **Wine**: dragging the window's edge didn't resize it on compositors that ignore Wine's
  resize request (Hyprland, for one). Panels UI now resizes the window itself under Wine.
- macOS: the README explains the one-time *"foobar2000 would like to access files in your
  Documents folder"* prompt; answering *Don't Allow* keeps a skin there from loading.

### For contributors
- foobar2000 SDK bumped to **SDK-2026-10-01** (pfc's ARM64EC `/alternatename` hooks fixed;
  `now_playing_album_art_notify` may pass null data, already handled).
- **Faster CI**: changes outside the component (docs, issue forms, local dev scripts) don't
  trigger builds; superseded PR runs are cancelled; the Windows architectures build in parallel
  with per-arch ccache; tests run in their own job without xwin. On a releasing push to main a
  dry-run stamps every build with the next version, and the Release workflow reuses those
  artifacts instead of rebuilding.
- `CLAUDE.md` gains a *Shared conventions* section kept identical with foo_navidrome (SDK, build,
  versioning, release asset naming, logging, test environments, troubleshooting).
- New SDK-free, unit-tested helpers: `list_logic` (playlist header columns, sort state, stable
  sort order), `ui_logic` (time/volume labels, seek and volume steps, fractional wheel
  accumulation, folder-picker start folder, edge-resize geometry), `PrefsModel` skin-list
  filtering, the `feedback` flag on script buttons.
- `src/platform/win/folder_picker.h`: the shared Windows folder picker (`IFileOpenDialog` with a
  `SHBrowseForFolder` fallback), used by the welcome screen, the wizard and Preferences.
- `tools/extract_fooava.py` only cuts per-panel records at names some `$panel()` uses, and
  reports how many records it skipped.

## [1.10.0](https://github.com/santiagorod92/foo_ui_panels/compare/v1.9.0...v1.10.0) (2026-10-11)

### Highlights
- **32-bit foobar2000**: Panels UI now also runs in the 32-bit (x86) build of foobar2000 v2.

### New
- A **32-bit Windows (x86) DLL** in the release component, at its root, where 32-bit
  foobar2000 v2 looks for it. Installing is unchanged: the same `.fb2k-component` works in
  32-bit, 64-bit and ARM64 foobar2000 on Windows, and on macOS.

### Changed
- The README no longer says foobar2000 v2 is 64-bit only.

### For contributors
- `WIN_ARCH=x86 ./build.sh` / `make build-x86` → `build-x86/foo_ui_panels.dll`, linked against
  `shared-Win32.lib`. xwin must be splatted with `--arch x86,x86_64,aarch64`.
- CI builds and uploads the x86 DLL; the release job packages it.

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
