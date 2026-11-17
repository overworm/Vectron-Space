#!/usr/bin/env python3
"""gen_tables.py (v11, 8 bpp) - Super FX tables, true-colour textures and the 112-colour view palette.

View: 224x104 px, 112 wall columns of 2 px, 8 bpp bitmap (SCMR 8bpp, 128 rows), Mode 3 BG1.
Palette (CGRAM 0..111): 10 chroma ramps k-means'd from the stage textures + 4 accent ramps (amber, green, red, cyan), 8 light levels each.
  texel code = ramp * 8 + level; LUT[(side*8 + light) * 128 + code] -> palette index (the lighting);
  the same ramps light the floor and ceiling. Outputs: res/gpram.bin, layout.h/.inc, palette.h (bg_pal[9][112], sin8), bg_pal.bin, rowd.h
Reads res/tex_rgb.npz (gen_art.py), STAGE_JSON is applied afterwards by tint.py (brightness / hue)."""
import math, os, struct
import numpy as np

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "res")
NC, VH = 112, 104                     # wall columns (2 px each), view rows
L = dict(SIN=0x0200, DXT=0x0600, DYT=0x0A00, ANGO=0x0E00, COFF=0x0F00, REC=0x1000, MAP=0x3000, LUT=0x3800,
         ZBUF=0x4000, ROWT=0x4100, TEXW=0x4300, TEXF=0x7B00, GPEND=0x7F00)
BASE = 0x0200
NRAMP, NACC = 10, 4
LV = [0.06, 0.16, 0.28, 0.42, 0.57, 0.72, 0.86, 1.0]
ACC = [(1.0, 0.63, 0.10), (0.30, 0.90, 0.32), (1.0, 0.20, 0.16), (0.24, 0.88, 1.0)]
VPN = (NRAMP + NACC) * 8             # 112 view colours

s16 = lambda v: struct.pack("<h", int(v))
u16 = lambda v: struct.pack("<H", int(v) & 0xFFFF)
img = bytearray(L["GPEND"] - BASE)
def put(a, d): img[a - BASE:a - BASE + len(d)] = d

put(L["SIN"], b"".join(s16(round(math.sin(2 * math.pi * a / 512) * 32767)) for a in range(512)))
inv_abs = lambda x: 0x7FFF if abs(x) < 1e-6 else min(0x7FFF, round(256.0 / abs(x)))
put(L["DXT"], b"".join(u16(inv_abs(math.cos(2 * math.pi * a / 512))) for a in range(512)))
put(L["DYT"], b"".join(u16(inv_abs(math.sin(2 * math.pi * a / 512))) for a in range(512)))
ango, coff = [], []
for c in range(NC):
    xn = (c + 0.5 - NC / 2) / 64.0                     # focal 128 px (as the 256-wide game), 160 px window -> 64 degree fov
    ango.append(round(math.atan(xn) * 512 / (2 * math.pi))); coff.append(round(32767 / math.sqrt(1 + xn * xn)))
put(L["ANGO"], b"".join(s16(v) for v in ango)); put(L["COFF"], b"".join(u16(v) for v in coff))

def light_level(d): return max(0, min(7, 7 - int(d * 1.25)))
FOC = 128                                               # focal length in px (the sprites' projection assumes 128)
for idx in range(1024):
    perp = (idx * 8 + 4) / 256.0
    lh = min(1023, max(2, round(2.0 * FOC / perp))); vs = max(8, round(16384.0 / lh))
    put(L["REC"] + idx * 8, u16(lh) + u16(vs) + bytes([light_level(perp), 0, 0, 0]))

# ---------------------------------------------------------------- textures
T = np.load(os.path.join(OUT, "tex_rgb.npz"))
walls = T["walls"].astype(float) / 255.0                # (14,32,32,3)
floors = T["floors"].astype(float) / 255.0              # (4,16,16,3)
ACCSET = {(255, 160, 25): 0, (75, 230, 80): 1, (255, 50, 40): 2, (60, 225, 255): 3}
def accent_of(px255):
    return ACCSET.get(tuple(int(v) for v in px255), -1)

