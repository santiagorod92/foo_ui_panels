# foo_ui_panels — reborn (foobar2000 v2)

Ground-up reimplementation of discontinued **Panels UI** (`foo_ui_panels`, ~fb2k 0.9.5.2)
for **foobar2000 v2 — Windows x64 + ARM64EC + x86, macOS (universal)**. Original closed-source — skin compatibility goal, not recompile.
See `DESIGN.md` for roadmap/facts, `FORMAT.md` for legacy type→DUI mapping.

## Working rule: every change is cross-platform
Port started 2026-09-29 (foo_navidrome's rule, its CLAUDE.md "Working rule"): every feature/fix
lands on BOTH platforms, all builds + CI legs green, release zip = `mac/` bundle + `x64/` +
`arm64ec/` DLLs + the x86 DLL at the zip root (fb2k's 32-bit slot).
- Logic (script engine, panels, models, actions) is platform-free C++ in `src/core` /
  `src/panels`, drawing via `gfx::Canvas` and windowing via `ui::View`/`ui::ViewHost`/
  `ui::MainWindow` + the `ui::` service functions. No `HWND`/`HDC`/Cocoa there — only
  `#include "../fb2k.h"`, never `win_sdk.h`. `make check-portable` (also in CI) enforces it.
- A new platform need = a new method/function in `src/gfx/canvas.h` or `src/ui/view.h`,
  implemented in BOTH `src/platform/win/` and `src/platform/mac/`.
- **Every change is verified on both platforms before it ships** — logic, panel or platform
  code alike: (1) `make test` + `make check-portable` (+ `make skin-lint
  LINT_DIR=tests/skins/synthetic`), with new pure logic covered by a unit/golden test;
  (2) both builds; (3) runtime on Windows in the local Wine foobar2000 (`make deploy`,
  `scripts/ui-test.sh` to click/type/screenshot); (4) runtime on macOS in the VM
  (`make mac-vm` / `make mac-vm-test`, `mvm click|key|shot`); real Windows 11 (`make win11-test`)
  when the change touches theming, DPI or native controls. Exercise the changed feature
  itself, not just "it loads". Anything that couldn't be checked (and why) goes in the PR.
- macOS integration: a `user_interface` module ("Panels UI (reborn)", Preferences › Display —
  verified in the VM) owning its window, plus a `ui_element_mac` layout element ("Panels UI").
- macOS runtime testing: the local VM in sibling repo `../macos-devbox` (`mvm`, dockur/macos,
  Intel guest — bundle must keep its x86_64 slice). `make mac-vm-test` = mac-build → deploy →
  relaunch → screenshot; `make mac-vm` boots it and opens the noVNC tab only once macOS is up
  (input during OpenCore's picker cancels its auto-boot timeout; `VNC=0` = no tab); `make mac-vm-skin`, `make mac-vm-navidrome[-config]` (foo_navidrome + its Wine settings), `make mac-vm-<mvm cmd>`. Never `docker prune`/`rm`.
- Real Windows 11 runtime testing (Dark Mode, DPI, native theming — what Wine fakes): sibling
  `../windows-devbox` (`wvm`, dockur/windows). `make win11-test` = build → `wvm deploy` → copy
  skins/$SKIN into the component folder (Panels UI's default skin dir; `wvm deploy` recreates
  that folder, so the skin is re-copied every time) → relaunch → screenshot. `win11-skin` drops
  names Windows can't hold (stray `*`/`\` files a skin's scripts wrote under Wine) — scp aborts
  the whole copy on one. The guest needs Panels UI picked as UI module once (then
  `make win11-snapshot`). `make win11-<wvm cmd>`: `-theme ARGS=dark`, `-dpi ARGS=144`, `-shot`.

## Shared conventions (foo_ui_panels ⇄ foo_navidrome)

foo_ui_panels (Panels UI) and foo_navidrome are sister components, sibling checkouts under one
parent (`../foo_ui_panels`, `../foo_navidrome`). They're developed together, installed together
and tested in the same foobar2000. **This section is identical in both repos' CLAUDE.md** and
holds only what's true for both; anything repo-specific stays in the repo's own sections.
- **Editing it:** change both copies in the same round, keep the subsection order below, one
  terse bullet per fact (`**Topic:** fact`), no narrative or session logs. Check they match:
  `diff <(awk '/^## Shared conventions/{f=1} /^## /&&!/^## Shared/{f=0} f' ../foo_ui_panels/CLAUDE.md) <(awk '/^## Shared conventions/{f=1} /^## /&&!/^## Shared/{f=0} f' ../foo_navidrome/CLAUDE.md)`.
- **What goes here:** a rule, tool, layout or gotcha that's useful in both repos (SDK, build
  layout, versioning, logging, release, test environments, troubleshooting). If it's useful in
  only one repo, it goes in that repo's own sections.

### How they work together
- **Contracts:** foo_navidrome publishes `service_base` interfaces, and foo_ui_panels vendors a
  GUID-matched copy of the contract header and finds it with `service_enum_t` at runtime. There's
  no build coupling, and the feature is a no-op when the other component is missing. foo_navidrome's
  "Cross-compatibility with other same-author components" section is the full list: each
  interface, its consumers and its vendored copies.
- **Current links:** `navidrome_rating_api` drives Panels UI's star ratings on `navidrome://`
  tracks, which also read the `NAVIDROME_RATING` field. `navidrome_library_api` feeds the
  Panels UI album grid (`album_list`) and `library_tree`. `navidrome_lyrics_api` feeds Panels
  UI's `lyrics_panel`.
- **Changing a contract:** a change to a contract header (GUID, vtable order, sink signatures)
  lands in both repos in the same round, and the vendored copy stays byte-identical to the
  original. A new interface also gets a bullet in foo_navidrome's cross-compatibility list.
- **Runtime check:** test a feature that crosses the boundary with both DLLs installed (Wine,
  macOS VM). Also check that each side still works with the other component missing.

### SDK
- **Source:** `reupen/foobar2000-sdk-unmodified`, tags `SDK-YYYY-MM-DD`. Don't use the
  `marc2k3` or `razielanarki` mirrors: they're stale and missing `helpers-mac/`.
- **Location:** one checkout, shared by both repos: `../foobar2000/{SDK,helpers,helpers-mac,shared,
  foobar2000_component_client}` + `../pfc`. A bump therefore affects both repos at once. CI
  re-clones upstream's default branch on every run.
- **In use: SDK-2026-10-01.** foobar2000 v2.26; C++20 is required (since 2026-09-16).
- **Bump procedure:**
  1. Clone the tag into a scratch dir.
  2. Run `diff -rq` against the sibling SDK folders.
  3. Read foobar2000.org/changelog-sdk.
  4. `git grep` both repos for every symbol the diff touches.
  5. Copy the changed files into the siblings.
  6. Build every leg of both repos and run both test suites.
  7. Update the version in "In use" above (both copies) and in each README's development table.
- **Sync history:**
  - 2026-09-16: C++20 is required. pfc now redirects to `shared.dll` through
    `wrap_pfc_hooks.h`, and the ARM64EC link needed the hooks. `skipTrack.h` was added.
  - 2026-10-01: pfc's ARM64EC `/alternatename` is fixed (`#` names). `now_playing_album_art_notify`
    may now pass null data.
- **Upstream breakage:** a sync can break a build with no change on our side (a renamed
  target, a new required integration step). Diff the live upstream files and read the SDK
  changelog before debugging our code.

### Build, version, release
- **Toolchain:** local Windows builds are done on Linux with clang-cl + lld-link + xwin (splat
  `--arch x86,x86_64,aarch64`), static `/MT` CRT, x64 + x86. Release builds also include ARM64EC:
  foo_ui_panels builds it with clang-cl, foo_navidrome with MSBuild in CI. macOS ships as a
  universal bundle, and the x86_64 slice must stay (the VM guest is Intel).
- **Version:** `scripts/version.sh` is the same resolver in both repos: env override
  (`PUI_VERSION` / `NAVIDROME_VERSION`) → `git describe` against `v*` → `version.txt`
  (`export-subst`). No version is kept by hand. CI checks out with `fetch-depth: 0`. Run
  `git fetch --tags` after each release.
- **Release:** semantic-release with Conventional Commits (`feat` = minor;
  `fix`/`perf`/`refactor` = patch; `chore`/`docs`/`ci`/`style`/`test` = no release). `main`
  is protected (PRs only) and nothing is committed back. `CHANGELOG.md` is kept by hand in the
  release PR. `notify-n8n` publishes to foobar2000.org. If the head commit is non-releasing,
  dispatch the workflow by hand.
- **Release asset:** one `<component>_<version>.fb2k-component` per release, labelled
  `<component> <version> (macOS + Windows x86/x64/ARM64)`. Every place that uploads it
  (`.releaserc.json`, any `gh release upload --clobber` re-upload) uses that same label.
- **Commits:** author Santiago Rodriguez only, no AI trailers. Push only at the end of an
  iteration.
- **CI path filters:** files that aren't part of the component don't trigger CI builds:
  `**.md`, issue forms, `LICENSE`, `.gitignore`, and local-only dev/test scripts and tools. The
  `paths-ignore` lists live in foo_ui_panels' `build.yml` (push + PR; a new PR push cancels the
  superseded run) and in foo_navidrome's `release.yml` (push). A new local-only script or tool
  is added to that list in the same change. Anything CI builds, tests or packages with stays off
  it. A release-worthy commit that only touches ignored paths doesn't release; dispatch the
  workflow by hand.

