#!/usr/bin/env python3
"""PNG of a sweet-shot dump (userland/tools/sweet-shot.c): tools/sweet-shot.py DUMP PNG.
The panel's memory is blue-green-red-x, 4 bytes a pixel."""
import struct, sys, zlib

data = open(sys.argv[1], 'rb').read()
assert data[:6] == b'SWSHOT', 'not a sweet-shot dump'
w, h, row, fmt = struct.unpack_from('<4I', data, 6)
px = data[22:]
raw = bytearray()
for y in range(h):
    line = px[y * row: y * row + w * 4]
    raw.append(0)
    for x in range(0, len(line), 4):
        b, g, r = line[x], line[x + 1], line[x + 2]
        raw += bytes((r, g, b))
def chunk(t, d):
    c = struct.pack('>I', len(d)) + t + d
    return c + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(bytes(raw), 6)) + chunk(b'IEND', b'')
open(sys.argv[2], 'wb').write(png)
print(f'{sys.argv[2]}: {w} x {h}')
