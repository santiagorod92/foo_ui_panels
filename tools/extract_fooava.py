#!/usr/bin/env python3
# Extract a loadable skin script from a PanelsUI config blob (e.g. fooAvA's s8.bin,
# itself decompressed from the SFX). Produces fooava.txt for the component to load.
#
# Pipeline (see CLAUDE.md "Loading a real PanelsUI script"):
#   1. parse the KV pvar dictionary + locate the master layout script (longest text run)
#   2. strip // comments + per-line whitespace
#   3. balance parens (PanelsUI tolerated an unmatched ')'; fb2k titleformat does not)
#   4. prepend the 53 pvar defaults as guarded $if($strcmp($getpvar(k),),$setpvar(k,v),) (NOT $ifequal(...,,...): that compares
#      numerically, so a pvar the user set to "0" counted as empty and got re-seeded every paint;
#      NOT $if($getpvar(k),...): our $getpvar reports "found" even when unset) so
#      mode-gated panels appear on a truly fresh session, WITHOUT clobbering a value the user
#      (or a button's PVAR:SET) already set this session — the seed only fires while the pvar
#      is still empty. An earlier unconditional $setpvar(k,v) re-ran every single repaint (this
#      prefix is script text, evaluated on every paint), permanently stomping any live toggle
#      back to its extraction-time snapshot — e.g. fooAvA's onepanel one/two-panel switch could
#      never actually change: every click's PVAR:SET was undone by the very next repaint.
#   5. fix image path casing against the real images/ dir (PanelsUI was authored on a
#      case-insensitive Windows fs; our images/ tree doesn't always match the decoded case)
#
# Usage: python3 extract_fooava.py <s8.bin> <out fooava.txt>
import os, re, struct, sys

def fix_image_case(text, out_path):
    # Build a case-insensitive index of the images/ dir next to `out_path` once, then
    # rewrite any \images\...ext reference to the on-disk casing when it differs.
    # Index keys are relative to the skin dir (e.g. "images/fooAVA/back.png"), matching
    # what the script references look like once the leading '/' is stripped.
    skin_dir = os.path.dirname(out_path) or '.'
    images_root = os.path.join(skin_dir, 'images')
    if not os.path.isdir(images_root):
        return text
    index = {}
    for dirpath, _, files in os.walk(images_root):
        for f in files:
            full = os.path.join(dirpath, f)
            rel = os.path.relpath(full, skin_dir).replace(os.sep, '/')
            index[rel.lower()] = rel

    pat = re.compile(r'/images\\[A-Za-z0-9_./\\ -]+\.(?:png|bmp|jpg|jpeg|ico)', re.IGNORECASE)

    def repl(m):
        ref = m.group(0)
        rel = ref.lstrip('/').replace('\\', '/')
        real = index.get(rel.lower())
        if not real or real == rel:
            return ref
        return '/' + real.replace('/', '\\')

    return pat.sub(repl, text)

# Literal author-machine paths that leak in beyond the pvar dict (e.g. baked into a
# per-panel record's raw text/name) — see AUTHOR_PATH_PVARS in main() for the pvar case.
AUTHOR_PATH_PREFIXES = [
    r'G:\\Dawid\\Muza\\Anberlin\\Cities\\',
    r'F:\\Program Files\\foobar2000_VISTA\\\\?Artists\\',
]

def keep_search_open(text):
    # The Quick Search panel is followed by $setpvar(showsr,0), which the original host only ever
    # evaluated once; we re-run the script on every repaint, so it would close the box again
    # immediately. The box has its own close button (PVAR:SET:showsr:0) and Esc.
    return text.replace("$panel(search,Quick Search Toolbar,60,1,190,20,)$setpvar(showsr,0))",
                        "$panel(search,Quick Search Toolbar,60,1,190,20,))")