### Code style and working rules
- **Cross-platform:** every feature or fix lands on Windows and macOS. Logic goes in shared,
  SDK-free code covered by unit tests; only the wiring is per-platform.
- **No code comments:** none in `src/`, `tests/`, `scripts/`, `tools/`, the `Makefile` or the
  workflows. The *why* goes in CLAUDE.md or the commit message.
- **README:** every user-visible change updates the README in the same change.
- **Make targets:** a command run more than once, or with easy-to-forget flags, becomes a
  `make` target.

### Logging and diagnostics
- **Log file:** `<profile>/<component>.log` (`foo_ui_panels.log`, `foo_navidrome.log`). Shape:
  - WARN+ by default; INFO when the component's verbose toggle is on.
  - Rotates to `.1` past 2 MB.
  - Each session opens with an always-written NOTE banner (component, foobar2000, OS, arch,
    Wine versions).
  - Lines from before the log path is known are buffered.
- **Copy Diagnostics:** an in-memory ring of recent lines feeds the Copy Diagnostics report in
  Preferences, on both platforms. The text is pasted into public issues, so it is always
  redacted: home → `~`, user name, server host, auth/URLs (`scrubAuth`). Issue forms live in
  `.github/ISSUE_TEMPLATE/`.
- **Keep the logging matched:** a new logging or diagnostics feature (level, field, redaction,
  rotation) is mirrored in the other repo unless there's a stated reason not to.

