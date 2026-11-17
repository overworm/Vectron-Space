#!/usr/bin/env python3
"""gen_hbar.py - the bike handlebars (left / straight / right) -> res/spr_hbar.bin (3 x 2048 B, same layout as a weapon frame:
a 64x64 tile canvas whose four 32x32 quarters are the four sprites of a 128x32 strip) and res/hbar_pal.h"""
import os, numpy as np
from PIL import Image
from scipy import ndimage
HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "art_src", "new")
OUT = os.path.join(HERE, "..", "res")
W, H = 128, 32


def cut(name):
    im = Image.open(os.path.join(SRC, name)).convert("RGB")
    a = np.asarray(im).astype(int)
    light = (a.min(axis=2) > 205) & ((a.max(axis=2) - a.min(axis=2)) < 28)
    lab, n = ndimage.label(light)
    edge = set(lab[0, :]) | set(lab[-1, :]) | set(lab[:, 0]) | set(lab[:, -1])
    bg = np.isin(lab, [e for e in edge if e])
    rgba = np.dstack([a, np.where(bg, 0, 255)]).astype(np.uint8)
    img = Image.fromarray(rgba, "RGBA")
    return img.crop(img.getbbox())


def fit(img):
    sc = min(W / img.width, H / img.height)
    w, h = max(1, round(img.width * sc)), max(1, round(img.height * sc))
    s = img.convert("RGBa").resize((w, h), Image.LANCZOS).convert("RGBA")
    cv = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    cv.paste(s, ((W - w) // 2, H - h), s)
    return cv


def to_tiles(idx):                                   # idx: 64x64 uint8 -> 64 tiles of 4bpp, 8 per row
    out = bytearray()
    for ty in range(8):
        for tx in range(8):
            b = bytearray(32)
            for y in range(8):
                p = [0, 0, 0, 0]
                for x in range(8):
                    c = int(idx[ty * 8 + y, tx * 8 + x]) & 15
                    for k in range(4):
                        p[k] |= ((c >> k) & 1) << (7 - x)
                b[y * 2], b[y * 2 + 1], b[16 + y * 2], b[16 + y * 2 + 1] = p
            out += b
    return bytes(out)


frames = [fit(cut(n)) for n in ("hbar_left.png", "hbar_straight.png", "hbar_right.png")]
rgb = np.concatenate([np.asarray(f)[..., :3] for f in frames], axis=0)
al = np.concatenate([np.asarray(f)[..., 3] > 100 for f in frames], axis=0)
fill = rgb[al].mean(axis=0) if al.any() else (0, 0, 0)
rgb2 = np.where(al[..., None], rgb, fill).astype(np.uint8)
q = Image.fromarray(rgb2).quantize(colors=15, method=Image.MEDIANCUT, dither=Image.NONE)
pal = np.array(q.getpalette()[:45]).reshape(15, 3)
qi = np.asarray(q).astype(np.uint8) + 1
qi = np.where(al, qi, 0).astype(np.uint8)
data = b""
prev = []
for k in range(3):
    part = qi[k * H:(k + 1) * H]
    cv = np.zeros((64, 64), np.uint8)
    for q4 in range(4):
        cv[(q4 // 2) * 32:(q4 // 2) * 32 + 32, (q4 % 2) * 32:(q4 % 2) * 32 + 32] = part[:, q4 * 32:q4 * 32 + 32]
    data += to_tiles(cv)
    prev.append(part)
open(os.path.join(OUT, "spr_hbar.bin"), "wb").write(data)
cols = [0] + [((int(c[2]) >> 3) << 10) | ((int(c[1]) >> 3) << 5) | (int(c[0]) >> 3) for c in pal]
open(os.path.join(OUT, "hbar_pal.h"), "w").write("static const u16 hbar_pal[16] = {%s};\n" % ",".join("0x%04X" % c for c in cols))
sheet = Image.new("RGB", (W, H * 3 + 4), (90, 120, 160))
for k in range(3):
    p = np.zeros((H, W, 3), np.uint8)
    for y in range(H):
        for x in range(W):
            if prev[k][y, x]: p[y, x] = pal[prev[k][y, x] - 1]
            else: p[y, x] = (90, 120, 160)
    sheet.paste(Image.fromarray(p), (0, k * (H + 2)))
sheet.resize((W * 4, (H * 3 + 4) * 4), Image.NEAREST).save(os.path.join(HERE, "art_src", "hbar_preview.png"))
print("hbar ok", len(data))
