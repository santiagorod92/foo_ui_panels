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
- **Text drawing: always `DrawTextW`** (via the `dtW` UTF-8→UTF-16 helper in skin_engine.cpp / playlist_view.cpp).
  foobar strings (titleformat output, `format_title`, tags) are UTF-8; `DrawTextA` mojibakes non-ASCII
  (`’`→`â€™`, accents). Never draw foobar text with the A variant.
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
  `$font` face gotcha: the skin uses `$font($get(fontAVA),…)`, but `$get` reads `$puts` vars (not our
  pvars), so the face arrives EMPTY → GDI would pick a big default System font; `select_font` falls back
  to "Tahoma" for a blank face (compact white now-playing text, close to the skin's Swis721 Cn BT).
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
  (`src/volume.{h,cpp}` — drag slider, set_volume dB −100..0), `"Single Column Playlist"`/`"ELPlaylist"`
  to native `PlaylistView` (`src/playlist_view.{h,cpp}`), `"Channel spectrum panel"`/`Spectrum` to native
  the spectrum (EQ bars under the cover): renderer prefers a real DUI spectrum component if installed
  (PanelHost substring-matches `"Spectrum"` → hosts e.g. **foo_vis_spectrum_analyzer**), else falls back to
  native `Spectrum` (`src/spectrum.{h,cpp}` — `visualisation_manager`/`visualisation_stream` FFT-512 →
  themed mirrored log-spaced bars, ~25fps). Others to `PanelHost`. (CoverFlow/"vinyl-spines" is
  foo_chronflow — dead, not replicable; the cover-case spine `$button2` cycles `mini.panels` =
  cover/mini-playlist/track-info instead.)
  `PlaylistView`: the skin's playlist is `foo_uie_elplaylist` (a Columns-UI panel — NOT hostable in a
  DUI replacement), so we self-draw the active playlist grouped by album (consecutive `%album artist%|%album%`):
  per-group header (cover thumb from `$replace(%path%,%filename_ext%,*folder*.*)` + album artist/album/date/genre/
  track-count) then track rows (`$num(%tracknumber%,2). %title%` + rating `rating_stars24/{0-5}s1.png` +
  `%length%`). Background = the skin wallpaper (pvar `background` when `backgroundd`) under a translucent
  dark overlay (`image.cpp fill_alpha`, alpha ~185) so it matches the main window yet stays readable;
  navy `fill_gradient_v` fallback. Row highlight colour is tinted to the wallpaper's dominant colour
  (`image_avg_color` → 1×1 downscale, ×3 brightened) so it harmonises instead of a stark blue: now-playing
  = gradient bar, click-selected (`activeplaylist_is_item_selected`) = translucent band + brighter underline.
  Wheel scroll. Selection: click=single, Shift+click=range (from `m_anchor`), Ctrl+click=toggle
  (`activeplaylist_set_selection_single`/`set_selection`+`bit_array_range`); dbl-click =
  `activeplaylist_execute_default_action`. Right-click = the classic foobar context menu via
  `contextmenu_manager::win32_run_menu_context(hwnd, selected, &pt, ...)` (Properties / tagging / etc.;
  selects the row first if it's outside the selection). Clicking the star column (x in `[w-106, w-51)`)
  sets that row's rating via `SkinEngine::set_rating` (writes RATING tag, metadb_io_v2); WM_MOUSEMOVE over
  the star column live-previews the star count (`m_hover_row`/`m_hover_stars`, TrackMouseEvent→WM_MOUSELEAVE
  clears it) before the click commits. Re-reads playlist each paint (1s). The seek/volume
  bars draw a solid dark groove (RGB 12,12,14 — must NOT blit the parent: WS_CLIPCHILDREN means the
  parent never paints behind the child, so blitting reads the white window bg brush) with a THIN
  (~4px, vertically centred) themed fill =
  `bar/v{colour.b}.png` (a ~3px coloured gloss strip; volume adds the `bar/vol{colour.b}.png` round knob).
  Fallback `image.cpp fill_gradient_v` (theme colour, light top → dark bottom) if the image is missing.
  NOTE: don't fill the FULL panel height (looks too thick / misaligned with the volume knob), and don't use
  `progress*.png`/`tb*.png` — those are near-white gloss/ticked overlays, NOT a colour source. The master's
  own volume draw uses `%panel_volume%`/`%cwb_volume%` (foo_cwb_hooks, absent) so we must draw natively.

## fooAvA config format (s8.bin) — decoded
`skins/fooava/config/s8.bin` layout: `[56-byte header][53 pvar KV pairs][length-prefixed script blocks]`.
- pvar KV: `<u32 nameLen><name><u32 valLen><val>` repeated (the setup defaults).
- script blocks: `<u32 len><text>`. block 0 = font/cover init; block 1 (112KB) is a nested binary
  panel-config blob whose **master layout script is a contiguous 40376-byte text run at offset ~161**
  (starts with the fooAvA header comment, ends `//////////END///////////`; 75 `$panel` + 79 draws).
- Extracted master → shipped as `fooava.txt` next to the DLL; `main` reads it (fallback: built-in test skin).
  Re-extract via the python parsers in /tmp (parse_pss*.py) if needed.
- Assets RECOVERED (was a gap): button PNGs (`images/fooAVA/*.png`, cover-case art, side-tab icons —
  ~365 files) were NOT in the bzip2 SFX streams. They live in the original DeviantArt distribution
  **.exe** (a **ClickTeam Install Creator** SFX — "clickteam.com" in the PE head). `tools/recover_fooava_images.py
  <fooava_1_05_*.exe> <skin_root>` extracts them via a proper ClickTeam parser (ported from
  Bioruebe/cicdec, installer **version 30**): find the overlay signature `77 77 67 54 29 48`, walk
  blocks (`id u16`+skip2+`size u32`), decompress the **FILE_LIST** (0x143A) node table, and for each
  file read its exact **offset** into **FILE_DATA** (0x7F7F) — body at `dataStart+offset+4` =
  `[method byte][zlib 78 da deflate | BZh | raw]`. Mapping by offset is EXACT. (An earlier size-collision
  heuristic mismapped same-size images — wrong colours/dims, e.g. `playlist1` 65px, `reflet300` 20×20;
  do NOT reintroduce it.) Engine loads PNGs by path → renders with no code change. Variants: `prevN.png`
  N=1 black default / 2 blue / 3 red / 4 green; `$button` draws normal (img1=black), themed = hover (img2,
  not drawn). Staged copy: `skins/fooava/images/fooAVA/` (script's lowercase `s\` resolves to the `S/`
  dir case-insensitively under Wine).
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
- `$button`/`$button2`: render only the NORMAL state (param 6); param 7 (hover) is ignored so themed
  and base variants don't stack. For `$button` param 6 is an image path → `draw_image`. For `$button2`
  it's a draw *command* (often `'`-quoted `$imageabs2(...)`, coords relative to the button) → run via
  `run_subscript` with the draw origin `m_ox/m_oy` set to the button (x,y); a resolved path draws directly;
  else nothing (a button whose image is missing stays invisible but clickable — no empty border).
  Compiled commands cached in `m_subcache`. `$textbutton(l,t,w,h,strN,strH,action,…)` draws the evaluated
  normal text in the box + records a click region (renders artist/album track-info). `$calcwidth(text)`
  → pixel width via `GetTextExtentPoint32A` (used to center text). `$imagebutton(l,t,imgN,imgH,action,…)`
  draws the image natural-size + ~11×15 hit box (rating stars).
- Action dispatch lives in `SkinEngine::run_button_action(action)`. Transport handled FIRST via
  `playback_control` (Previous/Next/Stop/Play/Pause → previous/next/stop/play_or_pause; `Playback/Random`
  → `start(track_command_rand)`) — NOT by main-menu leaf name, because the leaf "Random" is ambiguous
  (matches both Playback/Random and the Random playback ORDER). A bare action equal to a playback-order
  name (Default, Repeat (track), Repeat (playlist), Shuffle (tracks)…) → `playback_order_set_active`
  (the order button cycles the real order; `%cwb_playback_order%` field resolves from the active order so
  the repeat/shuffle icons + label reflect it). `TAG:SET:field:value` → writes the tag on the now-playing
  track via `metadb_io_v2::update_info_async` + a `file_info_filter` (`meta_set_filter`); `rating`→`RATING`,
  value 0 clears (the star `$imagebutton`s). NOTE: don't add a `%rating%` hook field — it shadowed the
  rating provider (foo_playcount) and made the stars vanish; let the provider supply `%rating%`. Then:
  `PVAR:SET:key:value` → set pvar +
  `save_pvars` + `repaint_all()` (invalidate canvas + every panel so a mode/theme change shows
  everywhere); `WINDOWSIZE:w:h[:halign:valign]` → `SetWindowPos` top-level; `POPUP:<file.ava>` → open that
  PanelsUI script in a floating `Popup` window (`src/popup.{h,cpp}`; reads `panels/<file.ava>.txt`);
  `play`/`pause` → `play_or_pause`; `MENUBAR:toggle` → flip pvar `menubar` + post `PUI_WM_TOGGLE_MENU`
  to the top-level window (main.cpp `SetMenu`s the bar on/off; restores persisted state at startup via
  `pvar_int("menubar",1)`; the settings popup has a "SHOW MENU BAR" checkbox). Else
  `run_action()` matches the leaf (e.g. "Playback/Random"→"Random") against `mainmenu_commands`. Values
  may be `'`-quoted (`unquote`).
- **Click routing**: the main window's `handle_click` hit-tests `m_buttons` (master-script buttons). But
  buttons drawn *inside* a native panel (Display's cover-case border `mini.panels` switch, play/pause,
  rating) are recorded in the panel's OWN coordinate space: `draw_script(...,capture)` sets `m_capture`
  so `$button*` push into the panel's vector; `TrackDisplay::on_click` (WM_LBUTTONDOWN) hit-tests that and
  calls `run_button_action`. `Button` is now in its own `button.h` (breaks the skin_engine↔track_display
  include cycle).
- **Theme colour**: paths are dynamic — `prev$getpvar(colour.b).png` resolves at arg-eval time. The master
  runs `$select($getpvar(set.colour),…)` → sets `colour.b` (1-4 image variant) + `colour` (r-g-b for
  glow/bars). Native Seekbar/Volume read `SkinEngine::theme_color()` (pvar `colour`) so bars match buttons.
  The theme is chosen in the **settings popup** (gear button, bottom-right → `POPUP:FOOAvA_settings.ava`):
  three swatches do `PVAR:SET:set.colour:1|2|3` (blue/red/green) + other toggles (coverflow, hide-titlebar,
  album-art, lyrics…). Settings script extracted from the .exe's `FOOAvA_settings.ava` config (a `04 00 00`
  PanelsUI blob; its master is the longest text run — same extraction as s8.bin), preprocessed and shipped
  as `panels/FOOAvA_settings.ava.txt`.
