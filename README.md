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

The component ships no skin of its own. fooAvA is distributed by its author (link above); the
scripts in [`tools/`](tools/) recover its images and scripts from the original installer
(`recover_fooava_images.py`) and convert its Panels UI configuration into the scripts this
component loads (`extract_fooava.py`, which also writes fooAvA's `foo_ui_panels.ini`).

### Skin configuration

The component is skin-agnostic: everything a particular skin needs beyond its scripts lives in
an optional `foo_ui_panels.ini` in the skin folder (`key = value` lines, `#` comments). Without
it, a skin gets neutral defaults. Edits are picked up live, like script edits.

| Key | Meaning (default) |
|---|---|
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
  instrumental tracks shown as such.
- Right-click **Edit code…** on a panel, with **Apply** to reload its script live — or edit
  the skin's scripts or `foo_ui_panels.ini` in any editor: saved changes are picked up within a
  second.
- Skin diagnostics in **View › Console**: functions the skin uses that aren't supported (yet),
  missing skin images, and panels no installed element can host.
- The player window reopens where you left it, at the same size.
- Button tooltips and the skin's own window title (`$settitle`, e.g. "artist - title" while
  playing).
- The skin's tray icon (`$settray`): on Windows a notification-area icon — click to show/hide the
  player, right-click for play/pause, stop, previous/next, show/hide and exit — and minimising
  sends the player to the tray; on macOS the same menu from a menu-bar icon.
- GDI+-style outlines (`$gp_set_pen` + `$gp_draw_rectangle`) next to the existing brush fills.
- **macOS support**: the same engine and panels run on macOS (Cocoa + CoreGraphics/CoreText),
  shipped in the same `.fb2k-component` as the Windows build, with its own Preferences page
  (skin folder, active skin, main script, accent colour) and full-resolution Retina rendering.
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

- A step-by-step guide for setting up fooAvA from its original download.

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
  supported.

**Pull requests.** Fixes and new features are welcome. A few conventions keep things smooth:

- **Every change is cross-platform.** Logic lives in platform-free C++ (`src/core`,
  `src/panels`); anything that needs the OS goes through `src/gfx/canvas.h` / `src/ui/view.h`
  and is implemented for both `src/platform/win` and `src/platform/mac`. `make check-portable`
  (also run in CI) verifies the core compiles without Windows headers.
- Both builds should pass: `./build.sh` (Windows) and `./scripts/mac-build.sh` (macOS) — CI
  checks both on every PR.
- `make test` runs the unit tests (`tests/`, host compiler, under ASan/UBSan — also in CI). They
  cover the parts of the engine that don't need foobar2000: script/config parsing, `$eval`, menu
  action matching, LRC/lrclib parsing, playlist reordering. Add a case when you touch those.
- Use [Conventional Commits](https://www.conventionalcommits.org/) (`feat:`, `fix:`, …): they
  drive the automated releases (see [Releases](#releases)).
- Update this README when you add or change a feature (e.g. *What's implemented*, *Roadmap*),
  so the docs stay in sync with the code.

Not sure where to start, or want to discuss an idea first? Open an issue.

## Building from source

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
`feat:`/`fix:`/`perf:`/`refactor:` commit cuts a new semver release, updates
[`CHANGELOG.md`](CHANGELOG.md) and attaches the `.fb2k-component` to the GitHub release, which
is then published to foobar2000.org.

## License

The component's source code is released under the [MIT License](LICENSE).
fooAvA and its artwork belong to dawxxx666, who licenses them under CC BY-NC-SA 3.0.
