#!/usr/bin/env python3
"""Extract the 1-bit 16x24 sprites (tiles by Oryx) embedded in ClassicRogue 2.5
(Donnie Russell's Windows port of PC Rogue 1.48): .rdata of ClassicRogue.exe,
unpacked with `7z x ClassicRogue.exe`. The game colours each sprite itself.
Usage: extract.py <.rdata> -> sprites.png (92 sprites, 23 per row, white on transparent)
Order: 5-30 monsters A-Z, 31 player, 32-39 terrain, 40 door, 41 stairs,
42 trap, 43 amulet, 44 food, 45 fruit, 46 potion, 47 ring, 48 scroll,
49-50 staff/wand, 51-59 weapons, 60-67 armour, 68 gold, 70-85 bolts, 86-91 effects."""
import sys
from PIL import Image
d = open(sys.argv[1], 'rb').read()
BASE, N, W, H = 53198, 92, 16, 24          # found by row-period analysis (24 rows, blank row 6)
sheet = Image.new('RGBA', (23 * W, 4 * H), (0, 0, 0, 0))
for t in range(N):
    o = BASE + t * W * H // 8
    for y in range(H):
        r = d[o + 2 * y] << 8 | d[o + 2 * y + 1]
        for x in range(W):
            if r >> (15 - x) & 1:
                sheet.putpixel(((t % 23) * W + x, (t // 23) * H + y), (255, 255, 255, 255))
sheet.save('sprites.png')
