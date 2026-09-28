#!/usr/bin/env python3
"""Second tile set for Rogue PC: DawnLike (DragonDePlatino, palette DawnBringer,
CC BY 4.0), sprites picked by name from ~/Games/rvip-tools/tilesets
(names: Tommy Ettinger's DawnLikeAtlas). DawnLike is the full set the
DawnHack sheet was cut from, so it replaces DawnHack here.
Writes tiles-dawn.png/.rgba (and tiles-dawn-1.png, DawnLike's second frame
for the opt-in animation): 16x16 full-colour sprites, 32 per row, slot =
ClassicRogue sprite number (mktiles.py), plus the 16 + 16 autotiled floor and
passage slots (TL_FLOORS, TL_CORRS). Every slot tiles.c can return is
filled (bolts are drawn as text), so the two sets never mix on the map."""
import os, re, sys
from PIL import Image
HERE = os.path.dirname(os.path.abspath(__file__))
TS = os.path.expanduser('~/Games/rvip-tools/tilesets')
sys.path.insert(0, TS)
from dawnlike_preview import pos, sprite

h = open(os.path.join(HERE, 'tiles.h')).read()
dfn = lambda n: int(re.search(r'#define %s (\d+)' % n, h).group(1))
arr = lambda n: [int(v) for v in re.search(r'%s\[[^]]*\] = \{([^}]*)\}' % n, h).group(1).split(',')]
src = open(os.path.join(HERE, '..', 'src', 'extern.c'), encoding='latin-1').read()
def names(start):
    body = src[src.index(start):]; body = body[:body.index('};')]
    return re.findall(r'"([^"]*)"', body)

MON = {  # game name -> DawnLike name, where they differ
 'aquator': 'rust monster', 'bat': 'giant bat', 'centaur': 'forest centaur', 'dragon': 'firedrake',
 'emu': 'terror bird', 'venus flytrap': 'large rotting plant', 'griffin': 'griffon',
 'ice monster': 'ice vortex', 'kestral': 'nighthawk', 'nymph': 'wood nymph', 'phantom': 'ghost',
 'quagga': 'gray horse', 'rattlesnake': 'pit viper', 'slime': 'acid blob', 'ur-vile': 'deep orc',
 'xeroc': 'large mimic', 'zombie': 'human zombie',
}
WEAP = {'short bow': 'shortbow', 'two handed sword': 'two handed sword'}
ARMOR = {'leather armor': 'bronze armor', 'ring mail': 'hotrock mail',
         'studded leather armor': 'lacquered armor', 'scale mail': 'scale armor',
         'chain mail': 'grandmaster mail', 'splint mail': 'iron armor', 'plate mail': 'full plate'}
WALL = 'lit brick wall '
FIXED = {'ULWALL': WALL + 'right down', 'URWALL': WALL + 'left down', 'LLWALL': WALL + 'right up',
         'LRWALL': WALL + 'left up', 'VWALL': WALL + 'up down', 'TWALL': WALL + 'left right',
         'BWALL': WALL + 'left right', 'PLAYER': 'fighter', 'PASSAGE': 'night stone floor c',
         'FLOOR': 'day tile floor c', 'DOOR': 'day tile floor c', 'STAIRS': 'small stairs down',
         'TRAP': 'magic trap tile', 'AMULET': 'amulet of yendor', 'FOOD': 'food ration',
         'POTION': 'clear potion', 'RING': 'gold ring', 'SCROLL': 'blank scroll',
         'STAFF': 'quarterstaff', 'WAND': 'oak wand', 'GOLD': 'pile of gold coins'}

N = dfn('NTILES')
img = Image.new('RGBA', (32 * 16, (N + 31) // 32 * 16), (0, 0, 0, 0))
img1 = img.copy()
def sprite1(name):
    sheet, c, r = pos[name]
    p = os.path.join(TS, 'DawnLike', sheet.replace('0.png', '1.png'))
    if not sheet.endswith('0.png') or not os.path.exists(p): return sprite(name)
    return Image.open(p).convert('RGBA').crop((c*16, r*16, c*16+16, r*16+16))
filled, missing = {}, []
def put(slot, name):
    if name not in pos: missing.append(name); return
    filled[slot] = name
    img.paste(sprite(name), ((slot % 32) * 16, (slot // 32) * 16))
    img1.paste(sprite1(name), ((slot % 32) * 16, (slot // 32) * 16))

mons = re.findall(r'\{\s*"([^"]*)"', src[src.index('struct monster monsters[26]'):])[:26]
for n, slot in zip(mons, arr('t_mon')): put(slot, MON.get(n, n))
for n, slot in zip(names('char *w_names'), arr('t_weap')): put(slot, WEAP.get(n, n))
for n, slot in zip(names('char *a_names'), arr('t_arm')): put(slot, ARMOR.get(n, n))
for k, n in FIXED.items(): put(dfn('TL_' + k), n)
for m in range(16):   # autotiled floors: slot base+m is bordered on the sides of mask m (n8 s4 w2 e1)
    sides = ''.join(c for b, c in ((8, 'n'), (4, 's'), (2, 'w'), (1, 'e')) if m & b) or 'c'
    put(dfn('TL_FLOORS') + m, 'day tile floor ' + sides)
    put(dfn('TL_CORRS') + m, 'night stone floor ' + sides)
if missing: sys.exit('no DawnLike sprite (add a stand-in): ' + ', '.join(sorted(set(missing))))
img.save(os.path.join(HERE, 'tiles-dawn.png'))
img1.save(os.path.join(HERE, 'tiles-dawn-1.png'))
open(os.path.join(HERE, 'tiles-dawn.rgba'), 'wb').write(
    img.size[0].to_bytes(4, 'little') + img.size[1].to_bytes(4, 'little') + img.tobytes())
print(len(filled), 'slots, all DawnLike;', len(MON) + len(WEAP) + len(ARMOR), 'stand-ins by hand')
