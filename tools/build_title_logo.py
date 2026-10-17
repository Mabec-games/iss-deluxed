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
TEXT_AT, TEXT_SIZE = (93, 41), (160, 55)

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

# The 'd, in the ball's own colours, tucked against its right edge.
C = {'K': (0, 0, 0), 'Y': (255, 247, 16), 'H': (255, 255, 123), 'G': (255, 206, 16),
     'M': (247, 156, 0), 'B': (189, 132, 0)}
APOS = ["KKKK", "KHYK", "KYYK", "KGYK", ".KYK", ".KK.", "KK.."]
D = ["........KKKK", "........KHYK", "........KYYK", "........KYYK", "........KYYK",
     "..KKKKKKKYYK", ".KHHHHGGYYYK", "KHYYKKKKKYYK", "KYYK....KYYK", "KYYK....KYYK",
     "KYYK....KYYK", "KYYK....KYYK", "KYYK....KYYK", "KGYYKKKKGYYK", "KMGYYYYYYGMK",
     "KBMMMMMMMMBK", ".KKKKKKKKKK."]
def stamp(rows, ox, oy):
    for j, row in enumerate(rows):
        for i, ch in enumerate(row):
            if ch in C: op[ox + i, oy + j] = C[ch] + (255,)
stamp(APOS, 290, 40)
stamp(D, 293, 53)
save_bmp(f'{out}/logo.bmp', logo.crop(BOX))
save_bmp(f'{out}/reference.bmp', final.crop(BOX))
print('box in 256 view:', BOX[0] - MARGIN, BOX[1], 'size', BOX[2] - BOX[0], BOX[3] - BOX[1])