- `fooava.txt` = pvar defaults + master. (block_00 init is NOT prepended — its deep-nested cover
  logic breaks titleformat → 0 placements.) Instead `read_panel_script()` prepends a simple
  `$setpvar(MyCoverPath,$replace(%path%,%filename_ext%,*folder*.*))` so the Display panel finds the
  cover; the wildcard image loader resolves `*folder*.*` → Folder.jpg/png.
- **Positional literal text**: PanelsUI draws titleformat literal text (between functions); standard
  `run()` only returns it. `DrawString : pfc::string_base` accumulates all written text into one buffer;
  `SkinHook` reads a SLICE of that buffer `[m_flushFrom, end)` at each flush (control chars from `$char(N)`
  dropped). `$alignabs(left,top,right,bottom,halign,valign)` sets the box+flags and flushes the prior box;
  `$textcolor`/`$set_font_color` set the colour; flushed on next `$alignabs`/`$drawstring` and via `finish()`
  after run (NOT the destructor — the DrawString is destroyed first). CRITICAL: read from the live buffer,
  do NOT keep a separate appended pending — titleformat appends a `$if`/`$ifequal` *condition's* value to the
  buffer then truncates it; reading the post-truncate buffer is why `%_isplaying%`→"1" no longer leaks as a
  "1" before the title. PanelsUI underscore fields handled: `%_width%`
  `%_height%` `%el_width%` `%el_height%` `%_isplaying%` `%_ispaused%` `%foobar_path%`.
