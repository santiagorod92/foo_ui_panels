#!/usr/bin/env python3
import os, re, struct, sys

FOOAVA_SKIN_CONFIG = """\
# foo_ui_panels skin configuration for fooAvA 1.05 (written by tools/extract_fooava.py).
# Keys: README.md, "Skin configuration".
script = {script}
images = images/fooAVA

# Theme: accent colour "r-g-b" and theme number (1 black, 2 blue, 3 red, 4 green).
theme.accent_pvar = colour
theme.index_pvar = colour.b
theme.index_default = 2

# Fonts: the face is set through $setpvar(fontAVA,...); stand-ins for faces rarely installed.
font.face_pvar = fontAVA
font.alias.Swis721 Cn BT D-Type = Nimbus Sans Narrow
font.alias.Calibri = Carlito
font.alias.HandelGotD = Nimbus Sans Narrow

# Wallpaper (shared with the native panels): drawn below the 24px title bar.
background.enabled_pvar = backgroundd
background.image_pvar = background
background.alpha_pvar = alpha.bgr
background.alpha = 195
background.top = 24

# Art the native panels draw (relative to images; {{theme}} = theme number, {{n}} = stars).
asset.bar_fill = bar/v{{theme}}.png
asset.volume_knob = bar/vol{{theme}}.png
asset.nocover = nocoverb.png
asset.cd_case = cdcaseck3.png
asset.cd_case_window = 57 8 494 474
asset.rating_stars = rating_stars24/{{n}}s1.png

# The analyser strips sit on the cover: the 20px one is the reflection; lift the pair a bit.
spectrum.mirror_below = 30
spectrum.grow = 8
spectrum.raise = 26

# Settings popup: its layout needs 360px (theme swatches left of the font column).
popup.size = 360 500

# Mini player (View > Panels UI > Mini mode): the size fooAvA's own MiniPlayer button uses, the
# top-right corner kept in place, and the same variables its restore button reads.
mini.size = 430 172
mini.anchor = RIGHT TOP
mini.saved_w_pvar = savedW
mini.saved_h_pvar = savedH

# View > Panels UI > Skin commands (assignable to keyboard shortcuts).
command.Toggle title bar = PVAR:TOGGLE:hidetitlebar

# Cover lookup the skin did through a dead plugin.
cover.pvar = MyCoverPath
cover.pattern = *folder*.*

# The cover's mini playlist overlay becomes the Lyric Show panel; the CD-case edge then
# toggles cover <-> lyrics (state 3 folds back to 1).
panel.remap.mini.playlist = mini.lyrics|Lyric Show
action.remap.PVAR:SET:mini.panels:3 = PVAR:SET:mini.panels:1

# First-run flags cleared when a settings popup opens (first.cf=1 would show the page asking
# to install foo_chronflow instead of the native cover flow).
onboarding.pvars = first.* !first.cf

# Cover flow needed foo_chronflow (dead); the native panel provides it: turn it on once.
pvar.once.cfbutton = 1
pvar.once.first.cf = 0
"""

def write_skin_config(out):
    path = os.path.join(os.path.dirname(out) or '.', 'foo_ui_panels.ini')
    if os.path.exists(path):
        print(f"skin config: {path} already exists, left as is")
        return
    with open(path, 'w', encoding='utf-8') as f:
        f.write(FOOAVA_SKIN_CONFIG.format(script=os.path.basename(out)))
    print(f"skin config -> {path}")

def fix_image_case(text, out_path):
    skin_dir = os.path.dirname(out_path) or '.'
    if os.path.basename(skin_dir) == 'panels':
        skin_dir = os.path.dirname(skin_dir) or '.'
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

AUTHOR_PATH_PREFIXES = [
    r'G:\\Dawid\\Muza\\Anberlin\\Cities\\',
    r'F:\\Program Files\\foobar2000_VISTA\\\\?Artists\\',
]

def keep_search_open(text):
    return text.replace("$panel(search,Quick Search Toolbar,60,1,190,20,)$setpvar(showsr,0))",
                        "$panel(search,Quick Search Toolbar,60,1,190,20,))")

def tidy_collection(text):
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
    return text.replace(tails[0], "COLLECTION$alignabs(0,0,1,1,left,top)$if($strcmp(")

def tidy_display(text):
    text = text.replace("$alignabs(,$sub(%_width%,45),", "$alignabs(,$sub(%_width%,43),")
    text = text.replace("PLAY A SONG TO SHOW INFORMATION)),2)),$sub(%_width%,20),",
                        "PLAY A SONG TO SHOW INFORMATION)),2)),$sub(%_width%,13),")
    return text.replace("']']),2)),$sub(%_width%,2),", "']']),2)),$add($sub(%_width%,13),18),")

PANEL_TIDY = {"Display": tidy_display}

def tidy_coverflow(text):
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

VOLUME_FILL_Y_OLD = "$imageabs2(0,12,0,0,$add($get(volume.level)),0,$sub(%_width%,65),$sub(%_height%,42),"
VOLUME_FILL_Y_NEW = "$imageabs2(0,12,0,0,$add($get(volume.level)),0,$sub(%_width%,65),$sub(%_height%,38),"

def remember_window_size(text):
    for lit in ("'371':'527'", "'736':'527'"):
        text = text.replace("WINDOWSIZE:" + lit + ":RIGHT:TOP",
                            "WINDOWSIZE:$getpvar(savedW):$getpvar(savedH):RIGHT:TOP")
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
        if text[j:j + 1] == ',':
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
    q = False
    for i in range(len(ln) - 1):
        c = ln[i]
        if c == "'": q = not q
        elif not q and c == '/' and ln[i + 1] == '/' and (i == 0 or ln[i - 1] != ':'):
            return i
    return -1

def preprocess(text):
    lines = []
    for ln in text.split('\n'):
        i = find_comment(ln)
        if i != -1: ln = ln[:i]
        ln = ln.strip()
        if ln: lines.append(ln)
    body = ''.join(lines)
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

    o = 56; pvars = []
    while o + 4 <= len(data):
        if not (1 <= u32(o) <= 100): break
        nlen = u32(o); o += 4; name = data[o:o+nlen].decode('latin1'); o += nlen
        if o + 4 > len(data): break
        vlen = u32(o); o += 4
        if vlen > len(data) - o: break
        pvars.append((name, data[o:o+vlen].decode('latin1'))); o += vlen

    AUTHOR_PATH_PVARS = {'MyCoverPath', 'artistFolderPath', 'MyArtistPath'}
    pvars = [(k, '' if k in AUTHOR_PATH_PVARS else v) for k, v in pvars]

    PVAR_OVERRIDES = {'backgroundd': '1'}
    pvars = [(k, PVAR_OVERRIDES.get(k, v)) for k, v in pvars]

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

    region = data.find(b'///END')
    if region < 0:
        print('no per-panel region (standalone popup config)')
        return
    write_skin_config(out)
    pdir = os.path.join(os.path.dirname(out) or '.', 'panels')
    os.makedirs(pdir, exist_ok=True)
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