def tidy_collection(text):
    # COLLECTION view (single-panel and both two-panel variants): drop the panel's dark backdrop
    # and white/grey outlines (the skin wallpaper shows through), and tint the title with the
    # skin colour instead of flat white. Safe to run more than once.
    tails = ("COLLECTION$if($strcmp(", "COLLECTION$alignabs(0,0,1,1,left,top)$if($strcmp(")
    out, pos, search = [], 0, 0
    while True:
        hits = [h for h in (text.find(t, search) for t in tails) if h >= 0]
        if not hits: break
        i = min(hits)
        start = max(pos, i - 900)
        seg = text[start:i]
        for tag in ("brushcolor-0-0-0 pencolor-255-255-255 alpha-102)", "brushcolor-0-0-0 pencolor-null alpha-102)"):
            k = seg.find(tag)
            if k >= 0:
                b = seg.rfind("$drawrect(", 0, k)
                seg = seg[:b] + seg[k + len(tag):]
        k = seg.find("brushcolor-null pencolor-150-150-150)")
        if k >= 0:
            b = seg.rfind("$drawrect(", 0, k)
            seg = seg[:b] + seg[k + len("brushcolor-null pencolor-150-150-150)"):]
        seg = seg.replace(",240-240-240)$alignabs(", ",$getpvar(colour))$alignabs(")
        out.append(text[pos:start]); out.append(seg)
        pos = i; search = i + 10
    out.append(text[pos:])
    text = "".join(out)
    # Our text boxes draw lazily (at the next flush, after any later $font), so end the title's
    # box right away — otherwise it comes out in the colour/font of whatever is styled next.
    return text.replace(tails[0], "COLLECTION$alignabs(0,0,1,1,left,top)$if($strcmp(")

def tidy_display(text):
    # Display panel (now-playing, under the CD case): re-space the title / artist / album lines
    # so the three sit evenly under the rating stars (as originally stacked 45/20/2 px above the
    # panel bottom the title floated high and the album crowded the artist). Safe to run twice.
    text = text.replace("$alignabs(,$sub(%_width%,45),", "$alignabs(,$sub(%_width%,43),")
    text = text.replace("PLAY A SONG TO SHOW INFORMATION)),2)),$sub(%_width%,20),",
                        "PLAY A SONG TO SHOW INFORMATION)),2)),$sub(%_width%,13),")
    return text.replace("']']),2)),$sub(%_width%,2),", "']']),2)),$add($sub(%_width%,13),18),")

# Per-panel fixups, by panel name.
PANEL_TIDY = {"Display": tidy_display}

def tidy_coverflow(text):
    # COVERFLOW MODE view: hide the panel's white outer outline and grey inner frame so the
    # carousel blends with the skin like the other panels (the translucent dark backdrop stays).
    # Safe to run more than once.
    out, pos, search = [], 0, 0
    while True:
        i = text.find("COVERFLOW MODE", search)
        if i < 0: break
        start = max(pos, i - 600)
        seg = text[start:i]
        seg = seg.replace("brushcolor-0-0-0 pencolor-255-255-255 alpha-102)",
                          "brushcolor-0-0-0 pencolor-null alpha-102)")
        k = seg.find("brushcolor-null pencolor-150-150-150)")
        if k >= 0:
            b = seg.rfind("$drawrect(", 0, k)
            seg = seg[:b] + seg[k + len("brushcolor-null pencolor-150-150-150)"):]
        out.append(text[pos:start]); out.append(seg)
        pos = i; search = i + len("COVERFLOW MODE")
    out.append(text[pos:])
    return "".join(out)

def scrub_author_paths(text):
    for prefix in AUTHOR_PATH_PREFIXES:
        text = re.sub(prefix, '', text)
    return text

# The volume bar's fill is authored 4 px higher than the progress bar's, though both use the
# same 10x12 knob (bar/vol<n>.png) at the same y:
#   progress  $imageabs2(,,,,<progress_px> , , 6,$sub(%_height%,38),bar/s<n>.png,)
#   volume    $imageabs2(0,12,0,0,<level_px>,0,$sub(%_width%,65),$sub(%_height%,42),bar/v<n>.png,)
# so the 3 px fill ends up riding the top of the 12 px knob instead of its middle, and the two
# bars don't line up. Both fills are the same 3 px gradient bar graphic, so match the volume
# fill's y to the progress bar's (%_height%-38) — the knob's vertical centre.
VOLUME_FILL_Y_OLD = "$imageabs2(0,12,0,0,$add($get(volume.level)),0,$sub(%_width%,65),$sub(%_height%,42),"
VOLUME_FILL_Y_NEW = "$imageabs2(0,12,0,0,$add($get(volume.level)),0,$sub(%_width%,65),$sub(%_height%,38),"