pix = np.concatenate([walls.reshape(-1, 3), np.repeat(floors.reshape(-1, 3), 3, axis=0)])
keep = np.array([accent_of(np.round(p * 255)) < 0 for p in pix])
pix = pix[keep]
mx = np.maximum(pix.max(axis=1), 1e-3); chroma = pix / mx[:, None]
rng = np.random.default_rng(1); cen = chroma[rng.choice(len(chroma), NRAMP, replace=False)]
for _ in range(30):
    d = ((chroma[:, None, :] - cen[None]) ** 2).sum(-1); lab = d.argmin(1)
    for k in range(NRAMP):
        m = lab == k
        if m.any(): cen[k] = (chroma[m] * mx[m, None]).sum(0) / mx[m].sum()
cen = np.clip(cen / np.maximum(cen.max(axis=1, keepdims=True), 1e-3), 0, 1)
ramps = list(cen) + [np.array(a) for a in ACC]
base = [(0, 0, 0)] * VPN
pal = np.zeros((VPN, 3))
for r in range(NRAMP + NACC):
    for l in range(8): pal[r * 8 + l] = np.clip(ramps[r] * LV[l], 0, 1)
pal[0] = 0
nearest_level = lambda v: int(np.argmin([abs(v - x) for x in LV]))
# CGRAM slots of the view colours: everything the HUD (BG2 palettes 1..3: 16..20, 24, 32..35, 48..51) and the sprites (128..255) do not use
SLOTS = list(range(0, 16)) + [21, 22, 23] + list(range(25, 32)) + list(range(36, 48)) + list(range(52, 128))
SLOTS = SLOTS[:VPN]
RUNS = []                                                  # (first colour, first CGRAM slot, count) of the contiguous runs
for p_, s_ in enumerate(SLOTS):
    if RUNS and RUNS[-1][1] + RUNS[-1][2] == s_: RUNS[-1][2] += 1
    else: RUNS.append([p_, s_, 1])

def encode(a):
    """(h, w, 3) float -> bytes, row-major [v][u]"""
    out = bytearray()
    for y in range(a.shape[0]):
        for x in range(a.shape[1]):
            px = a[y, x]; acc = accent_of(np.round(px * 255))
            if acc >= 0: out.append((NRAMP + acc) * 8 + 7); continue
            v = max(px.max(), 1e-3); ch = px / v
            out.append(int(((cen - ch) ** 2).sum(1).argmin()) * 8 + nearest_level(v))
    return bytes(out)

tex = bytearray()
for a in walls:                                         # u-major: the column of a texture is contiguous
    code = np.frombuffer(encode(a), dtype=np.uint8).reshape(32, 32)
    for cx in range(32): tex += bytes(code[:, cx])
put(L["TEXW"], bytes(tex))
fl = bytearray()
for a in floors: fl += encode(a)                        # [v][u], 16x16
put(L["TEXF"], bytes(fl))

# ---------------------------------------------------------------- LUT: [side][light][code]
lut = bytearray()
for side in range(2):
    for light in range(8):
        f = (0.18 + 0.82 * (light / 7.0) ** 1.3) * (0.82 if side else 1.0)
        for c in range(128):
            r, l = divmod(c, 8)
            if r >= NRAMP + NACC: lut.append(0); continue
            if r >= NRAMP: lut.append(SLOTS[r * 8 + (7 if light >= 3 else 5 + (light >= 1))])   # lamps keep glowing in the dark
            else: lut.append(SLOTS[r * 8 + nearest_level(LV[l] * f)])
put(L["LUT"], bytes(lut))

