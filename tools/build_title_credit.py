"""Build mods/title_logo/credit{,_reference}.bmp: the title screen's bottom
credit lines replaced by "MABEC GAMES, 2026" in the game's own sprite font.

usage: build_title_credit.py FULL_FRAME BG_ONLY_FRAME OUT_DIR
  Native 4:3 (256x224) captures of the finished title screen: as drawn, and
  with sprites hidden (SNESRECOMP_LAYER_MASK=15), which shows what lies
  behind the text.

Letters come from the original two lines. Characters the title does not
show are derived from the font itself: 0 is the O, 2 the mirrored S, 6 the
9 turned upside down (recoloured row by row), the comma the full stop with
a tail.
"""
import struct, sys
from PIL import Image

BOX = (8, 203, 248, 223)          # credit area in the 256-wide view
LINE1, LINE2 = 205, 214           # top rows of the two original lines
H = 7                             # glyph height
# (line, x0, x1) of each source glyph in the original credit text
SRC = {
    'M': (1, 89, 95), 'A': (1, 81, 87), 'O': (1, 65, 71), '9': (1, 37, 43),
    'G': (1, 146, 152), '.': (1, 243, 244),
    'B': (2, 124, 130), 'E': (2, 73, 79), 'C': (2, 64, 70), 'S': (2, 90, 96),
}
TEXT = "MABEC GAMES, 2026"
SPACE = 6                        # each glyph advances by its own width + 1, as the font does

def save_bmp(path, im):
    im = im.convert('RGBA'); w, h = im.size
    px = b''.join(struct.pack('<BBBB', b, g, r, a) for r, g, b, a in im.get_flattened_data())
    head = struct.pack('<2sIHHI', b'BM', 54 + len(px), 0, 0, 54)
    info = struct.pack('<IiiHHIIiiII', 40, w, -h, 1, 32, 0, len(px), 2835, 2835, 0, 0)
    open(path, 'wb').write(head + info + px)

full_f, bg_f, out = sys.argv[1:4]
full = Image.open(full_f).convert('RGB'); bg = Image.open(bg_f).convert('RGB')
fp, bp = full.load(), bg.load()

def glyph(line, x0, x1):
    top = LINE1 if line == 1 else LINE2
    return [[fp[x, top + y] if fp[x, top + y] != bp[x, top + y] else None
             for x in range(x0, x1 + 1)] for y in range(H)]

g = {c: glyph(*v) for c, v in SRC.items()}
def row_colour(src, y):        # the most common colour on one row of a glyph
    counts = {}
    for c in src[y]:
        if c: counts[c] = counts.get(c, 0) + 1
    return max(counts, key=counts.get) if counts else None
g['0'] = g['O']
g['2'] = [list(reversed(r)) for r in g['S']]
nine = g['9']
g['6'] = [[row_colour(nine, y) if c else None for c in reversed(nine[H - 1 - y])] for y in range(H)]
dot = g['.']
g[','] = [r[:] for r in dot] + [[None] * len(dot[0])]
g[','][H][0] = row_colour(dot, H - 1)

width = sum(SPACE if c == ' ' else len(g[c][0]) + 1 for c in TEXT) - 1
img = bg.crop(BOX).convert('RGBA')
ip = img.load()
x = (BOX[2] - BOX[0] - width) // 2
top = (LINE1 + LINE2) // 2 + 1 - BOX[1]
for c in TEXT:
    if c == ' ': x += SPACE; continue
    for y, row in enumerate(g[c]):
        for i, col in enumerate(row):
            if col: ip[x + i, top + y] = col + (255,)
    x += len(g[c][0]) + 1
save_bmp(f'{out}/credit.bmp', img)
save_bmp(f'{out}/credit_reference.bmp', full.crop(BOX))
img.convert('RGB').resize((img.width * 4, img.height * 4), Image.NEAREST).save(f'{out}/credit_preview.png')
print('credit box', BOX, 'text width', width)