def remember_window_size(text):
    # The skin hardcodes the *author's* window size in its titlebar buttons
    # (WINDOWSIZE:'736':'527', and '371':'527' for the half-width state), so the maximise button
    # resized to 736x527 on any other machine and to a 371x527 window on restore. Point those at
    # the savedW/savedH pvars instead, and drop two invisible (no image) hit-box buttons on the
    # same spot that store the current size, so restore returns to whatever it actually was.
    for lit in ("'371':'527'", "'736':'527'"):
        text = text.replace("WINDOWSIZE:" + lit + ":RIGHT:TOP",
                            "WINDOWSIZE:$getpvar(savedW):$getpvar(savedH):RIGHT:TOP")
    # One pair of invisible buttons per titlebar group (left corner x=W-33, right corner x=W-72),
    # inserted as siblings right after the group so they sit on top of the minimode/maximise button
    # and record the current size before the skin resizes the window.
    anchor = "WINDOWSIZE:$getpvar(savedW):$getpvar(savedH):RIGHT:TOP,TOOLTIP:\"\"))"
    out = []
    pos = 0
    group = 0
    while True:
        i = text.find(anchor, pos)
        if i == -1:
            out.append(text[pos:])
            break
        j = i + len(anchor)
        if text[j:j + 1] == ',':  # the comma that introduced the next button now introduces these
            j += 1
        x = 33 if group == 0 else 72
        out.append(text[pos:j])
        out.append(f"$button($sub(%_width%,{x}),3,0,0,0,0,,,"
                   f"PVAR:SET:savedW:$eval(%_width%),TOOLTIP:)"
                   f"$button($sub(%_width%,{x}),3,0,0,0,0,,,"
                   f"PVAR:SET:savedH:$eval(%_height%),TOOLTIP:)")
        pos = j
        group += 1
    return ''.join(out)


def align_volume_bar(text):
    return text.replace(VOLUME_FILL_Y_OLD, VOLUME_FILL_Y_NEW)

def find_comment(ln):
    # Start of a // comment, ignoring "//" inside a single-quoted titleformat literal (e.g. the
    # 'cdda://' test) — cutting there used to chop the rest of the line, including the whole
    # progress-bar block that follows it in the master script.
    q = False
    for i in range(len(ln) - 1):
        c = ln[i]
        if c == "'": q = not q
        elif not q and c == '/' and ln[i + 1] == '/' and (i == 0 or ln[i - 1] != ':'):
            return i
    return -1


def preprocess(text):
    # Strip // comments + per-line whitespace, then balance parens.
    lines = []
    for ln in text.split('\n'):
        i = find_comment(ln)
        if i != -1: ln = ln[:i]
        ln = ln.strip()
        if ln: lines.append(ln)
    body = ''.join(lines)
    # Trim short trailing junk after the last ')' (extraction can grab a few bytes
    # of the next binary record onto the end of a script).
    lp = body.rfind(')')
    if lp != -1 and len(body) - lp <= 8:
        body = body[:lp + 1]
    res = []; depth = 0
    for c in body:
        if c == '(': depth += 1; res.append(c)
        elif c == ')':
            if depth == 0: continue
            depth -= 1; res.append(c)
        else: res.append(c)
    res.append(')' * depth)
    return ''.join(res)