### Test environments
- **Wine:** `~/.foobar2000` (fb2k v2.26 x64), with both components in
  `profile/user-components-x64/<name>/`. Each repo's `win-build`/`deploy` installs its own DLL,
  and the other stays as last installed. `tools/wclick.c` (originally from foo_ui_panels) posts
  clicks and keys without touching the host pointer.
- **macOS VM:** `../macos-devbox` (`mvm`, dockur/macos, Intel guest). **Windows 11 VM:**
  `../windows-devbox` (`wvm`, dockur/windows), for Dark Mode, DPI and native theming that Wine
  fakes. Never `docker prune`/`rm`/`rmi`.
- **UI tests:** both repos drive the real foobar2000 with one script,
  `scripts/ui-test.sh <wine|win11|mac> <scenario>`: each scenario is written once over a small
  per-platform driver and asserts on the component log, the process and crash reports.
  `ui-test.sh compare <scenario> [platforms]` prints a step matrix that marks where platforms
  differ (`PARALLEL=1` runs them at once; one test agent per platform works the same way).
  Run Windows changes on Wine *and* the Win11 VM: Wine is an emulation and real Windows
  differs (dark-mode theming, popups, focus). Check the screenshots, not only the log.

### Troubleshooting
- **DLL fails to load** ("No access to memory location"; fb2k falls back to the Default UI):
  usually a foobar2000 object created at static init. Create it lazily.
- **Is the DLL loaded under Wine?** `grep -i foo_ /proc/$(pgrep -f 'foobar2000[.]exe')/maps`.
  Check the component log for a new session banner.
- **`pkill -f`/`pgrep -f`:** a pattern that also appears in your own command line kills your
  shell. Use `'foobar2000[.]exe'`.
- **No debugger:** logging is the diagnostic channel. Add log lines at decision points instead
  of guessing.
- **Build breaks right after an SDK sync:** see SDK › "Upstream breakage" before touching our
  code.

## Key decisions
- Target service (Windows): full UI replacement via `user_interface` (`foobar2000/SDK/ui.h`, v1–v4),
  `FB2K_MAKE_SERVICE_INTERFACE_ENTRYPOINT`. Same slot as foo_ui_classic / Columns UI.
