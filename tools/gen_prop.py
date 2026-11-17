# Parked-bike prop sprites for the pre-chase cut-scene, built from the artist's sheet.
import sys, numpy as np
from PIL import Image
SRC = sys.argv[1]
sys.path.insert(0, 'tools')
im = Image.open(SRC).convert('RGB')
a = np.array(im).astype(int)
BOX = {'A': (125, 40, 227, 169), 'B': (266, 183, 399, 330), 'C': (132, 214, 239, 328), 'D': (8, 40, 73, 136)}
def crop(k):
    x0, y0, x1, y1 = BOX[k]
    c = im.crop((x0, y0, x1, y1)).convert('RGBA')
    p = c.load()
    for y in range(c.height):
        for x in range(c.width):
            r, g, b, _ = p[x, y]
            if r + g + b >= 700: p[x, y] = (0, 0, 0, 0)
    bb = c.getchannel('A').point(lambda v: 255 if v else 0).getbbox()
    return c.crop(bb)
SRCS = {k: crop(k) for k in BOX}
# (canvas, source, height)
PLAN = [(64, 'A', 62), (64, 'B', 52), (64, 'C', 44), (32, 'A', 30), (32, 'B', 25), (32, 'C', 21),
        (16, 'D', 15), (16, 'D', 12), (16, 'D', 10), (16, 'D', 8)]
pics = []
for cv, k, h in PLAN:
    s = SRCS[k]
    w = max(2, round(s.width * h / s.height))
    r = s.resize((w, h), Image.LANCZOS)
    c = Image.new('RGBA', (cv, cv), (0, 0, 0, 0))
    c.paste(r, ((cv - w) // 2, cv - h))
    pics.append(c)
# shared 15-colour palette
mont = Image.new('RGB', (sum(p.width for p in pics), 64), (0, 0, 0))
x = 0
for p in pics:
    m = Image.new('RGB', p.size, (0, 0, 0)); m.paste(p.convert('RGB'), mask=p.getchannel('A').point(lambda v: 255 if v > 127 else 0))
    mont.paste(m, (x, 0)); x += p.width
q = mont.quantize(15, Image.MEDIANCUT)
pal = q.getpalette()[:45]
cols = [(0, 0, 0)] + [tuple(pal[i*3:i*3+3]) for i in range(15)]
def idx(rgb):
    return min(range(1, 16), key=lambda i: sum((rgb[j] - cols[i][j]) ** 2 for j in range(3)))
def to_tiles(img):
    w, h = img.size; px = img.load(); out = bytearray()
    for ty in range(h // 8):
        for tx in range(w // 8):
            b = bytearray(32)
            for y in range(8):
                p0 = p1 = p2 = p3 = 0
                for x in range(8):
                    c = px[tx*8+x, ty*8+y]; bit = 7 - x
                    p0 |= (c & 1) << bit; p1 |= ((c >> 1) & 1) << bit; p2 |= ((c >> 2) & 1) << bit; p3 |= ((c >> 3) & 1) << bit
                b[y*2] = p0; b[y*2+1] = p1; b[16+y*2] = p2; b[16+y*2+1] = p3
            out += b
    return bytes(out)
blobs = {64: b'', 32: b'', 16: b''}
for p in pics:
    ind = Image.new('P', p.size, 0); ip = ind.load(); sp = p.load()
    for y in range(p.height):
        for x in range(p.width):
            r, g, b, al = sp[x, y]
            ip[x, y] = idx((r, g, b)) if al > 127 else 0
    blobs[p.width] += to_tiles(ind)
data = blobs[64] + blobs[32] + blobs[16]
assert len(data) == 3*2048 + 3*512 + 4*128, len(data)
open('res/spr_prop.bin', 'wb').write(data)
cols = [tuple(min(255, int(v * 1.45)) for v in c) for c in cols]
bgr = [((c[2] >> 3) << 10) | ((c[1] >> 3) << 5) | (c[0] >> 3) for c in cols]
open('res/prop_pal.h', 'w').write('static const u16 prop_pal[16] = {' + ','.join('0x%04X' % v for v in bgr) + '};\n')
# preview
pv = Image.new('RGB', (64*3 + 32*3 + 16*4 + 20, 64), (60, 90, 60)); x = 0
for p in pics:
    pv.paste(p.convert('RGB'), (x, 0), p.getchannel('A').point(lambda v: 255 if v > 127 else 0)); x += p.width + 2
pv.resize((pv.width * 3, pv.height * 3), Image.NEAREST).save('/tmp/prop_prev.png')