# ------------------------------------------------- floor / ceiling row table
# [D' u16][floor LUT addr u16][ceiling LUT addr u16][ceiling D' u16]
for k in range(VH // 2 + 1):
    kk = max(k, 1)
    lf = light_level(0.5 * FOC / kk); lc = max(0, light_level(1.5 * FOC / kk) - 1)
    put(L["ROWT"] + k * 8, u16(min(32767, round(131072.0 / kk))) + u16(L["LUT"] + lf * 128) + u16(L["LUT"] + lc * 128) + u16(min(32767, round(393216.0 / kk))))
open(os.path.join(OUT, "gpram.bin"), "wb").write(bytes(img))

# ---------------------------------------------------------------- palette variants (all of them keep the 112 entries)
rgb = [tuple(int(round(c * 31)) for c in p) for p in pal]
def rgb15(r, g, b): return (b << 10) | (g << 5) | r
def lum(r, g, b): return 0.3 * r + 0.59 * g + 0.11 * b
def tint(colors, mul, add):
    return [(max(0, min(31, int(r * mul[0] + add[0]))), max(0, min(31, int(g * mul[1] + add[1]))), max(0, min(31, int(b * mul[2] + add[2])))) for (r, g, b) in colors]
b0 = rgb
variants = [
    b0,
    tint(b0, (1.0, 0.98, 0.9), (7, 6, 2)),
    tint(b0, (1.25, 0.55, 0.55), (5, 0, 0)),
    tint(b0, (0.55, 0.2, 0.2), (1, 0, 0)),
    tint(b0, (1.15, 0.6, 0.6), (3, 0, 0)),
    tint(b0, (1.35, 0.75, 0.6), (3, 0, 0)),
    [(int(0.25 * 31 * (lum(r, g, b) / 31.0) ** 0.6), min(31, int(31 * (lum(r, g, b) / 31.0) ** 0.6 * 1.15) + 1), int(0.3 * 31 * (lum(r, g, b) / 31.0) ** 0.6)) for (r, g, b) in b0],
    [(min(31, int(31 * (lum(r, g, b) / 31.0) ** 0.8 * 1.15) + 1), min(31, int(31 * (lum(r, g, b) / 31.0) ** 0.8 * 1.0) + 1), int(0.25 * 31 * (lum(r, g, b) / 31.0) ** 0.8)) for (r, g, b) in b0],
    [(min(31, int(31 * (lum(r, g, b) / 31.0) ** 0.75 * 1.2) + 2), int(0.22 * 31 * (lum(r, g, b) / 31.0) ** 0.75), int(0.22 * 31 * (lum(r, g, b) / 31.0) ** 0.75)) for (r, g, b) in b0],
]
open(os.path.join(OUT, "bg_pal.bin"), "wb").write(b"".join(u16(rgb15(*c)) for c in b0))
with open(os.path.join(OUT, "palette.h"), "w") as f:
    f.write("/* generated by tools/gen_tables.py (8 bpp view) */\n#define BG_PAL_VARIANTS 9\n#define VPN %d\nstatic const u16 bg_pal[9][%d] = {\n" % (VPN, VPN))
    for v in variants:
        f.write("{" + ",".join("0x%04X" % rgb15(*c) for c in v) + "},\n")
    f.write("};\n")
    f.write("static const u8 vruns[%d][3] = {%s};\n" % (len(RUNS), ",".join("{%d,%d,%d}" % tuple(r_) for r_ in RUNS)))
    f.write("#define VRUN_N %d\n" % len(RUNS))
    f.write("static const s16 sin8[512] = {\n" + ",".join(str(round(math.sin(2 * math.pi * a / 512) * 256)) for a in range(512)) + "\n};\n")
with open(os.path.join(OUT, "rowd.h"), "w") as f:
    f.write("static const u16 rowd_f[128] = {%s};\n" % ",".join(str(min(32767, round(131072.0 / max(k, 1)))) for k in range(128)))
    f.write("static const u16 rowd_c[128] = {%s};\n" % ",".join(str(min(32767, round(393216.0 / max(k, 1)))) for k in range(128)))
with open(os.path.join(OUT, "layout.h"), "w") as f:
    f.write("/* generated by tools/gen_tables.py */\n#ifndef LAYOUT_H\n#define LAYOUT_H\n")
    for k, v in L.items(): f.write("#define GP_%s 0x%04X\n" % (k, v))
    f.write("#define GP_IMAGE_BASE 0x%04X\n#define GP_IMAGE_SIZE %d\n#define VIEW_W 224\n#define VIEW_H 104\n#define VIEW_COLS %d\n#endif\n" % (BASE, len(img), NC))
with open(os.path.join(OUT, "layout.inc"), "w") as f:
    for k, v in L.items(): f.write(".EQU %s $%04X\n" % (k, v))
print("gpram.bin %d bytes, end $%04X, %d palette colours" % (len(img), L["GPEND"], VPN))