- Build (Windows): **cross-compile on Linux** (clang-cl + lld-link + xwin Windows SDK/CRT). No MSVC.
- Build (macOS): CMake `APPLE` branch, same SDK/pfc/shared source sets as the SDK's .xcodeproj
  targets. Native on a Mac/CI, or cross on Linux (clang + ld64.lld, `cmake/clang-macos.cmake`,
  MacOSX.sdk from an Xcode .xip via `scripts/extract-macos-sdk.py`).
- Reference skin / format spec source: **fooAvA 1.05** (dawxxx666). Its config XML = the
  Panels-UI format to reverse-engineer. Full fooAvA also needs `foo_chronflow` (dead, not replicated).
- **Skin-agnostic engine**: no skin's file names, pvar names, art paths or layout numbers in
  `src/` — those go in the skin's `foo_ui_panels.ini` (`SkinConfig`, keys documented in README
  "Skin configuration"), with neutral defaults. fooAvA's ini is generated by `tools/extract_fooava.py`.
  Main script: Preferences override → ini `script` → the only top-level `*.txt`.
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
  no-op if the other component isn't installed. See `skin_actions.cpp`'s `set_rating()`. Pattern +
  full consumer list documented in `foo_navidrome`'s own CLAUDE.md ("Cross-compatibility with
  other same-author components") — add a symmetric note there for any new interface.

## Repo conventions
- Remote: `origin` → github.com/santiagorod92/foo_ui_panels (private). Branch `main`.
- **Commits: author Santiago Rodriguez <santiagom9992@gmail.com> ONLY — no Co-Authored-By trailer.**
- **Don't push per-step.** Iterate locally; push only at end of an iteration round or on request.
- **No code comments**: `src/`, `tests/` (not `tests/golden`, `tests/skins` data), `scripts/`,
  `tools/`, `cmake/`, `CMakeLists.txt`, `build.sh`, the `Makefile` and `.github/workflows` +
  `.github/actions` carry no comments (stripped 2026-10-10, same rule as foo_navidrome) — don't add
  any when writing or changing code: no `//`, `/* */`, `#`, `<!-- -->`, doc blocks or trailing
  notes, including inside code embedded in heredocs/strings (generated headers, `bash -c`
  bodies). A non-obvious *why* goes in this CLAUDE.md (gotchas) or the commit message; script
  help goes in its `usage()` heredoc. String-literal data (skin scripts in raw strings such as
  `skin_templates.cpp`/`builtin_skin.cpp`, the ini text in `tools/extract_fooava.py`) and the
  markdown inside `.github/ISSUE_TEMPLATE/` form values aren't comments — leave them.
- **Every new feature/fix that changes user-visible behaviour updates `README.md` in the same
  change** (*What's implemented*, *Roadmap*, *Installation*, *Works with foo_navidrome*… as
  relevant) — the README is the public doc and must stay in sync with the code.
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
  menu, colour picker, editor window, embedded foreign UI element. Key/mod codes: `src/ui/keys.h`
  (SDK-free). `src/ui/host_input.h` = what every platform host does with a view (start, paint +
  focus ring, press takes focus, key → view → Tab) — both hosts call it, don't re-implement.
- `src/core/script_runtime.{h,cpp}` — the `$functions` (SDK-free): ONE table (`script_functions()`)
  drives dispatch, skin diagnostics and the generated `docs/SCRIPT_FUNCTIONS.md` (`make docs`; a
  test fails if stale). Needs from fb2k/platform go through `ScriptEnv`; engine state is
  `ScriptState`. New function = table row + `f_<name>` + test (`tests/recording_canvas.h`).
  fb2k evaluates a hook function's params before calling it, so a sized `$imageabs2` used as a
  `$button`/`$button2` state image would draw at 0,0: every skin-script compile goes through
  `defer_button_images()` (script_util), which quotes those into literal button-state text.
- `src/core/skin_engine.{h,cpp}` — `SkinEngine`: panel registry/dispatch, mini mode, theme/assets,
  shared frames. Split by concern: `skin_hook.cpp` (titleformat hook = `ScriptEnv` impl, render,
  draw_script, diagnostics), `skin_actions.cpp` (button actions, clicks, rating, tray),
  `skin_files.cpp` (load, hot reload, panel scripts, pvar storage). Read the header first.