- `$imageabs2` opts parsed (`parse_img_opts`): `alpha-N` and `ROTATEFLIP-N` (6 = vertical mirror,
  used for cover reflections; drawn via GDI+ destination parallelogram, cache not mutated).
- Image loader resolves **wildcard paths** (`image.cpp resolve_wildcard`, FindFirstFile): `*folder*.jpg`
  → `Folder.jpg`. Possible future fallback: broad `*.jpg`/`*.png` if specific patterns miss.
- Main window needs **WS_CLIPCHILDREN** or the double-buffered WM_PAINT BitBlt paints over hosted panels.
- Color spec `brushcolor-null` / `pencolor-null` = transparent (no fill/frame). `$drawrect` honours
  `alpha-N` (semi-transparent fill via `fill_alpha`/AlphaBlend, msimg32) — needed or the settings panel's
  `alpha-60` black overlays paint solid and hide everything.
  (Assets recovered — see `tools/recover_fooava_images.py`, staged at `skins/fooava/images/fooAVA/`,
  deployed to `<dll dir>/images/fooAVA/`. Wine resolves the script's mixed-case paths case-insensitively.)

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
  `execute_command(id-base)`. `WM_INITMENUPOPUP` → `refresh_popup()` re-generates the opening top-level
  popup (fresh `mainmenu_manager`) so radios/checks reflect live state (e.g. Playback›Order after the
  order button changes it) — the menu bar is built once, so without this the checks only updated on restart.

### Cross-build gotchas (all handled, don't re-discover)
- **Static release CRT only**: xwin ships no debug CRT → toolchain forces `/MT` (`CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded`, Release). Debug build → `msvcrtd.lib` not found.
- **Header case-sensitivity**: SDK `#include`s cased headers (`<SDKDDKVer.h>` etc); xwin lowercases. `build.sh` symlinks cased names into `~/.xwin`.
- **Include order in main.cpp**: `WIN32_LEAN_AND_MEAN` + `<winsock2.h>` before `<windows.h>` (WS1/WS2 clash), then `<objbase.h>` (COM `interface` macro) + `<mmsystem.h>` (`timeGetTime` for pfc/timers.h).
- **Link `shared-x64.lib`**: import lib for fb2k `shared.dll` (provides `uBugCheck`, `stricmp_utf8`, `uPrintfV`…). At `sdk/foobar2000/shared/`.
- libPPUI (rich list controls) deferred to Phase 2+ — needs WTL+ATL, not in xwin.
