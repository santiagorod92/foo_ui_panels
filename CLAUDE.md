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
- **Don't push per-step.** Iterate locally; push only at the end of an iteration round or on request.
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
  `$panel` (records rects), `$eval` (integer arithmetic; **`{}` are grouping like parens**, needed for
  cover-scaling math), `$getpvar/$setpvar`; draw
  funcs stubbed. `layout(w,h)` re-evaluates on resize → creates/positions hosted panels.
  Legacy type→DUI mapping in `map_type()` (see FORMAT.md). **This is the core architecture** —
  Panels UI is a scripted absolute-positioning canvas, not a splitter tree (splitter.cpp now unused).
  Drawing is immediate-mode: the script runs in the canvas **WM_PAINT** (double-buffered),
  `SkinEngine::render(dc,w,h)` executes draw funcs against the DC then positions hosted panels.
  Implemented draw funcs: `$drawrect` (brushcolor-/pencolor- spec), `$gradientrect`, `$drawroundrect`,
  `$font`, `$drawstring`. Colors are `r-g-b[-a]` (alpha ignored, GDI). Links `msimg32` (GradientFill).
  `$imageabs(x,y,w,h,path,align)` and `$imageabs2(maxW,maxH,imgW,imgH,srcX,srcY,dstX,dstY,path,opts)`
  draw via `src/image.{h,cpp}` (GDI+, links `gdiplus`; bitmaps cached by path since paint runs per frame;
  `alpha-N` opt honored). Paths resolved against skin base dir (`set_base_dir`, currently the DLL dir);
  `\`→`/`, leading `./`/`/` stripped. `images_shutdown()` called on component shutdown.
  Native `Track Display` panel: `src/track_display.{h,cpp}` — own child window, repaints (1s timer)
  by running a per-panel titleformat script via `SkinEngine::draw_script(dc,w,h,script,nowplaying)`
  (built-in fields resolve from the now-playing track). Transparent bg = blit parent behind it.
  Gotcha: `draw_script` uses `playback_control::playback_format_title(...display_level_all)` (not
  `metadb_handle::format_title`) so dynamic fields like `%playback_time%`/`%isplaying%` resolve.
  `render()` routes `$panel(...,"Track Display",...)` to a `TrackDisplay`, `"Seek Panel"` to a native
  `Seekbar` (`src/seekbar.{h,cpp}` — progress bar + click-to-seek), `"Volume Panel"` to native `Volume`
  (`src/volume.{h,cpp}` — drag slider, set_volume dB −100..0), others to `PanelHost`. Native widgets self-drawn, no assets.

## fooAvA config format (s8.bin) — decoded
`skins/fooava/config/s8.bin` layout: `[56-byte header][53 pvar KV pairs][length-prefixed script blocks]`.
- pvar KV: `<u32 nameLen><name><u32 valLen><val>` repeated (the setup defaults).
- script blocks: `<u32 len><text>`. block 0 = font/cover init; block 1 (112KB) is a nested binary
  panel-config blob whose **master layout script is a contiguous 40376-byte text run at offset ~161**
  (starts with the fooAvA header comment, ends `//////////END///////////`; 75 `$panel` + 79 draws).
- Extracted master → shipped as `fooava.txt` next to the DLL; `main` reads it (fallback: built-in test skin).
  Re-extract via the python parsers in /tmp (parse_pss*.py) if needed.
- Asset gap: button PNGs (`/images/fooAVA/*.png`) were NOT in the SFX streams — images won't load yet.
- Wallpaper: the 6 extracted JPEGs (SFX streams s9–s14) are the `walls`. Place at
  `<dll dir>/images/fooAVA/walls/1.jpg..6.jpg`. The master draws it only when pvar `backgroundd=1`
  (default 0 = solid black) — fooava.txt is patched to seed `backgroundd=1`. Alpha from `alpha.bgr` (195).

## Loading a real PanelsUI script into fb2k titleformat — required preprocessing
fb2k titleformat won't run the raw script. `tools/extract_fooava.py <s8.bin> <fooava.txt>` does, IN ORDER:
1. Strip `//` comments (full-line + trailing) and per-line leading/trailing whitespace; join lines.
2. **Balance parens** — fooAvA has an unmatched `)` (PanelsUI auto-closed; fb2k doesn't). Drop stray
   closers (depth<0) and append missing closers. Without this, titleformat compiles but executes
   nothing (0 placements, 0 output).
3. Prepend the 53 pvar defaults as `$setpvar(k,v)` (panels are gated behind `$ifequal($getpvar...)`).
Result installed as `fooava.txt`. Default mode yields ~6 of 75 panels (rest gated by mode pvars).
- **Per-panel scripts**: after the master, block_01 holds `<u32 nameLen><name><meta><script>` records
  (raw bytes of s8.bin — NOT block_01.txt, which is UTF-8-mangled). Extracted (preprocessed) to
  `panels/<name>.txt` next to the DLL (emitted by `tools/extract_fooava.py` alongside fooava.txt).
  Native `TrackDisplay` loads `panels/<placement-name>.txt`
  (e.g. "Display" = album-art cover display) instead of its default. `read_panel_script()` in SkinEngine.
- Draw funcs now real (not stubbed): `$draw_image`, `$fileexists` (GetFileAttributes), `$greater`.
- `$button`/`$button2`: record clickable rect + action; `SkinEngine::handle_click` (from main WM_LBUTTONDOWN)
  runs the action via `run_action()` — matches the leaf name (e.g. "Playback/Random"→"Random") against
  registered `mainmenu_commands` and executes. Icon PNGs missing → faint frame marker drawn so buttons
  are visible/clickable.
- `fooava.txt` = pvar defaults + master. (block_00 init is NOT prepended — its deep-nested cover
  logic breaks titleformat → 0 placements.) Instead `read_panel_script()` prepends a simple
  `$setpvar(MyCoverPath,$replace(%path%,%filename_ext%,*folder*.*))` so the Display panel finds the
  cover; the wildcard image loader resolves `*folder*.*` → Folder.jpg/png.
- **Positional literal text**: PanelsUI draws titleformat literal text (between functions); standard
  `run()` only returns it. `DrawString : pfc::string_base` routes each written chunk to `SkinHook::emit_text`;
  `$alignabs(left,top,right,bottom,halign,valign)` sets the box+flags and flushes; `$textcolor`/`$set_font_color`
  set the color; flushed on next `$alignabs`/`$drawstring`/end. This renders the bottom track-info text.
- `$imageabs2` opts parsed (`parse_img_opts`): `alpha-N` and `ROTATEFLIP-N` (6 = vertical mirror,
  used for cover reflections; drawn via GDI+ destination parallelogram, cache not mutated).
- Image loader resolves **wildcard paths** (`image.cpp resolve_wildcard`, FindFirstFile): `*folder*.jpg`
  → `Folder.jpg`. Possible future fallback: broad `*.jpg`/`*.png` if specific patterns miss.
- Main window needs **WS_CLIPCHILDREN** or the double-buffered WM_PAINT BitBlt paints over hosted panels.
- Color spec `brushcolor-null` / `pencolor-null` = transparent (no fill/frame).
  Asset gap: fooAvA's button PNGs were NOT in the extracted SFX streams (only 6 JPEGs) — needs recovery.

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