- `src/core/skin_lint.{h,cpp}` + `tools/skin_lint.cpp` — offline skin check (`make skin-lint
  LINT_DIR=<skin>`), SDK-free.
- `src/core/image_cache.{h,cpp}` — image cache, wildcard cover paths, album-art pipeline.
- `src/core/skin_config.{h,cpp}` — per-skin `foo_ui_panels.ini` + main-script resolution.
- `src/core/skin_paths.{h,cpp}` — skin folder settings/resolution (`component_dir()` is platform);
  also the persisted main-script override (keeps `skin_config.cpp` SDK-free).
- SDK-free, unit-tested (`tests/`, `make test`): `script_util` (script/ini parsing, `$eval`,
  menu-action matching), `lyrics_parse` (LRC, lrclib JSON), `playlist_ops.h`, `meter_math.h`,
  `skin_config`, `ui_logic` (zoom steps/mapping, mini-mode plan, skin-command parsing, album search/sort),
  `list_logic` (native panels' geometry/scroll/keyboard nav: `GroupedRows`, `TreeRows`, `GridLayout`,
  `CoverFlow` — panels keep only fb2k + drawing calls), `prefs_model` (+ `prefs_text` wording,
  picker lists), `builtin_skin`, `skin_templates` (layout wizard + its built-in layouts), `script_runtime` (+ `pvars.h`), `skin_lint`. Panel type → kind is `panel_kind()` (script_util).
  `make test` compiles them WITHOUT the SDK include path — never `#include "../fb2k.h"` there;
  new pure logic goes in one of these (or a new SDK-free file added to `TEST_SRC`) with a test.
- Golden render tests (`tests/test_render_golden.cpp`): scripts run via `tests/mini_titleformat.h`
  (test-only titleformat subset) and every canvas op compared with `tests/golden/*.txt`;
  `UPDATE_GOLDEN=1 make test` regenerates. `tests/skins/synthetic` must call every script function
  (a test checks) and lint clean (CI). It's ours — `.gitignore` re-includes it past `skins/`.
- Logging/diagnostics: `src/core/log.{h,cpp}` (SDK-free, tested) — `pui::log::info/warn/error/note`
  to `foo_ui_panels.log` in the profile (INFO only with *Verbose logging*, 2 MB rotation to `.1`,
  session banner, 400-line ring kept for diagnostics even when not written); `log::console(level,…)`
  also prints "Panels UI: …" to the fb2k console — use it instead of `console::print`.
  `log::guarded(tag, what, fn)` logs exceptions. Lines written before `configure()` are kept
  pending, INFO ones too, and filtered by the verbose setting when the file opens: the UI module
  creates its window before `start_logging()`, and dropping INFO there lost `main window created`
  on every session. `src/core/diagnostics.{h,cpp}` (SDK-free, tested)
  builds the Copy Diagnostics report (home dir → `~`, user name → `<user>`);
  `diagnostics_collect.cpp` fills it (`SkinEngine::describe`, installed components) and starts the
  log (`start_logging()`, from `initquit` and the first `load_skin`, whichever runs first — the UI
  module initialises before `initquit`). Prefs › Diagnostics tab on both platforms; platform bits
  are `ui::copy_to_clipboard/os_version/wine_version/home_dir`. Issue forms in `.github/ISSUE_TEMPLATE/`.
- `src/core/ui_settings.{h,cpp}` — zoom setting + always-on-top (the core's standard config
  object, applied via `config_object_notify`); `main_menu.cpp` — View › Panels UI commands.
