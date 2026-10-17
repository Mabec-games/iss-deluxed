"""Build mods/title_logo/{logo,reference}.bmp for the title-logo overlay.

reference.bmp is the finished original logo exactly as the game draws it;
the overlay only appears while the frame matches it. logo.bmp replaces it.
Both cover the same box, at native resolution, in the 256-pixel view.

usage: build_title_logo.py BRUSH_FRAME TEXT_FRAME FINAL_FRAME TEXT_PNG OUT_DIR
  Native 398x224 screenshots of the title screen with only the brushstroke
  drawn, with the lettering added, and with the logo complete.

The original ball is kept pixel for pixel; the 'd is drawn onto it below.
"""
import struct, sys
from PIL import Image

MARGIN = 71                      # 16:9 screenshots: (398 - 256) / 2
BOX = (89, 26, 321, 98)          # logo box in the 398-wide screenshot
TEXT_AT, TEXT_SIZE = (93, 41), (160, 55)   # final place of the lettering

def save_bmp(path, im):
    im = im.convert('RGBA'); w, h = im.size
    px = b''.join(struct.pack('<BBBB', b, g, r, a) for r, g, b, a in im.get_flattened_data())
    head = struct.pack('<2sIHHI', b'BM', 54 + len(px), 0, 0, 54)
    info = struct.pack('<IiiHHIIiiII', 40, w, -h, 1, 32, 0, len(px), 2835, 2835, 0, 0)
    open(path, 'wb').write(head + info + px)

brush_f, text_f, final_f, text_png, out = sys.argv[1:6]
brush, lettered, final = (Image.open(f).convert('RGBA') for f in (brush_f, text_f, final_f))
W, H = final.size
logo = final.copy()
bp, lp, fp, op = brush.load(), lettered.load(), final.load(), logo.load()
ball = [[fp[x, y] != lp[x, y] for x in range(W)] for y in range(H)]   # drawn after the lettering
for y in range(BOX[1], BOX[3]):
    for x in range(BOX[0], BOX[2]):
        if not ball[y][x] and lp[x, y] != bp[x, y]:
            op[x, y] = bp[x, y]                  # erase the original lettering
text = Image.open(text_png).convert('RGBA').resize(TEXT_SIZE, Image.LANCZOS)
text.putalpha(text.getchannel('A').point(lambda a: 255 if a >= 128 else 0))
logo.alpha_composite(text, TEXT_AT)
for y in range(BOX[1], BOX[3]):                  # the ball stays in front
    for x in range(BOX[0], BOX[2]):
        if ball[y][x]: op[x, y] = fp[x, y]

# The 'd, drawn the way the ball's own letters are: embossed, not outlined.
# Each horizontal run of a stroke goes highlight -> yellow -> orange -> brown
# from left to right and ends in a one-pixel black shadow, and the glyph
# leans right towards the bottom like the E beside it. Only the edges that
# face the white background get the ball rim's dark brown.
RAMP = [(255, 255, 123), (255, 247, 16), (255, 247, 16), (255, 206, 16),
        (247, 156, 0), (189, 132, 0), (165, 99, 0)]
SHADOW, RIM = (0, 0, 0), (57, 41, 0)
GLYPHS = {
    'd': ["        #####",
          "        #####",
          "        #####",
          "        #####",
          "        #####",
          "        #####",
          "        #####",
          "        #####",
          "        #####",
          "        #####",
          "   ##########",
          " ############",
          "#############",
          "#####   #####",
          "####     ####",
          "####     ####",
          "####     ####",
          "####     ####",
          "####     ####",
          "####     ####",
          "####     ####",
          "####     ####",
          "#####   #####",
          "#############",
          " ############",
          "  ###### ####"],
    "'": ["####", "####", "####", " ###", " ##.", "##.."],
}
def emboss(name, ox, oy, lean):
    rows = GLYPHS[name]; h = len(rows)
    cells = {}
    for j, row in enumerate(rows):
        shift = round(lean * j / max(h - 1, 1))
        runs, i = [], 0
        while i < len(row):
            if row[i] == '#':
                k = i
                while k < len(row) and row[k] == '#': k += 1
                runs.append((i, k)); i = k
            else: i += 1
        for i0, i1 in runs:
            n = i1 - i0
            for i in range(i0, i1):
                t = (i - i0) / max(n - 1, 1)
                col = SHADOW if i == i1 - 1 and n > 2 else RAMP[min(int(t * (len(RAMP) - 1) + .5), len(RAMP) - 1)]
                cells[(ox + i + shift, oy + j)] = col
    for (x, y), col in cells.items():
        bg = fp[x, y][:3]
        outside = bg[0] > 200 and bg[1] > 200       # white title background
        edge = any((x + dx, y + dy) not in cells for dx, dy in ((1,0),(-1,0),(0,1),(0,-1)))
        op[x, y] = (RIM if edge and outside else col) + (255,)
emboss("'", 289, 35, 1)
emboss('d', 293, 40, 3)
save_bmp(f'{out}/logo.bmp', logo.crop(BOX))
# For the fly-in: the brushstroke alone, the original lettering alone (every
# pixel it changes, in its true colours) and the new lettering at its final
# size. The game tells lettering from brushstroke by the colours that only
# the lettering uses.
save_bmp(f'{out}/brush.bmp', brush.crop(BOX))
old = Image.new('RGBA', (BOX[2] - BOX[0], BOX[3] - BOX[1]), (0, 0, 0, 0))
for y in range(BOX[1], BOX[3]):
    for x in range(BOX[0], BOX[2]):
        if lp[x, y] != bp[x, y]:
            old.putpixel((x - BOX[0], y - BOX[1]), lp[x, y][:3] + (255,))
save_bmp(f'{out}/old_text.bmp', old)
save_bmp(f'{out}/text.bmp', text)
save_bmp(f'{out}/reference.bmp', final.crop(BOX))
print('box in 256 view:', BOX[0] - MARGIN, BOX[1], 'size', BOX[2] - BOX[0], BOX[3] - BOX[1])
