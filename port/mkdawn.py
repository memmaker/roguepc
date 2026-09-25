#!/usr/bin/env python3
"""Second tile set for Rogue PC: DawnHack art (DragonDePlatino, CC BY 3.0;
palette by DawnBringer) as sheeted for PC Rogue 1.48 by Rogue Collection
(dawnhack/tilemap_v4.bmp: A-Z with slime and ur-vile, player, walls, items).
Writes tiles-dawn.png/.rgba: 16x16 full-colour sprites, 32 per row, slot =
ClassicRogue sprite number (mktiles.py); slots it doesn't cover stay
transparent and the frontends draw the Oryx sprite there (bolts, effects)."""
import os
from PIL import Image
HERE = os.path.dirname(os.path.abspath(__file__))
rc = Image.open(os.path.join(HERE, 'dawnhack', 'tilemap_v4.bmp')).convert('RGBA')
N = 92
img = Image.new('RGBA', (32 * 16, (N + 31) // 32 * 16), (0, 0, 0, 0))
MAP = {5 + i: i for i in range(26)}                   # monsters A-Z
MAP.update({1: 27, 2: 28, 31: 26, 32: 32, 33: 31, 34: 29, 35: 31, 36: 30,   # corners, player, walls
            37: 34, 39: 33, 40: 35, 41: 36, 42: 37, 43: 38, 45: 39, 46: 41,
            47: 42, 48: 43, 49: 44, 50: 44, 68: 40})
MAP.update({s: 45 for s in [44] + list(range(51, 60))})       # weapons
MAP.update({s: 46 for s in range(60, 68)})                    # armour
for slot, i in MAP.items():
    img.paste(rc.crop((i * 16, 0, i * 16 + 16, 16)), ((slot % 32) * 16, (slot // 32) * 16))
img.save(os.path.join(HERE, 'tiles-dawn.png'))
open(os.path.join(HERE, 'tiles-dawn.rgba'), 'wb').write(
    img.size[0].to_bytes(4, 'little') + img.size[1].to_bytes(4, 'little') + img.tobytes())
print(len(MAP), 'slots from tilemap_v4.bmp')
