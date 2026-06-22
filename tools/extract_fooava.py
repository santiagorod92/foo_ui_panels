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

    # Strip // comments + whitespace.
    lines = []
    for ln in master.split('\n'):
        i = ln.find('//')
        if i != -1: ln = ln[:i]
        ln = ln.strip()
        if ln: lines.append(ln)
    body = ''.join(lines)

    # Balance parens.
    res = []; depth = 0
    for c in body:
        if c == '(': depth += 1; res.append(c)
        elif c == ')':
            if depth == 0: continue
            depth -= 1; res.append(c)
        else: res.append(c)
    res.append(')' * depth)
    body = ''.join(res)

    prefix = ''.join(f'$setpvar({k},{v})' for k, v in pvars)
    open(out, 'w', encoding='latin1').write(prefix + body)
    print(f"pvars={len(pvars)} panels={body.count('$panel(')} -> {out} ({len(prefix)+len(body)} bytes)")

if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
