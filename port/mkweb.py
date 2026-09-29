#!/usr/bin/env python3
"""Pictures of the web build (web/build.sh, RVIP W0 rule 6: no canvas but the map).
mkweb.py OUTDIR writes:
  tiles-oryx.png  the Oryx sprites (tiles.h, 1 bit 16x24) pre-coloured: 16 bands,
                  one per CGA colour, 32 sprites per row (CSS sprites, map)
  rogue-title.png the CGA title picture (../data/rogue.pic, 320x200, palette 1)
  tiles-web.json  per DawnLike slot: drawn (has), differs in frame 1 (anim)"""
import json, os, re, sys
from PIL import Image
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = sys.argv[1]
PAL = [0x000000, 0x0000aa, 0x00aa00, 0x00aaaa, 0xaa0000, 0xaa00aa, 0xaa5500, 0xaaaaaa,
       0x555555, 0x5555ff, 0x55ff55, 0x55ffff, 0xff5555, 0xff55ff, 0xffff55, 0xffffff]
rgb = lambda v: (v >> 16, v >> 8 & 255, v & 255)

h = open(os.path.join(HERE, 'tiles.h')).read()
dfn = lambda n: int(re.search(r'#define %s (\d+)' % n, h).group(1))
N, TW, TH = dfn('NTILES'), dfn('TW'), dfn('TH')
bits = [int(v, 16) for v in re.findall(r'0x([0-9a-f]{4})', re.search(r'tile_bits[^{]*\{(.*?)\};', h, re.S).group(1))]
rows = (N + 31) // 32
im = Image.new('RGBA', (32 * TW, 16 * rows * TH), (0, 0, 0, 0))
px = im.load()
for c in range(16):
    for t in range(N):
        for y in range(TH):
            b = bits[t * TH + y]
            for x in range(TW):
                if b >> (15 - x) & 1:
                    px[(t & 31) * TW + x, (c * rows + (t >> 5)) * TH + y] = rgb(PAL[c]) + (255,)
im.save(os.path.join(OUT, 'tiles-oryx.png'), optimize=True)

d = open(os.path.join(HERE, '..', 'data', 'rogue.pic'), 'rb').read()
d += bytes(7 + 16384 - len(d))
CGA1 = [(0, 0, 0), (0x55, 0xff, 0xff), (0xff, 0x55, 0xff), (0xff, 0xff, 0xff)]
pic = Image.new('RGB', (320, 200))
pp = pic.load()
for y in range(200):
    for x in range(320):
        b = d[7 + (y & 1) * 8192 + (y // 2) * 80 + x // 4]
        pp[x, y] = CGA1[b >> (6 - 2 * (x & 3)) & 3]
pic.save(os.path.join(OUT, 'rogue-title.png'), optimize=True)

a = Image.open(os.path.join(HERE, 'tiles-dawn.png')).convert('RGBA')
b = Image.open(os.path.join(HERE, 'tiles-dawn-1.png')).convert('RGBA')
n = (a.width // 16) * (a.height // 16)
box = lambda t: ((t & 31) * 16, (t >> 5) * 16, (t & 31) * 16 + 16, (t >> 5) * 16 + 16)
has = [1 if a.getpixel(((t & 31) * 16 + 8, (t >> 5) * 16 + 8))[3] else 0 for t in range(n)]
anim = [1 if a.crop(box(t)).tobytes() != b.crop(box(t)).tobytes() else 0 for t in range(n)]
json.dump({'has': has, 'anim': anim}, open(os.path.join(OUT, 'tiles-web.json'), 'w'), separators=(',', ':'))
