#!/usr/bin/env python3
# Extract a loadable skin script from a PanelsUI config blob (e.g. fooAvA's s8.bin,
# itself decompressed from the SFX). Produces fooava.txt for the component to load.
#
# Pipeline (see CLAUDE.md "Loading a real PanelsUI script"):
#   1. parse the KV pvar dictionary + locate the master layout script (longest text run)
#   2. strip // comments + per-line whitespace
#   3. balance parens (PanelsUI tolerated an unmatched ')'; fb2k titleformat does not)
#   4. prepend the 53 pvar defaults as $setpvar(...) so mode-gated panels appear
#
# Usage: python3 extract_fooava.py <s8.bin> <out fooava.txt>
import struct, sys

def preprocess(text):
    # Strip // comments + per-line whitespace, then balance parens.
    lines = []
    for ln in text.split('\n'):
        i = ln.find('//')
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

    # Master layout = longest run of printable bytes in the remaining blob.
    best = (0, 0); cur = 0; start = 0
    for i, b in enumerate(data):
        if 9 <= b < 127:
            if cur == 0: start = i
            cur += 1
            if cur > best[1]: best = (start, cur)
        else: cur = 0
    master = data[best[0]:best[0]+best[1]].decode('latin1')

    body = preprocess(master)

    prefix = ''.join(f'$setpvar({k},{v})' for k, v in pvars)
    open(out, 'w', encoding='latin1').write(prefix + body)
    print(f"pvars={len(pvars)} panels={body.count('$panel(')} -> {out} ({len(prefix)+len(body)} bytes)")

    # Per-panel scripts: records after the master = <u32 nameLen><name><meta><script>.
    # Written (preprocessed) to <out dir>/panels/<name>.txt; the component loads them by panel name.
    import os
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
            open(os.path.join(pdir, name + '.txt'), 'w', encoding='latin1').write(preprocess(best.decode('latin1')))
            made += 1
        except OSError:
            pass
    print(f"per-panel scripts: {made} -> {pdir}")

if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
