# Panels UI skin/config format — recovered spec

Reverse-engineered from the **fooAvA 1.05** config (`skins/fooava/config/s8_skin.txt`,
`s4_titleformat.txt`) and the original `foo_ui_panels.dll` binaries. This is the format the
skin engine (`src/core/skin_engine.cpp`) implements for compatibility.

## Layout model — IMPORTANT correction
Panels UI does **not** use a static splitter tree. The skin is a script re-evaluated on
every resize; it positions panels by **absolute computed rects**:
`$panel(name, type, x, y, w, h)` where x/y/w/h derive from `%_width%`/`%_height%` via `$eval`,
with show/hide driven by pvars (e.g. `$ifequal($getpvar(onepanel),1, ...)`). So the core engine
is a **titleformat interpreter** that each pass (a) collects `$panel` placements → positions
hosted windows, and (b) runs draw funcs (`$drawrect`/`$font`/`$imageabs2`/`$button`).

## fooAvA panel types → implementation
Legacy CUI/PSS panel names used by fooAvA (2nd arg of `$panel`, usage count in parentheses) and
what renders them (`SkinEngine`'s panel-kind dispatch; `map_type()` for hosted elements):
- `Channel spectrum panel` (38) → **native** spectrum (`src/panels/spectrum`)
- `Track Display` (10) → **native** track display
- `Single Column Playlist` (7), `ELPlaylist` (1) → **native** grouped playlist
- `Lyric Show` (4) → **native** lyrics panel
- `Playlist switcher` (3) → **native** playlist/library tree
- `Album list` (3), `Graphical Browser` (3), `Chronflow` (1) → **native** album browser / cover flow
- `Seek Panel` (1), `Volume Panel` (1) → **native** seek bar / volume
- `Quick Search Toolbar` (1) → **native** quick search
- `Peakmeter` (3) → **native** peak meter
- `Album Art` (1) → **native** album art viewer
- anything else → hosted DUI element looked up by that name

## Model
A skin is a **titleformat-style script** evaluated continuously. The script lays out child
panels and draws directly. Heritage: Panel Stack Splitter (PSS) scripting (`SCPL*`, `$scplsetlayout`).

- Coordinate fields: `%_width%`, `%_height%` (current panel size). Math via `$eval({%_width%}-5)`.
- **Persistent variables (pvars)**: `$setpvar(name,val)` / `$getpvar(name)` survive across
  evaluations and sessions — back the "setup panel" (e.g. `onepanel`, `MyFont`, `CoverMode`).
  `$puts`/`$get` are per-evaluation locals.

## Function vocabulary (by usage count in fooAvA — implement in priority order)

### Layout / hosting
- `$panel(name, uie_type, x, y, w, h, ...)` (81) — host another component's UI element
  (`uie_type` = registered panel name, e.g. "Channel spectrum panel", "Seek Panel", "Track Display").
- `$alignabs(...)` (71), `$calcwidth(...)` (49), `$scplsetlayout(...)` (11), `$windowstyle(...)` (2).

### Drawing (GDI + GDI+)
- `$drawrect` (54), `$drawstring` (32), `$drawroundrect` (16), `$gradientrect` (4)
- `$imageabs2` (112), `$imageabs` (4), `$draw_image` (12), `$draw_text` (5)
- `$font` (161), `$set_font_color` (5), `$textcolor`, `$offset_colour` (9), `$calculate_blend_target` (6)
- GDI+: `$gp_set_brush` (4), `$gp_set_pen` (3), `$gp_fill_rectangle` (3)

### Controls
- `$button` (53), `$button2` (43), `$imagebutton` (15), `$textbutton` (3)

### State / eval
- `$get` (608), `$eval` (308), `$getpvar` (279), `$puts` (124), `$setpvar` (44)

### Standard titleformat (reuse fb2k titleformat where possible)
- `$sub` (283), `$if` (153), `$ifequal` (146), `$upper` (141), `$replace` (59),
  `$ifgreater` (55), `$div` (51), `$char` (45), `$add` (23), `$select` (18), `$strcmp` (16),
  `$mod` (12), `$fileexists` (12), `$stricmp` (11), `$strstr` (10), `$num` (7), `$not` (6),
  `$if2` (6), `$cwb_fileexists` (6), `$substr` (5), `$or` (5), `$mul` (5), `$meta` (5),
  `$and` (4), `$trim` (2), `$strrchr` (2), `$sortidx` (2), `$playlist_active` (2), `$len2` (2)

## fooAvA assets (in skins/fooava/, gitignored — proprietary DLLs / CC-BY-NC-SA art)
- `dlls/foo_ui_panels__s3.dll`, `__s5.dll` — **original Panels UI binaries** (RE reference).
- `dlls/foo_ui_columns__s2.dll`, `unknown__s6.dll`, `unknown__s7.dll` (likely foo_chronflow + dep).
- `images/img_s9..s14.jpg` — skin wallpapers / cover backgrounds (800x600, 1024x768).
- `config/s8_skin.txt` — main fooAvA layout+draw script. `config/s4_titleformat.txt` — TF scripts.
- `raw/s1.bin` — bitmap/gradient data.

Note: skin references hardcoded author paths (`G:\Dawid\Muza\...`, `F:\Program Files\foobar2000_VISTA\...`)
— blanked on import by `tools/extract_fooava.py` (`AUTHOR_PATH_PVARS` / `scrub_author_paths`),
image path casing (Windows fs was case-insensitive, ours isn't) fixed the same way (`fix_image_case`).