- Zoom: engine/views always work in skin units. Windows hosts convert with `pui::win::zoom()`
  (`platform/win/zoom.h`) and `GdiCanvas` scales via a GDI world transform (GDI+ drawn with an
  identity DC transform + its own scale — Wine's GDI+ inherits the DC transform); macOS scales
  the root view's bounds (`set_root_zoom`).
- `src/core/component.cpp` — `DECLARE_COMPONENT_VERSION`; welcome screen script in `builtin_skin.cpp`.
- `src/core/builtin_images.h` — images built into the component: `res/**/*.png`, embedded at configure
  time (`cmake/embed_images.cmake`), drawn as `builtin:<path>` (the wizard's `res/wizard/<id>.png`,
  regenerated by `scripts/wizard-previews.sh` — demo library, never real music/art).
- `src/core/button.h`, `fs_util.h` (UTF-8 paths/files), vendored `navidrome_*_api.h` (GUIDs
  are C++17 `inline` variables — `FOOGUIDDECL` is Windows-only `selectany`).
- `src/panels/` — native panels, one `ui::View` each: `track_display`, `seekbar`, `volume`,
  `playlist_view`, `spectrum` and `peak_meter` (painted on the host's render thread,
  `render_fps`), `album_art` (now-playing cover, "Album Art"), `popup`
  (settings popup), `album_list` ("Graphical Browser"/"Album list"/"Chronflow" — no stock DUI
  element; Media Library + Navidrome albums, grid or cover flow), `lyrics_panel` ("Lyric Show":
  LRC sync, tags → sidecar → cache → `navidrome_lyrics_api` (navidrome:// tracks, always on, worker
  thread, result cached as LRC via `lyrics_text`) → lrclib.net; `lyr.*` pvars), `quick_search`, `library_tree`
  ("Playlist switcher", self-drawn tree), `cover_menu` (now-playing cover right-click menu +
  full-size viewer, shared by album_art/track_display).
- Engine extras: script problems + "!" marker (`script_problems`, `draw_problem_marker`), per-panel
  view state in `_view.*` pvars (`view_state`, saved at shutdown), Tab traversal
  (`focus_next_panel`, `ViewOptions::on_tab`, hosts draw `draw_focus_ring`), welcome screen =
  `builtin_test_skin()` + actions `SKIN:CHOOSE_FOLDER`/`PREFERENCES`/`WIZARD:OPEN`; layout wizard =
  `layout_wizard_script()` (engine `m_wizard` mode, `SKIN:TEMPLATE:<id>` writes the layout to
  `layout_skins_dir()`), shown once on first run (`first_run_setup`).
- `src/platform/win/` — `main.cpp` (`user_interface`, main window = `ui::MainWindow`, menus,
  keyboard, `$settray` icon via `tray.h`), `win_view.cpp` (`WinViewHost` child HWNDs + every `ui::` service),
  `gdi_canvas.{h,cpp}` (GDI + GDI+), `panel_host` (hosts a DUI `ui_element` by name),
  `preferences.cpp` (Preferences page view, plain Win32 — no ATL/WTL in our toolchain; Dark Mode
  via `dark_mode.{h,cpp}`, since the SDK's hooks need ATL or real dialogs).
- `src/platform/mac/` — `mac_main.mm` (`ui_element_mac` "Panels UI", root canvas =
  `ui::MainWindow`; skins in `~/Library/foobar2000-v2/foo_ui_panels`), `mac_view.mm` (flipped
  NSView hosts + every `ui::` service), `mac_canvas.{h,mm}` (CoreGraphics/CoreText/ImageIO,
  1 canvas unit = 1 pt at backingScaleFactor px, dpi 96), `mac_preferences.mm` (same
  tabs as Windows, applies on every change).
- Preferences logic: `src/core/prefs_model.{h,cpp}` (SDK-free, tested) — both pages are views
  over `PrefsModel`; storage + going live is `PrefsBackend` (`prefs_store.cpp`). A new setting
  goes in the model first. `ui::MainWindow::capture()` (window + panels) feeds the skin preview
  cache (`SkinEngine::save_preview`).
- Text drawing on Windows: **always wide APIs** (GdiCanvas converts UTF-8). `DrawTextA`
  mojibakes non-ASCII foobar strings (titleformat output, tags).
- fooAvA config decode tooling: `tools/extract_fooava.py` (s8.bin → `fooava.txt` + `panels/*.txt`,
  handles paren-balancing/pvar-seeding needed for fb2k titleformat to actually execute),
  `tools/recover_fooava_images.py` (ClickTeam SFX asset extraction). Don't reintroduce the old
  size-collision image-mapping heuristic — it mismapped same-size PNGs; offset-based mapping is exact.
  `tools/setup_fooava.py` = the user-facing one-step wrapper (installer → skin folder, default
  `Documents/fooava`). Per-panel records are cut only at names some `$panel(name,` uses: stray
  length-prefixed bytes inside a script (e.g. `\x02\0\0\0ts`) otherwise split it (lost `ChronTIM`).
  Left-column **L** view: the extractor swaps the full-column `Lyric Show` for `Display` and puts a
  `LyricsCD` Lyric Show in the case's cover rect (`lyrics_in_cd_case`, `$get(lyrcd)`).
  Settings popup (`tidy_settings_popup`): its background + frame block is moved first (drawn late
  it covers the options) and CHOOSE FONT is left-aligned (right-aligned at -45 it's cut off).
- `scripts/ui-test.sh <wine|win11|mac> <scenario>` — one UI test driver for every platform
  (see Shared conventions › Test environments): `smoke` turns verbose logging on
  (`cfg_var.2F6C9A41-…` in configInts), deploys, relaunches and asserts the session banner,
  `loading skin`, `main window created` (logged on both platforms), playback (`playback  new track`, INFO from the skin engine's play callback), no crash/ERROR,
  screenshot; `compare` prints a step matrix across platforms. Wine helpers kept: `click`
  (`tools/wclick.c`), `shot`, `pvars`, `restart`. Make: `ui`, `ui-smoke`, `ui-compare`,
  `win11-smoke`, `mac-vm-smoke`. Win11 reads the guest log by pulling it over scp.

## Build (Arch Linux cross-compile)
- Prereqs: `clang-cl`, `lld-link`, `cmake`, `ninja`; `cargo install xwin && xwin --accept-license --arch x86,x86_64,aarch64 splat --output ~/.xwin`.
- Build: `./build.sh` (or `make build`) → `build/foo_ui_panels.dll` (PE32+ x64, exports `foobar2000_get_interface`).
  `WIN_ARCH=arm64ec ./build.sh` (`make build-arm64ec`) → `build-arm64ec/` (Windows on ARM, native).
  `WIN_ARCH=x86` (`make build-x86`) → `build-x86/` (32-bit fb2k v2, links `shared-Win32.lib`).
  xwin must be splatted `--arch x86,x86_64,aarch64`. No ARM runtime test env (Wine is x86) — CI only builds it.
  x86 runtime: the 32-bit fb2k installer unpacks with `7z x` into a portable folder; components go in
  `profile/user-components/` (no `-x64`).
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
  mirror). Version in use + bump procedure: Shared conventions › SDK. The 2026-09-16 sync bumped `pfc-lite.h` to require C++20 —
  `CMakeLists.txt`'s `CMAKE_CXX_STANDARD` must stay at 20.
- Version: `scripts/version.sh` → `<build>/generated/version_generated.h` (`PUI_VERSION`, a
  CMake target run every build, header rewritten only on change) → `component.cpp`'s
  `DECLARE_COMPONENT_VERSION`, i.e. what *Preferences › Components* shows. Order: `$PUI_VERSION`
  (release jobs: `release-build.sh`, `release-mac` — the tag doesn't exist yet while
  semantic-release builds) → `git describe` (`1.6.0` on a clean tag, `1.6.0-dev.2+ef96f46[.dirty]`
  after it) → `version.txt`, only without `.git`: in the repo it's a `$Format:%(describe…)$`
  placeholder `git archive` expands (`.gitattributes` `export-subst`). No version number is kept
  by hand; releases are the `v*` tags (`git fetch --tags` after one, or builds describe against
  the previous). The mac Info.plist takes plain x.y.z (`--base`, fixed at configure). Same
  resolver as foo_navidrome's `scripts/version.sh` (there: `NAVIDROME_VERSION`/`COMPONENT_VERSION`).
  `build.yml` checks out with `fetch-depth: 0` so CI builds see the tags.

## CI / release
- `.github/workflows/build.yml` — build check on push/PR (`paths-ignore`: Shared conventions › CI
  path filters), parallel jobs: `build` matrix (x64 / arm64ec / x86, one `build.sh` each, ccache
  per arch via `setup-build`'s `ccache-key`, persisted with `actions/cache`), `test`
  (`check-portable` + `make test` + skin-lint, `setup-build` with `xwin: false`) and `build-mac`
  (universal bundle, uploaded as a `ditto` zip). On a push to main that will release (same
  head-commit gate as Release) a `version` job runs `semantic-release --dry-run` first
  (`verifyReleaseCmd` → `next_version`) and every build is stamped with it (`PUI_VERSION`, also
  `version.sh --base` for the mac Info.plist) + a `release-stamp` artifact.
- `.github/workflows/release.yml` — semantic-release (`.releaserc.json`, Conventional Commits,
  same config as foo_navidrome), chained to Build via `workflow_run` (only after a green Build of
  a push to main; checks out that run's `head_sha`) or dispatched by hand. It downloads that Build
  run's artifacts; `scripts/release-build.sh <ver>` packages them when `release-stamp` equals the
  real next version (DLLs + mac bundle → complete asset, `has_mac=true`, so `release-mac` and
  `release-package` are skipped) and otherwise builds the DLLs itself (dispatch, version drift);
  then `release-mac` builds the bundle from the tag and `release-package` re-uploads the asset
  with `mac/` added (`--clobber`) before `notify-n8n`. Commit type = release impact:
  `feat` minor, `fix`/`perf`/`refactor` patch, `chore`/`docs`/`ci`/`style`/`test` none.
  The head-commit gate is duplicated in Build's `version` job and Release's `release` job — keep
  them identical.
- `main` is branch-protected (PR + review required; admins exempt): the release workflow never
  pushes to it — only the `v<ver>` tag and the GitHub release (notes = changelog; no
  `@semantic-release/git`/`changelog` plugins). `release-mac` passes `PUI_VERSION` itself. A PR
  squash-merged as `ci:`/`docs:`/`chore:` skips the release gate: dispatch the workflow by hand
  if earlier release-worthy commits are still unreleased.
- `CHANGELOG.md` is kept by hand (no plugin can write it: `main` is protected): a release PR adds
  its version's section (Highlights / New / Changed / Fixed / For contributors, user wording), and
  after the release the GitHub release notes are replaced with that section (`gh release edit
  v<ver> --notes-file`) — the generated notes only list commit subjects.
- Shared CI setup: `.github/actions/fetch-sdk` (SDK sibling layout) and
  `.github/actions/setup-build` (that + clang-cl/xwin).
- `notify-n8n` job → n8n workflow (infra-foundations `n8n/`) that uploads to foobar2000.org
  componentsadmin. Gated on repo variable `FOOBAR_ORG_PUBLISH=true`; needs a homelab runner
  registered for this repo (infra-foundations `github_runner_instances`) + the
  `N8N_FOOBAR_RELEASE_WEBHOOK_URL/_SECRET` secrets.

### Cross-build gotchas (handled in build.sh/toolchain, don't re-discover)
- Static release CRT only: xwin ships no debug CRT → toolchain forces `/MT` Release.
- ARM64EC (clang-cl, LLVM 22): links the aarch64 splat (ARM64X CRT) + the **x64** static UCRT
  (aarch64's has no EC objects); `src/platform/win/arm64ec_compat.h` (force-included) stubs pfc's
  `__rdtsc`/`__cpuid`; `/U__x86_64__` (clang defines it, MSVC doesn't); `-fno-builtin` (libm
  intrinsic libcalls get no exit thunk → `undefined symbol: #log10`).
- Header case-sensitivity: SDK includes cased headers; xwin lowercases — build.sh symlinks cased names.
- Include order in main.cpp: `WIN32_LEAN_AND_MEAN`+`<winsock2.h>` before `<windows.h>`, then
  `<objbase.h>` (COM `interface` macro) + `<mmsystem.h>` (`timeGetTime`).
- Link `shared-x64.lib` / `shared-ARM64EC.lib` / `shared-Win32.lib` (fb2k `shared.dll` import libs, at `../foobar2000/shared/`).
- libPPUI rich controls deferred to Phase 2+ (needs WTL+ATL, not in xwin).

## Deploy/test (Wine)
- foobar2000 v2.26 at `~/.foobar2000/` (shared with foo_navidrome, see Shared conventions › Test environments).
- Deploy DLL to `~/.foobar2000/profile/user-components-x64/foo_ui_panels/foo_ui_panels.dll`.
  Fully restart fb2k to reload.
- No fb2k objects at static init — incl. members of the (static) `user_interface` instance's
  `SkinEngine`: `abort_callback_impl`, `play_callback_impl_base`, `::get()` calls. The DLL fails
  to load ("No access to memory location") and fb2k falls back to the Default UI. Create lazily.
- `titleformat_object::run()` with a null hook crashes the core — pass an empty hook.
- Window edge resize under Wine: Wine turns an edge drag (`HTLEFT`…) into `_NET_WM_MOVERESIZE`,
  which Hyprland ignores — `main.cpp` runs its own capture/`SetWindowPos` loop when `wine_version()` is set.
- Full UI replacement must forward `WM_KEYDOWN`/`WM_SYSKEYDOWN` to
  `keyboard_shortcut_manager::on_keydown_auto(wp)` or shortcuts break.
