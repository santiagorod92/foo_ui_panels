#!/usr/bin/env python3
# Recover the fooAvA image assets (button PNGs, cover-case art, side-tab icons) from the
# original DeviantArt distribution .exe — a **ClickTeam Install Creator** SFX.
#
# Reliable extraction (ported from Bioruebe/cicdec). The overlay starts at the signature
# 77 77 67 54 29 48; it is a sequence of blocks: blockId(u16) + skip2 + blockSize(u32) + data.
#   FILE_LIST (0x143A): a compressed node table -> file names + each file's offset/sizes.
#   FILE_DATA (0x7F7F): the file bodies; file k lives at dataStart + offset + 4 as
#                       [method byte][zlib(78 da) deflate | BZh | raw].
# This installer is version 30 (u16 nodeSize, has a u32 index field). Mapping files by their
# recorded `offset` is exact — no size-guessing (an earlier heuristic mismapped same-size
# images, e.g. swapping themed colour variants / wrong dimensions).
#
# Usage: python3 recover_fooava_images.py <fooava_1_05_*.exe> <out_dir> [--all]
#   default: only image files (png/jpg). --all: every file (incl .ava/.pui/.txt configs).
import struct, zlib, bz2, sys, os, re

def u16(d, o): return struct.unpack_from('<H', d, o)[0]
def u32(d, o): return struct.unpack_from('<I', d, o)[0]

def unpack(d, o, dec=None):
    """Decompress a ClickTeam stream at offset o (optional known decompressed size)."""
    p = o
    if dec is None: dec = u32(d, p); p += 4
    method = d[p]; p += 1
    if method == 0: return d[p:p + dec]                       # NONE (stored)
    if d[p:p + 1] == b'\x78': return zlib.decompress(d[p + 2:], -15)[:dec]  # DEFLATE (skip zlib hdr)
    if d[p:p + 3] == b'BZh': return bz2.BZ2Decompressor().decompress(d[p:])[:dec]
    return zlib.decompressobj().decompress(d[p:])[:dec]

def parse_filelist_v30(man):
    """Parse a version-30 FILE_LIST -> list of (path, offset, compressedSize, uncompressedSize)."""
    n = u16(man, 0); p = 4; files = []
    for _ in range(n):
        if p + 4 > len(man): break
        ns = p; nodeSize = u16(man, p); type_ = u16(man, p + 2); ne = ns + nodeSize
        if ne > len(man) or nodeSize < 4: break
        if type_ != 0: p = ne; continue          # not a file node
        q = p + 4 + 2                              # nodeSize(2)+type(2)+skip2
        offset = u32(man, q); comp = u32(man, q + 4); uncomp = u32(man, q + 12)
        q += 18 + 16 + 4 + 24                      # fields + skip18 + index(4) + 3x FILETIME(24)
        path = man[q:ne].split(b'\x00')[0].decode('latin1')
        files.append((path, offset, comp, uncomp))
        p = ne
    return files

def main(exe, outdir, want_all=False):
    d = open(exe, 'rb').read()
    sig = d.find(b'\x77\x77\x67\x54\x29\x48')
    if sig < 0: print('overlay signature not found'); return 1
    p = sig + 6; L = len(d); dataStart = None; man = None
    while p + 8 <= L:
        bid = u16(d, p); bsize = u32(d, p + 4); bdata = p + 8; nxt = bdata + bsize
        if bid == 0x7F7F: dataStart = bdata
        elif bid == 0x143A:
            try: man = unpack(d, bdata)
            except Exception as e: print('FILE_LIST unpack failed:', e)
        if nxt <= p: break
        p = nxt
    if man is None or dataStart is None: print('missing FILE_LIST/FILE_DATA'); return 1

    files = parse_filelist_v30(man)
    os.makedirs(outdir, exist_ok=True)
    made = 0
    keep = re.compile(r'\.(png|jpg|jpeg)$' if not want_all else r'\.\w+$', re.I)
    for path, offset, comp, uncomp in files:
        if not path or not keep.search(path): continue
        rel = path.replace('PanelsUI\\AvA 1.05\\', '').replace('\\', '/').lstrip('/')
        try: data = unpack(d, dataStart + offset + 4, uncomp)
        except Exception: continue
        fp = os.path.join(outdir, rel)
        os.makedirs(os.path.dirname(fp) or outdir, exist_ok=True)
        open(fp, 'wb').write(data); made += 1
    print(f"files={len(files)} extracted={made} -> {outdir}")
    return 0

if __name__ == '__main__':
    a = [x for x in sys.argv[1:] if x != '--all']
    sys.exit(main(a[0], a[1], '--all' in sys.argv))
