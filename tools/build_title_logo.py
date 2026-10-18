"""Build mods/title_logo/{logo,reference}.bmp for the title-logo overlay.

reference.bmp is the finished original logo exactly as the game draws it;
the overlay only appears while the frame matches it. logo.bmp replaces it.
Both cover the same box, at native resolution, in the 256-pixel view.

usage: build_title_logo.py BRUSH_FRAME TEXT_FRAME FINAL_FRAME TEXT_PNG OUT_DIR
  Native 398x224 screenshots of the title screen with only the brushstroke
  drawn, with the lettering added, and with the logo complete.

The original ball and trademark are kept pixel for pixel.
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
BG = fp[0, 0]                                                         # plain title background
ball = [[fp[x, y] != lp[x, y] and fp[x, y] != BG for x in range(W)]   # drawn after the lettering
        for y in range(H)]
for y in range(BOX[1], BOX[3]):
    for x in range(BOX[0], BOX[2]):
        if not ball[y][x] and lp[x, y] != bp[x, y]:
            op[x, y] = bp[x, y]                  # erase the original lettering
text = Image.open(text_png).convert('RGBA').resize(TEXT_SIZE, Image.LANCZOS)
text.putalpha(text.getchannel('A').point(lambda a: 255 if a >= 128 else 0))
tp = text.load()                                  # drop specks the shrink leaves
seen = set()
for y in range(TEXT_SIZE[1]):
    for x in range(TEXT_SIZE[0]):
        if not tp[x, y][3] or (x, y) in seen: continue
        group, todo = [], [(x, y)]
        seen.add((x, y))
        while todo:
            cx, cy = todo.pop(); group.append((cx, cy))
            for dx in (-1, 0, 1):
                for dy in (-1, 0, 1):
                    nx, ny = cx + dx, cy + dy
                    if 0 <= nx < TEXT_SIZE[0] and 0 <= ny < TEXT_SIZE[1] and \
                            tp[nx, ny][3] and (nx, ny) not in seen:
                        seen.add((nx, ny)); todo.append((nx, ny))
        if len(group) < 4:
            for gx, gy in group: tp[gx, gy] = (0, 0, 0, 0)
logo.alpha_composite(text, TEXT_AT)
for y in range(BOX[1], BOX[3]):                  # the ball stays in front
    for x in range(BOX[0], BOX[2]):
        if ball[y][x]: op[x, y] = fp[x, y]

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
