# foo_ui_panels — Panels UI (reborn)

A [foobar2000](https://www.foobar2000.org/) v2 (64-bit) user interface component that brings
back **Panels UI**: the scriptable, skinnable interface where the whole player window is a
titleformat-driven canvas and panels, buttons, images and text are placed wherever the skin
wants them.

The original Panels UI (`foo_ui_panels`) stopped at foobar2000 0.9.5.2 and was never released
as source. This is a from-scratch reimplementation for foobar2000 v2, built with one concrete
goal: making the **fooAvA** skin fully usable again on a current foobar2000.

## Why this exists

I've been a foobar2000 user since its early days. When Panels UI came out, people started
building incredible skins with it — and fooAvA by **dawxxx666** was the one that stuck with me.
Then foobar2000 moved on, Panels UI stayed behind at 0.9.5.2, and those skins stopped working.

As a foobar2000 user I love how far it lets you customize everything; that's exactly why it's
the player I use every day. So I decided to revive Panels UI and get fooAvA running again.

Before starting, I tried to reach the original author of Panels UI to ask whether the source
code could be made available, so the project could be continued without taking away their
authorship. I didn't get an answer, so this is a clean reimplementation instead: none of the
original code is used, the skin format was reverse-engineered from fooAvA's configuration.

## Thanks

- **Terrestrial** — author of the original Panels UI (`foo_ui_panels`). All the credit for the
  idea and the skin format is theirs; this project only exists to keep it alive.
- **dawxxx666** — author of the **fooAvA** skin, the reason this project exists. Thank you for
  such a beautiful skin.
  Get it from the author: [fooAvA 1.05 on DeviantArt](https://www.deviantart.com/dawxxx666/art/fooAvA-1.05-91986779)
  (licensed CC BY-NC-SA 3.0 by its author; it is **not** included in this repository or in the
  released component).
- The foobar2000 team, for the SDK and for keeping component development possible.

## Installation

One `.fb2k-component` carries both builds: Windows (foobar2000 v2, 64-bit) and macOS
(universal, macOS 11+).

1. Download `foo_ui_panels_<version>.fb2k-component` from the [Releases](../../releases) page.
2. Double-click it (or drag it onto foobar2000) and restart foobar2000.

**Windows**

3. Pick **Panels UI (reborn)** as the *User interface module* in **Preferences › Display**.
4. Point the component at a skin: **Preferences › Display › Panels UI (reborn)** → skin folder
   (and, if the folder has more than one `.txt` at its top level, which one is the main script).

**macOS** — Panels UI is a layout element there, not a separate UI module:

3. **View › Layout › Edit Layout**, add **Panels UI** and let it fill the window.
4. Put the skin (its main script, `panels/`, images and `foo_ui_panels.ini` if it has one) in
   `~/Library/foobar2000-v2/foo_ui_panels/` — or keep several skins in one folder (one subfolder
   each) and pick it, the active skin and its main script in **Preferences › Display › Panels UI
   (reborn)**. Changes there apply immediately.

The first time it runs with nothing set up, Panels UI writes its default layout — a dark,
Default-UI-like player: toolbar with transport, seek and volume, album art and what's playing
beside the playlist, a status bar — to `foo_ui_panels-skins` in the foobar2000 profile folder,
makes it the skin and opens the **layout wizard** over it. The wizard (also **View › Panels UI ›
Layout wizard…**, or the **Layout wizard…** button in Preferences › Display › Panels UI (reborn),
like the Default UI's *Quick Setup*) offers ready-made layouts — *Playlist and cover*, *Library* (playlists, playlist,
album art), *Album browser* (cover grid + playlist), *Playlist*, *Now playing* (large cover, synced
lyrics, spectrum) — each with a screenshot of it. Picking one writes it out as a skin folder of its own
(`main.txt` + `foo_ui_panels.ini`, a new folder each time, so edits are never overwritten): it is an
ordinary skin from then on, to edit like any other. **Use my own skin folder…** loads any other skin
instead (fooAvA, see below).

Picking a skin folder (the dialog's **OK**) is all it takes: it is saved and the skin loads. A folder
with nothing to run yet is kept all the same, with a warning saying why — no `.txt` main script at
its top level (the skin starts as soon as one appears there, no restart), or several `.txt` files
(Preferences opens to pick the main one). Until a folder has a main script, a welcome screen offers
the wizard, the folder chooser and the Preferences page.

### Setting up fooAvA

fooAvA is distributed by its author (link above) as a Windows installer for the old Panels UI.
The scripts in [`tools/`](tools/) (Python 3, no extra packages) turn it into a skin folder:

1. Download fooAvA 1.05 from the author's DeviantArt page — `fooava_1_05_by_dawxxx666_*.exe`.
   You don't need to run it (it targets foobar2000 0.9).
2. Unpack it into a working folder, every file included:
   `python3 tools/recover_fooava_images.py fooava_1_05_by_dawxxx666_*.exe fooava-files --all`
3. Make the skin folder (say `fooava/`) and copy the unpacked `images/` folder into it.
4. Convert the Panels UI configuration — the skin's layout and its panel scripts — into scripts
   this component runs: `python3 tools/extract_fooava.py <the configuration file> fooava/fooava.txt`.
   This also writes `fooava/panels/*.txt` and fooAvA's `foo_ui_panels.ini`. Convert each popup
   window (`FOOAvA_settings.ava`, `FOOAvA Playlist.ava`, `FOOAvA_about.ava`) the same way, into
   `fooava/panels/<its name>.txt`.
5. Check it: `make skin-lint LINT_DIR=fooava` (from a source checkout) lists anything missing.
6. Point the component at it: the welcome screen's **Choose a skin folder…**, or Preferences.
   On macOS copy the folder to `~/Library/foobar2000-v2/foo_ui_panels/` or choose it there too.

fooAvA's fonts (Calibri, Swis721 Cn BT) are optional: without them it uses the stand-ins its
`foo_ui_panels.ini` names. Cover flow needed the long-gone foo_chronflow; the native one replaces it.

### Skin configuration

The component is skin-agnostic: everything a particular skin needs beyond its scripts lives in
an optional `foo_ui_panels.ini` in the skin folder (`key = value` lines, `#` comments). Without
it, a skin gets neutral defaults. Edits are picked up live, like script edits.

| Key | Meaning (default) |
|---|---|
| `preview` | Image shown for the skin in Preferences (default: `preview.png`/`.jpg` or `screenshot.png`/`.jpg`, else a capture of the running skin). |
| `script` | Main script file. Default: the only `.txt` in the folder's top level. The Preferences page can override it. |
| `panels` | Folder of the per-panel scripts (`panels`). |
| `images` | Folder the native panels' art below is relative to (the skin folder). |
| `theme.accent_pvar` | Variable holding the accent colour, `r-g-b` (none: the Preferences accent override). |
| `theme.index_pvar`, `theme.index_default` | Variable holding the theme number used as `{theme}` in art names (`1`). |
| `font.face_pvar` | Variable holding the face when a script's `$font` names it through a variable. |
| `font.default` | Face used when none is named (`Tahoma`). |
| `font.alias.<face>` | Installed face to use when `<face>` isn't installed. |
| `background.image_pvar`, `background.enabled_pvar`, `background.alpha_pvar`, `background.alpha`, `background.top` | The wallpaper the main script draws (file relative to `images`, on/off variable, opacity, top offset), so native panels can show the same one. |
| `asset.bar_fill`, `asset.volume_knob` | Seek/volume bar art (`{theme}` allowed). |
| `asset.nocover`, `asset.cd_case`, `asset.cd_case_window` | Album browser: missing-cover art, case art and the cover's window inside it (`x y w h`, case pixels). |
| `asset.rating_stars` | Playlist rating-star art, `{n}` = 0–5 stars. Without it the playlist shows no stars. |
| `spectrum.mirror_below`, `spectrum.grow`, `spectrum.raise` | Spectrum strips shorter than N px draw the reflection; grow/raise them by N px. |
| `popup.size`, `popup.size.<file>` | Size of a `POPUP:` window, `W H` (`400 500`). |
| `mini.size`, `mini.anchor` | The skin's compact layout for **View › Panels UI › Mini mode**: client size `W H`, and the corner that stays put (`RIGHT TOP`, …; default top-left). Unset: no mini mode. |
| `mini.saved_w_pvar`, `mini.saved_h_pvar` | Variables that remember the full size while in mini mode — name the skin's own if its restore button reads them (default: reserved ones). |
| `command.<label>` | A skin command listed under **View › Panels UI › Skin commands** (so it can get a keyboard shortcut): one button action, or several separated by `;`. |
| `cover.pvar`, `cover.pattern` | Set this variable to the track folder's cover (`*folder*.*`) before each panel script runs. |
| `panel.remap.<name>` | Replace a `$panel()`: `<new name>` or `<new name>\|<new type>`. |
| `action.remap.<action>` | Run another button action instead. |
| `onboarding.pvars` | First-run flags set to 1 when a popup opens: names, `prefix*`, `!exclusion`. |
| `pvar.once.<name>` | Set this variable once, then leave it to the user. |
| `color.<role>`, `color.<panel>.<role>` | Native panels' colours (`r g b`, `r-g-b` or `#rrggbb`); the panel-specific key wins. Panels: `playlist`, `album_list`, `quick_search`, `lyrics`, `seekbar`, `volume`. Roles: `background`, `overlay` (dimming over the wallpaper), `text`, `text_secondary`, `text_dim`, `highlight` (selection; default: the theme accent), `separator`, `row_text`, `row_text_selected` (playlist), `hover`, `placeholder` (album browser), `frame` (quick search). Unset: the built-in dark look. |

## What's implemented

- Full foobar2000 UI replacement on Windows (`user_interface` service, same slot as the Default
  UI or Columns UI); a full-window layout element on macOS.
- The Panels UI script engine: titleformat-driven canvas with `$panel`, `$button`/`$button2`,
  `$imageabs2`, `$drawrect`, `$font` (incl. glow), `$alignabs`, persistent `$setpvar`/`$getpvar`
  variables and the skin's button actions (transport, playback order, window size, popups…).
- Native versions of the panels fooAvA uses: track display (CD case, rating stars), seek bar,
  volume, grouped playlist, spectrum analyser, peak meter, album art, album browser + cover
  flow, playlist switcher, quick search and a synced-lyrics panel — all drawn by the component
  itself, so they work the same on macOS.
- Grouped playlist: keyboard navigation (arrows, Page Up/Down, Home/End with Shift to extend
  the selection, Enter to play, type a few letters to jump to a title or album artist), drag
  selected tracks to reorder them, and drop files or folders from Explorer / Finder onto it
  (dropped anywhere else in the window, they're added to the end of the active playlist).
- Lyrics panel: a timing correction per track on top of the global one, "Search online again"
  to replace wrong lyrics (even ones from tags or a sidecar file), and lrclib.net's
  instrumental tracks shown as such. Synced lyrics found anywhere (tags, sidecar `.lrc`, cache)
  win over plain ones.
- **Karaoke highlight**: with word-timed lyrics (enhanced LRC, `<mm:ss.xx>` before each word or
  syllable) the current line fills in word by word as it's sung. Right-click › **Karaoke
  highlight** can also estimate it for ordinary line-timed lyrics, or turn it off.
- **Sync lyrics by tapping**: right-click › **Sync these lyrics by tapping** turns plain (or
  badly timed) lyrics into LRC. The track restarts, and you press Space or click as each line
  starts (Backspace undoes the last one and jumps back a little, Esc cancels). Then right-click
  to save it as a `.lrc` next to the track (local files) or in the lyrics cache. Lyrics in the
  file's own tags still come first on later plays.
- Right-click **Edit code…** on a panel to edit its script, applied live with **Apply** or
  Ctrl+S (⌘S on macOS): the syntax is coloured, a status line shows the cursor position and a
  quick check (unknown functions, missing arguments, unbalanced parentheses), and **Revert** goes
  back to the script as it was when opened. Or edit the skin's scripts or `foo_ui_panels.ini` in
  any editor: saved changes are picked up within a second.
- Skin diagnostics in **View › Console**: functions the skin uses that aren't supported (yet),
  missing skin images, and panels no installed element can host.
- Script problems on the skin itself: a panel whose script is missing or doesn't compile gets a
  small red **!** in its corner — hover for the details, click to edit the code (the main
  script's shows at the bottom right of the window). **View › Panels UI › Show script problems**
  turns the markers off.
- The player window reopens where you left it, at the same size — and so do its views: each
  playlist's scroll position and the album last picked in the album browser / cover flow.
- **Zoom**: the whole skin scales with the display (automatic: Windows display scaling; old
  skins were drawn for 96 dpi and look tiny on a high-DPI screen otherwise), or by hand from
  **View › Panels UI › Zoom in / Zoom out / Reset zoom** or the Preferences page — 75% to 300%,
  sharp text and art at any size. The window keeps the skin's size in skin pixels.
- **View › Panels UI** menu — every entry can get a keyboard shortcut (Preferences › Keyboard
  Shortcuts): **Mini mode** (the skin's compact player, for skins that declare one —
  `mini.size`), zoom, **Reload skin**, **Layout wizard…**, **Skin ›** (switch between the skins in your skins
  folder, applied right away) and **Skin commands ›** (the skin's own `command.<label>` actions).
- Keyboard and screen readers: **Tab** / **Shift+Tab** move between the panels that take keys
  (playlist, album browser, playlist switcher, search, lyrics), the focused one outlined; each
  panel has a name screen readers announce ("Playlist", "Album browser"…).
- The system's media controls (Windows' media overlay / SMTC, macOS Now Playing) are run by
  foobar2000 itself for whichever UI owns the main window; Panels UI hands the core its window
  the same way the Default UI does, so nothing extra is needed.
- foobar2000's **View › Always on Top** (Alt+A) keeps the Panels UI window on top; also on the
  Preferences page.
- Skins can use these from their own buttons too: `ONTOP:TOGGLE`, `ZOOM:IN`/`ZOOM:OUT`/
  `ZOOM:RESET`, `MINIMODE:TOGGLE`, `PVAR:TOGGLE:<variable>` (flips a 0/1 variable),
  `PREFERENCES` (this component's Preferences page), `URL:<address>` and `SKIN:CHOOSE_FOLDER`.
- Switching skin or main script in Preferences applies immediately (no restart). The skin picker
  shows a preview of the skin: its own (`preview` in `foo_ui_panels.ini`, else a `preview.png` /
  `screenshot.png` in its folder), else a picture the component takes of it the first time it is
  shown (kept in the profile's `foo_ui_panels-previews` folder).
- The Preferences page follows foobar2000's **Dark Mode** (Preferences › Display › Default User
  Interface; on *Auto* it tracks Windows' app theme live, also with Panels UI as the UI module).
- Album browser / cover flow: type to search by artist or album (Backspace edits, Esc clears),
  right-click empty space to sort by artist, album, year or recently added (remembered), arrow
  keys / Page Up/Down / Home/End move through the grid.
- The Album Art panel cross-fades to the new cover on a track change.
- Right-click the now-playing cover (the Album Art panel, or the panel a skin draws its cover
  in): **View cover** at full size, **Open cover file**, **Show in folder**, or **Search for the
  cover online**.
- Button tooltips and the skin's own window title (`$settitle`, e.g. "artist - title" while
  playing).
- The skin's tray icon (`$settray`): on Windows a notification-area icon — click to show/hide the
  player, right-click for play/pause, stop, previous/next, show/hide and exit — and minimising
  sends the player to the tray; on macOS the same menu from a menu-bar icon.
- GDI+-style outlines (`$gp_set_pen` + `$gp_draw_rectangle`) next to the existing brush fills.
- **macOS support**: the same engine and panels run on macOS (Cocoa + CoreGraphics/CoreText),
  shipped in the same `.fb2k-component` as the Windows build, with the same Preferences page
  (General, Script, Variables and Overrides tabs, same wording and choices — including "the
  component's own folder" as a skin) and full-resolution Retina rendering.
  Panels that host foobar2000's own UI elements only work if a Mac element of the same name
  exists.

## Works with foo_navidrome

[foo_navidrome](https://github.com/santiagorod92/foo_navidrome) — browse and stream a
Navidrome / Subsonic server inside foobar2000 — is written by the same author, and the two
components cooperate when both are installed (nothing to configure, and each works fine on its
own):

- **Ratings**: the skin's rating stars (now-playing panel and playlist) work on Navidrome tracks
  too — the rating is saved on the server through foo_navidrome, so it shows up in Navidrome's
  web UI and every other client.
- **Album browser / cover flow**: your Navidrome albums appear next to the Media Library ones,
  with their cover art; clicking one plays it.
- **Playlist switcher panel**: browse Navidrome artists and albums and play them straight from
  the skin.

## Roadmap

- More Panels UI skins from the 0.9.x days (only fooAvA has been tried so far — see Contributing).

## Contributing

Everyone is welcome to contribute — bug reports, fixes and new features alike.

**Other old Panels UI skins.** This project was built around fooAvA, but the goal is to run any
Panels UI skin from the foobar2000 0.9.x days. If you try another skin and something renders
wrong, a function isn't supported, or a panel is missing, just
[open an issue](../../issues) — it will be looked into. Helpful details:

- the skin's name and where to get it (link to the original download);
- your OS (Windows / macOS), foobar2000 version and component version;
- a screenshot of what you see (and, if you can, of how it should look);
- the script of the affected panel (right-click › **Edit code…**) or the titleformat function
  that fails;
- the `Panels UI:` lines from **View › Console** — they list what the skin uses that isn't
  supported — or, with a source checkout, the output of `make skin-lint LINT_DIR=<skin folder>`,
  which checks a skin folder without foobar2000 (functions, images, panel scripts, `asset.*` art).

The script functions the engine implements are listed in
[docs/SCRIPT_FUNCTIONS.md](docs/SCRIPT_FUNCTIONS.md).

**Pull requests.** Fixes and new features are welcome. The tools and versions you need are
listed under [Development environment](#development-environment). A few conventions keep
things smooth:

- **Every change is cross-platform.** Logic lives in platform-free C++ (`src/core`,
  `src/panels`); anything that needs the OS goes through `src/gfx/canvas.h` / `src/ui/view.h`
  and is implemented for both `src/platform/win` and `src/platform/mac`. `make check-portable`
  (also run in CI) verifies the core compiles without Windows headers.
- Both builds should pass: `./build.sh` (Windows) and `./scripts/mac-build.sh` (macOS) — CI
  checks both on every PR.
- `make test` runs the unit tests (`tests/`, host compiler, under ASan/UBSan — also in CI). They
  cover the parts of the engine that don't need foobar2000: the script functions themselves
  (against a recording canvas), script/config parsing, `$eval`, menu action matching, LRC/lrclib
  parsing, playlist reordering, the native panels' layout/scrolling/keyboard logic
  (`src/core/list_logic`), the Preferences model, the skin linter. Add a case when you touch
  those. Whole scripts are covered by *golden* render tests: the welcome screen and
  `tests/skins/synthetic` (a test skin that calls every script function and places every panel
  kind) are run and each canvas call compared with `tests/golden/*.txt` — when a change in what
  they draw is intended, `UPDATE_GOLDEN=1 make test` regenerates them; review the diff. A new
  script function is a row in the table in `src/core/script_runtime.cpp` plus a call in
  `tests/skins/synthetic/synthetic.txt` (a test checks); run `make docs` after.
- Use [Conventional Commits](https://www.conventionalcommits.org/) (`feat:`, `fix:`, …): they
  drive the automated releases (see [Releases](#releases)).
- Update this README when you add or change a feature (e.g. *What's implemented*, *Roadmap*),
  so the docs stay in sync with the code.

Not sure where to start, or want to discuss an idea first? Open an issue.

## Building from source

### Development environment

No Windows PC or Visual Studio needed: the Windows DLL is cross-compiled on Linux, and the macOS
bundle builds either on a Mac or cross-compiled on Linux. CI builds both on every PR. For
reference, this is what the project is developed and tested with today:

| | Used for development | Minimum / notes |
|---|---|---|
| **Dev OS** | Linux ([Omarchy](https://omarchy.org), Arch-based) | any Linux with LLVM ≥ 19 (CI: Ubuntu 24.04), or a Mac for the macOS build |
| **foobar2000** | v2.26 for Windows (x64, under Wine 11 Staging) and v2 for Mac (latest) | foobar2000 v2.x |
| **foobar2000 SDK** | `SDK-2026-09-16` from [reupen/foobar2000-sdk-unmodified](https://github.com/reupen/foobar2000-sdk-unmodified) (`build.sh` and CI fetch the latest tag) | a foobar2000 v2 SDK; only `SDK-2026-09-16` and newer are tested |
| **C++ standard** | C++20 | |
| **Windows compiler** | clang-cl / lld-link (LLVM 22) + CMake 4 + Ninja | LLVM **19+** (the MSVC STL headers reject older Clang), CMake 3.21+; Visual Studio/MSVC isn't used or tested |
| **Windows SDK / CRT** | Windows SDK 10.0.26100 + MSVC CRT, fetched by [xwin](https://github.com/Jake-Shadle/xwin) | |
| **macOS toolchain** | Linux cross-build with `clang` + `ld64.lld` against the macOS 14.5 SDK (from Xcode 15.4); CI uses the `macos-latest` runner's Xcode | Xcode command line tools + CMake + Ninja on a Mac; deployment target **macOS 11.0**, universal (arm64 + x86_64) |
| **Runtime testing** | Wine (fast loop), a Windows 11 VM ([dockur/windows](https://github.com/dockur/windows)) and a macOS 14 Sonoma VM ([dockur/macos](https://github.com/dockur/macos)) | any Windows 10/11 or macOS 11+ machine with foobar2000 v2 |
| **Build tooling** | GNU Make, bash, git | |

The unit tests (`make test`) and the skin linter use the host compiler and need neither the SDK
nor foobar2000.

### Happy path: build it and try it live

`make help` lists every target.

**Linux (Windows build, running under Wine)**, the main dev loop:

```sh
sudo pacman -S --needed clang lld llvm cmake ninja          # once (Arch shown)
cargo install xwin && xwin --accept-license splat --output ~/.xwin
make test        # unit tests + golden render tests (host compiler, ASan/UBSan)
make run         # build.sh (fetches the SDK if missing), install into Wine foobar2000, relaunch
```

Pick **Panels UI (reborn)** as the *User interface module* in *Preferences › Display* and
point it at a skin folder (see [Installation](#installation) and
[Skin configuration](#skin-configuration)). After each code change
run `make run` again. `make launch` restarts foobar2000 without rebuilding, and
`make skin-lint LINT_DIR=<skin folder>` checks a skin without running foobar2000.

**macOS:**

```sh
make test
make mac-build   # build-mac/foo_ui_panels.component (universal)
```

With foobar2000 quit, copy the bundle to
`~/Library/foobar2000-v2/user-components/foo_ui_panels/foo_ui_panels.component`, start
foobar2000 and add the **Panels UI** layout element (see [Installation](#installation)).
A bundle copied from another machine has to be re-signed ad hoc
(`codesign --sign - --force --deep <bundle>`), or macOS kills it on load.

### Cross-platform testing in VMs (Docker)

Most development happens on Linux, so both real platforms run as VMs in containers on the
same box, driven by helper scripts from the maintainer's sibling repos (`../macos-devbox`
→ `mvm`, `../windows-devbox` → `wvm`; point at other copies with `MVM=` / `WVM=`):

| VM | Based on | Used for |
|---|---|---|
| **Windows 11** | [dockur/windows](https://github.com/dockur/windows), unattended install | what Wine fakes or lacks: Dark Mode, DPI scaling, native theming, GDI+ text rendering |
| **macOS 14 Sonoma** | [dockur/macos](https://github.com/dockur/macos) | the macOS bundle on a real Cocoa/CoreText stack |

```sh
# Windows 11
make win11                     # boot, open the VM screen (noVNC) in the browser, then win11-test
make win11-test                # build, deploy the DLL + skins/$SKIN into the VM, relaunch, screenshot
make win11-skin SKIN=fooava    # re-copy a skin (into the component folder, Panels UI's default)
make win11-theme ARGS=dark     # Windows app theme; win11-dpi ARGS=144 for 150% scaling

# macOS
make mac-vm                    # boot, open the VM screen (noVNC) in the browser, then mac-vm-test
make mac-vm-test               # mac-build, deploy the bundle into the VM, relaunch foobar2000, screenshot
make mac-vm-skin SKIN=fooava   # copy a skin from skins/ into the guest
make mac-vm-navidrome          # install foo_navidrome there (latest release) ...
make mac-vm-navidrome-config   # ... and copy its server settings from the local Wine profile
```

In the Windows guest, pick **Panels UI (reborn)** as the user interface module once and then
`make win11-snapshot`. `make win11-<cmd>` / `make mac-vm-<cmd>` pass through to `wvm` / `mvm`:
`win11-shot`, `mac-vm-click ARGS='x y'`, `mac-vm-snapshot ARGS=name`, and so on. Each disk sits
on a host bind mount with reflink snapshots, so a broken guest is one restore away. No VM? Any
real Windows or Mac machine with foobar2000 v2 works the same way.

### Driving the UI from scripts, and letting an LLM agent do it

`scripts/ui-test.sh` drives the Panels UI running in the local Wine foobar2000 without
touching your mouse or keyboard: `tools/wclick.c` runs inside Wine and posts input straight
to the window (Hyprland host, screenshots via `grim`).

```sh
make ui ARGS='restart'                     # graceful close, install build/ DLL, relaunch + play
make ui ARGS='click X Y r'                 # also dbl, wheelN, key:VK, type:TEXT, bmclick, …
make ui ARGS='shot build/ui-test/after.png'
make ui ARGS='pvars key=value'             # edit persisted panel vars (foobar2000 stopped)
```

The layout wizard's previews (`res/wizard/*.png`, built into the component) are screenshots of
each template taken by `scripts/wizard-previews.sh`: it runs them in a throwaway portable
foobar2000 with its own Wine prefix and profile, playing a made-up library of synthesized tracks
and generated covers (`scripts/wizard-demo-library.sh`) — rerun it after changing a template.

Together with `make win11-test` / `win11-shot` and `make mac-vm-test` / `mac-vm-shot` in the VMs, a change can be checked
end to end with nothing but commands. A coding agent (the maintainer uses [Claude
Code](https://claude.com/claude-code)) runs the same loop: build, click through the skin,
take a screenshot and compare it with the expected render, on Wine, real Windows and macOS.
[`CLAUDE.md`](CLAUDE.md) holds the project rules for such an agent. The golden render tests
(`make test`) catch drawing regressions without foobar2000 at all.

The skin engine and every panel are platform-free C++ (`src/core`, `src/panels`) drawing through
a small canvas/view interface (`src/gfx`, `src/ui`); each platform supplies that layer
(`src/platform/win`: Win32 + GDI/GDI+, `src/platform/mac`: Cocoa + CoreGraphics/CoreText).

**Windows** — cross-compiled on Linux with `clang-cl` + `lld-link` against the Windows SDK/CRT
from [xwin](https://github.com/Jake-Shadle/xwin) — no Visual Studio needed.

```sh
# prerequisites (Arch shown): clang, lld, llvm, cmake, ninja, and xwin
sudo pacman -S --needed clang lld llvm cmake ninja
cargo install xwin && xwin --accept-license splat --output ~/.xwin

./build.sh          # -> build/foo_ui_panels.dll
```

The foobar2000 SDK is **not** vendored. It's expected next to the checkout (the same layout
[foo_navidrome](https://github.com/santiagorod92/foo_navidrome) uses, so both can share one
copy), and `build.sh` fetches the latest
[official SDK](https://github.com/reupen/foobar2000-sdk-unmodified) there if it's missing:

```
<parent>/
  foobar2000/   SDK, foobar2000_component_client, shared, ...
  pfc/
  foo_ui_panels/   <- this repo
```

`make run` builds, installs into a local foobar2000 (Wine) and relaunches it —
see [`scripts/deploy.sh`](scripts/deploy.sh).

**macOS** — `./scripts/mac-build.sh` (or `make mac-build`) builds the universal
`build-mac/foo_ui_panels.component`: natively on a Mac (CMake + Ninja + Xcode command line
tools), or cross-compiled on Linux with `clang` + `ld64.lld` against a `MacOSX.sdk` extracted
from an Xcode `.xip` ([`scripts/extract-macos-sdk.py`](scripts/extract-macos-sdk.py), default
location `~/.macos-sdk/MacOSX.sdk`).

Maintainers can also runtime-test the macOS build without a Mac. `make mac-vm-test` deploys it
into a local macOS VM ([dockur/macos](https://github.com/dockur/macos) on Linux/KVM, driven by
an `mvm` helper script; set its path with `MVM=`) and relaunches foobar2000 there.
`make mac-vm-skin` copies a skin from `skins/` into that VM, `make mac-vm-navidrome` installs
[foo_navidrome](https://github.com/santiagorod92/foo_navidrome) there (latest release, or
`NAVIDROME=<tag or .fb2k-component>`) and `make mac-vm-navidrome-config` copies its settings
(server, login, custom headers) from the local Wine foobar2000.

## Releases

Releases are automated with [semantic-release](https://github.com/semantic-release/semantic-release)
from [Conventional Commits](https://www.conventionalcommits.org/): every push to `main` with a
`feat:`/`fix:`/`perf:`/`refactor:` commit cuts a new semver release: a `v<version>` tag and a
GitHub release whose notes are the changelog, with the `.fb2k-component` attached, which is then
published to foobar2000.org. (`CHANGELOG.md` holds the history up to 1.3.0; later releases are
on the [Releases](../../releases) page.) Changes reach `main` through pull requests.

## License

The component's source code is released under the [MIT License](LICENSE).
fooAvA and its artwork belong to dawxxx666, who licenses them under CC BY-NC-SA 3.0.
