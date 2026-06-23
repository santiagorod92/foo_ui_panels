#!/usr/bin/env python3
# Recover the fooAvA image assets (button PNGs, cover-case art, side-tab icons) from the
# original DeviantArt distribution .exe.
#
# The .exe is a ClickTeam Install Creator SFX (look for "clickteam.com" in the PE head).
# Layout we exploit:
#   - bzip2 streams (DLLs, config s8, wallpaper JPEGs) early in the file  -> handled elsewhere
#   - a ClickTeam FILE_LIST manifest: a zlib stream (~offset 232k) of length-named records.
#     Each record stores the *uncompressed* file size as a LE32 at <name_end + 27>.
#   - a FILE_DATA region (~offset 4.8M) of concatenated zlib streams, one per file, in the
#     SAME order as the manifest. Most image files are stored (BTYPE=0) so the raw PNG is
#     directly present, but some are deflate-compressed -> we must zlib-decode each stream.
#
# Strategy: decode every zlib stream in the data region (in order) -> exact file bytes.
# Build the ordered manifest (name, size) records. Align data files to manifest records by
# matching uncompressed size within a forward window (handles files stored outside this
# region, e.g. configs/walls). Any record left unplaced is filled by a global bare-name+size
# lookup. Filenames are written under their real relative paths (images/fooAVA/...).
#
# Usage: python3 recover_fooava_images.py <fooava_1_05_*.exe> <out_dir>
import re, struct, zlib, os, sys

def decode_data_region(d, search_from=4_800_000):
    """Decompress each complete zlib stream in the FILE_DATA region, in order."""
    pos = d.find(b'\x78\xda', search_from); n = len(d); out = []
    while pos < n - 4:
        if d[pos] == 0x78 and d[pos+1] in (0x01, 0x9c, 0xda):
            dec = zlib.decompressobj()
            try:
                blob = dec.decompress(d[pos:]) + dec.flush()
            except Exception:
                pos += 1; continue
            consumed = (n - pos) - len(dec.unused_data)
            if len(blob) > 20:
                out.append(blob)
            pos += max(consumed, 1)
        else:
            pos += 1
    return out

def find_manifest(d):
    """Return the decompressed ClickTeam FILE_LIST (the zlib block richest in .png names)."""
    best = None
    for mt in re.finditer(b'\x78[\x01\x9c\xda]', d):
        z = mt.start()
        try:
            o = zlib.decompressobj().decompress(d[z:z+500_000])
        except Exception:
            continue
        if o.count(b'.png') >= 50 and (best is None or o.count(b'.png') > best.count(b'.png')):
            best = o
    return best

def manifest_records(m):
    """Ordered (fullpath, uncompressed_size). Size lives at name_end+27; dir is the preceding
    run that ends with a backslash."""
    runs = [(mt.start(), mt.group().decode('latin1'))
            for mt in re.finditer(rb'[ -~]{2,90}', m)]
    recs = []
    for k, (off, s) in enumerate(runs):
        if s.endswith('\\'):
            continue
        if re.search(r'\.(png|jpg|jpeg|ava|pui|txt|dll|exe|lnk|ini)$', s, re.I):
            ne = off + len(s)
            sz = struct.unpack_from('<I', m, ne + 27)[0] if ne + 31 <= len(m) else -1
            full = (runs[k-1][1] + s) if (k > 0 and runs[k-1][1].endswith('\\')) else s
            recs.append((full, sz))
    return recs

def rel_path(full):
    return full.replace('PanelsUI\\AvA 1.05\\', '').replace('\\', '/').lstrip('/')

def main(exe, outdir):
    d = open(exe, 'rb').read()
    D = decode_data_region(d)
    m = find_manifest(d)
    if not m:
        print('manifest not found'); return 1
    recs = manifest_records(m)
    os.makedirs(outdir, exist_ok=True)

    # pass 1: ordered window alignment by size
    placed = set(); wrote = 0; j = 0
    for blob in D:
        s = len(blob); start = j; found = -1
        for jj in range(start, min(start + 400, len(recs))):
            if recs[jj][1] == s:
                found = jj; break
        if found < 0:
            continue
        j = found + 1
        name = recs[found][0]
        if re.search(r'\.(png|jpg|jpeg)$', name, re.I):
            fp = os.path.join(outdir, rel_path(name))
            os.makedirs(os.path.dirname(fp) or outdir, exist_ok=True)
            open(fp, 'wb').write(blob); placed.add(found); wrote += 1

    # pass 2: fill any image record not yet placed via bare-name + size lookup
    bysize = {}
    for blob in D:
        if blob[:8] == b'\x89PNG\r\n\x1a\n':
            bysize.setdefault(len(blob), blob)
    filled = 0
    for idx, (name, sz) in enumerate(recs):
        if idx in placed or not re.search(r'\.png$', name, re.I):
            continue
        if sz in bysize:
            fp = os.path.join(outdir, rel_path(name))
            os.makedirs(os.path.dirname(fp) or outdir, exist_ok=True)
            open(fp, 'wb').write(bysize[sz]); filled += 1

    print(f"data files={len(D)} manifest recs={len(recs)} wrote={wrote} filled={filled}")
    return 0

if __name__ == '__main__':
    sys.exit(main(sys.argv[1], sys.argv[2]))
