#!/usr/bin/env python3
"""PNG contact sheets of a sweet-film dump (userland/tools/sweet-film.c).

usage: tools/sweet-film.py DUMP OUT_PREFIX [frames per sheet]

One sheet per group of frames: each row of the sheet is one surface (in the order recorded), each
column one frame, labelled with milliseconds since the first frame."""
import struct
import sys
from PIL import Image, ImageDraw

data = open(sys.argv[1], 'rb').read()
assert data[:6] == b'SWFILM', 'not a sweet-film dump'
frames, count = struct.unpack_from('<II', data, 6)
at = 14
shots = []
for n in range(frames):
    row = []
    for k in range(count):
        sid, w, h = struct.unpack_from('<3I', data, at)
        t, = struct.unpack_from('<Q', data, at + 12)
        at += 20
        px = data[at:at + w * h * 4]
        at += w * h * 4
        img = Image.frombytes('RGBX', (w, h), px, 'raw', 'BGRX').convert('RGB')
        row.append((sid, t, img))
    shots.append(row)
per = int(sys.argv[3]) if len(sys.argv) > 3 else 8
t0 = shots[0][0][1]
for first in range(0, frames, per):
    group = shots[first:first + per]
    w, h = group[0][0][2].size
    sheet = Image.new('RGB', ((w + 6) * len(group), (h + 18) * count), 'white')
    draw = ImageDraw.Draw(sheet)
    for c, row in enumerate(group):
        for r, (sid, t, img) in enumerate(row):
            x, y = c * (w + 6), r * (h + 18)
            sheet.paste(img, (x, y + 16))
            draw.text((x + 2, y + 2), 's%u +%d ms' % (sid, (t - t0) / 19200), fill='black')
    out = '%s-%02d.png' % (sys.argv[2], first // per)
    sheet.save(out)
    print(out)
