"""Build mods/title_logo/credit{,_reference,_behind}.bmp: the title screen's
"LICENSED BY NINTENDO" line replaced by the MABEC GAMES, 2026 banner. The
Konami line above it stays.

usage: build_title_credit.py FULL_FRAME BG_ONLY_FRAME BANNER_PNG OUT_DIR
  FULL_FRAME / BG_ONLY_FRAME: native 4:3 (256x224) captures of the finished
  title screen, as drawn and with sprites hidden (SNESRECOMP_LAYER_MASK=15).
  BANNER_PNG: the banner at native pixel size (assets/launcher/credit_banner.png,
  sampled from img/MABEC GAMES 2026 Pixel Banner.png at its 15.4 px grid).
"""
import struct, sys
from PIL import Image

BOX = (8, 213, 248, 223)          # the second credit line, in the 256-wide view
LINE2 = 214                       # its top row

def save_bmp(path, im):
    im = im.convert('RGBA'); w, h = im.size
    px = b''.join(struct.pack('<BBBB', b, g, r, a) for r, g, b, a in im.get_flattened_data())
    head = struct.pack('<2sIHHI', b'BM', 54 + len(px), 0, 0, 54)
    info = struct.pack('<IiiHHIIiiII', 40, w, -h, 1, 32, 0, len(px), 2835, 2835, 0, 0)
    open(path, 'wb').write(head + info + px)

full_f, bg_f, banner_f, out = sys.argv[1:5]
full = Image.open(full_f).convert('RGB').crop(BOX)
behind = Image.open(bg_f).convert('RGB').crop(BOX)
banner = Image.open(banner_f).convert('RGBA')
new = Image.new('RGBA', full.size, (0, 0, 0, 0))     # only the banner's own pixels
new.alpha_composite(banner, ((full.width - banner.width) // 2, LINE2 - BOX[1]))
save_bmp(f'{out}/credit.bmp', new)
save_bmp(f'{out}/credit_reference.bmp', full)
save_bmp(f'{out}/credit_behind.bmp', behind)
print('credit box', BOX, 'banner', banner.size)
