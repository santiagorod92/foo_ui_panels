#!/usr/bin/env python3
import struct, sys, zlib, lzma, re, subprocess, os
from concurrent.futures import ThreadPoolExecutor
from collections import deque
xip, outdir = sys.argv[1], sys.argv[2]
f = open(xip, 'rb')
magic, hsize, ver, tocc, tocu, alg = struct.unpack('>4sHHQQI', f.read(28))
assert magic == b'xar!'
f.seek(hsize); toc = zlib.decompress(f.read(tocc)).decode()
heap = hsize + tocc
m = re.search(r'<name>Content</name>', toc)
seg = toc[:m.start()]; fi = seg.rfind('<file')
blk = toc[fi:m.end()+2000]
off = int(re.search(r'<offset>(\d+)</offset>', blk).group(1))
length = int(re.search(r'<length>(\d+)</length>', blk).group(1))
f.seek(heap + off)
assert f.read(4) == b'pbzx'
flags, = struct.unpack('>Q', f.read(8))
end = heap + off + length
def chunks():
    while f.tell() + 16 <= end:
        h = f.read(16)
        if len(h) < 16: return
        usize, csize = struct.unpack('>QQ', h)
        yield f.read(csize), usize
def dec(a):
    data, usize = a
    return data if len(data) == usize else lzma.decompress(data)
os.makedirs(outdir, exist_ok=True)
p = subprocess.Popen(['cpio', '-idm', '--quiet'], stdin=subprocess.PIPE, cwd=outdir)
n = 0
with ThreadPoolExecutor(os.cpu_count()) as ex:
    q = deque()
    def drain(k):
        global n
        while len(q) > k:
            out = q.popleft().result()
            p.stdin.write(out); n += len(out)
            if n % (1 << 30) < len(out): print(f'{n >> 30} GiB', flush=True)
    for c in chunks():
        q.append(ex.submit(dec, c)); drain(2 * os.cpu_count())
    drain(0)
p.stdin.close(); p.wait()
print('done', n)