def main(src, out):
    data = open(src, 'rb').read()
    u32 = lambda o: struct.unpack_from('<I', data, o)[0]

    # KV pvar dictionary (after a 56-byte header).
    o = 56; pvars = []
    while o + 4 <= len(data):
        if not (1 <= u32(o) <= 100): break
        nlen = u32(o); o += 4; name = data[o:o+nlen].decode('latin1'); o += nlen
        if o + 4 > len(data): break
        vlen = u32(o); o += 4
        if vlen > len(data) - o: break
        pvars.append((name, data[o:o+vlen].decode('latin1'))); o += vlen

    # The original author's machine paths (G:\Dawid\Muza\..., F:\Program Files\
    # foobar2000_VISTA\...) leak into the pvar dictionary; meaningless on any other
    # machine (see FORMAT.md "must be remapped on import"). MyCoverPath gets
    # overwritten per-track by skin_engine.cpp's kCoverInit before any panel reads it,
    # and artistFolderPath/MyArtistPath have no reader in this skin, so blanking them
    # is a no-op today and just prevents a future feature from picking up stale junk.
    AUTHOR_PATH_PVARS = {'MyCoverPath', 'artistFolderPath', 'MyArtistPath'}
    pvars = [(k, '' if k in AUTHOR_PATH_PVARS else v) for k, v in pvars]

    # backgroundd=0 in the raw dump (a saved session state, not a skin-authored default) makes
    # fooava.txt's own top-level $ifequal($getpvar(backgroundd),1, <draw wallpaper>, <solid black
    # rect over the whole canvas>) take the black branch — the official fooAvA 1.05 preview shows
    # the wallpaper on by default, so force it back on for a fresh import.
    PVAR_OVERRIDES = {'backgroundd': '1'}
    pvars = [(k, PVAR_OVERRIDES.get(k, v)) for k, v in pvars]

    # The config holds several separate printable runs: a "globals" script (font config +
    # cover-art path resolution) right after the pvar dictionary, then the master layout (the
    # longest run), then one per panel record. The globals run used to be dropped, which left
    # every $get(fontAVAsize_*) in the master empty -> all text fell back to the engine's default
    # 9pt face instead of the skin's own 8/9pt Calibri/Swis721.
    runs = []
    cur = 0
    start = 0
    for i, b in enumerate(data):
        if b in (9, 10, 13) or 32 <= b < 127:
            if cur == 0:
                start = i
            cur += 1
        else:
            if cur >= 40:
                runs.append((start, cur))
            cur = 0
    if cur >= 40:
        runs.append((start, cur))

    # First run after the pvar dictionary that is not the master: the globals script.
    master_off, master_len = max(runs, key=lambda r: r[1])
    master = data[master_off:master_off + master_len].decode('latin1')
    globals_txt = ''
    for off, ln in sorted(runs):
        if off > o and not (off == master_off and ln == master_len) and ln >= 100:
            cand = data[off:off + ln].decode('latin1').strip()
            if cand.startswith('$if') or cand.startswith('$puts'):
                globals_txt = cand
                break

    body = remember_window_size(
        align_volume_bar(tidy_coverflow(tidy_collection(keep_search_open(scrub_author_paths(preprocess(master)))))))
    gvars = preprocess(globals_txt) if globals_txt else ''

    prefix = ''.join(f'$if($strcmp($getpvar({k}),),$setpvar({k},{v}),)' for k, v in pvars)
    full = fix_image_case(prefix + gvars + body, out)
    open(out, 'w', encoding='latin1').write(full)
    print(f"pvars={len(pvars)} globals={len(gvars)} panels={body.count('$panel(')}"
          f" -> {out} ({len(full)} bytes)")

    # Per-panel scripts: records after the master = <u32 nameLen><name><meta><script>.
    # Written (preprocessed) to <out dir>/panels/<name>.txt; the component loads them by panel name.
    pdir = os.path.join(os.path.dirname(out) or '.', 'panels')
    os.makedirs(pdir, exist_ok=True)
    region = data.find(b'///END')
    recs = []; o = region
    while o < len(data) - 8:
        ln = struct.unpack_from('<I', data, o)[0]
        if 2 <= ln <= 40 and all(32 <= b < 127 for b in data[o+4:o+4+ln]) and data[o+4:o+5] != b' ':
            recs.append((o, ln, data[o+4:o+4+ln].decode('latin1'))); o += 4 + ln
        else:
            o += 1
    offs = [r[0] for r in recs] + [len(data)]
    made = 0
    for idx, (o, ln, name) in enumerate(recs):
        seg = data[o+4+ln:offs[idx+1]]
        best = b''; cur = b''
        for b in seg:
            if 9 <= b < 127: cur += bytes([b])
            else:
                if len(cur) > len(best): best = cur
                cur = b''
        if len(cur) > len(best): best = cur
        if len(best) <= 40: continue
        try:
            panel_path = os.path.join(pdir, name + '.txt')
            panel_text = fix_image_case(scrub_author_paths(preprocess(best.decode('latin1'))), out)
            panel_text = PANEL_TIDY.get(name, lambda t: t)(panel_text)
            open(panel_path, 'w', encoding='latin1').write(panel_text)
            made += 1
        except OSError:
            pass
    print(f"per-panel scripts: {made} -> {pdir}")

if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
