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
(universal, macOS 11+). **macOS support is experimental** — see [Roadmap](#roadmap).

1. Download `foo_ui_panels_<version>.fb2k-component` from the [Releases](../../releases) page.
2. Double-click it (or drag it onto foobar2000) and restart foobar2000.

**Windows**

3. Pick **Panels UI (reborn)** as the *User interface module* in **Preferences › Display**.
4. Point the component at a skin: **Preferences › Display › Panels UI (reborn)** → skin folder.

**macOS** — Panels UI is a layout element there, not a separate UI module:

3. **View › Layout › Edit Layout**, add **Panels UI** and let it fill the window.
4. Put the skin (its `fooava.txt`, `panels/`, `images/`) in
   `~/Library/foobar2000-v2/foo_ui_panels/`.

The component ships no skin of its own. fooAvA is distributed by its author (link above); the
scripts in [`tools/`](tools/) recover its images and scripts from the original installer
(`recover_fooava_images.py`) and convert its Panels UI configuration into the scripts this
component loads (`extract_fooava.py`).

## What's implemented

- Full foobar2000 UI replacement on Windows (`user_interface` service, same slot as the Default
  UI or Columns UI); a full-window layout element on macOS.
- The Panels UI script engine: titleformat-driven canvas with `$panel`, `$button`/`$button2`,
  `$imageabs2`, `$drawrect`, `$font` (incl. glow), `$alignabs`, persistent `$setpvar`/`$getpvar`
  variables and the skin's button actions (transport, playback order, window size, popups…).
- Native versions of the panels fooAvA uses: track display (CD case, rating stars), seek bar,
  volume, grouped playlist, spectrum analyser, album browser + cover flow, playlist switcher,
  quick search and a synced-lyrics panel.
- Right-click **Edit code…** on a panel, with **Apply** to reload its script live.

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

- **macOS support** — builds and ships, but hasn't been runtime-tested on a Mac yet. Known gaps:
  no Preferences page (skin folder is fixed, see Installation), 1x rendering on Retina
  displays, and panels that host foobar2000's own UI elements only if a Mac element of the same
  name exists.
- A step-by-step guide for setting up fooAvA from its original download.

Feedback, bug reports and ideas are very welcome in [Issues](../../issues).

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

## Releases

Releases are automated with [semantic-release](https://github.com/semantic-release/semantic-release)
from [Conventional Commits](https://www.conventionalcommits.org/): every push to `main` with a
`feat:`/`fix:`/`perf:`/`refactor:` commit cuts a new semver release, updates
[`CHANGELOG.md`](CHANGELOG.md) and attaches the `.fb2k-component` to the GitHub release, which
is then published to foobar2000.org.

## License

The component's source code is released under the [MIT License](LICENSE).
fooAvA and its artwork belong to dawxxx666, who licenses them under CC BY-NC-SA 3.0.
