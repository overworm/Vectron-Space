#!/usr/bin/env python3
"""
gen_tables.py - builds the Game Pak RAM image for the GSU raycaster.

Output (into ../res):
  gpram.bin     bytes that the CPU copies to $70:0200 at boot
  layout.h      C defines of the GPRAM layout
  layout.inc    the same, as .EQU for the GSU assembler
  bg_pal.bin    BG1 palette (16 colours, the 3D view)

Texel code (4 bits):  n*2 + a
  n 0 flat, 1 bevel facing -u, 2 bevel facing +u, 3 bevel facing -v,
    4 bevel facing +v, 5 cyan lamp, 6 amber lamp, 7 recess (dark pit)
  a albedo variation (0 base metal, 1 lighter plate)
The LUT turns (light set, texel) into a palette index. The "normal" bevels are
lit by a fixed direction per wall orientation, so panel edges, rivets and
pipes shade differently on X and Y facing walls: pseudo normal mapping.
"""
import math, os, struct, sys

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "res")
os.makedirs(OUT, exist_ok=True)

# ----------------------------------------------------------------- layout
L = dict(
    SIN=0x0200, DXT=0x0600, DYT=0x0A00, ANGO=0x0E00, COFF=0x0F00,
    REC=0x1000, MAP=0x3000, LUT=0x3800, ZBUF=0x3C00, ROWT=0x3D00,
    TEXW=0x4100, TEXF=0x7900, GPEND=0x7D00, WIDB=0x7D00,
)
NUM_COLS = 128
NUM_ROWS = 128
BASE = 0x0200


def s16(v):
    return struct.pack("<h", int(v))


def u16(v):
    return struct.pack("<H", int(v) & 0xFFFF)


img = bytearray(L["GPEND"] - BASE)


def put(addr, data):
    img[addr - BASE: addr - BASE + len(data)] = data


# ------------------------------------------------------------- sine tables
sin_t = [round(math.sin(2 * math.pi * a / 512) * 32767) for a in range(512)]
put(L["SIN"], b"".join(s16(v) for v in sin_t))


def inv_abs(x):
    if abs(x) < 1e-6:
        return 0x7FFF
    return min(0x7FFF, round(256.0 / abs(x)))


dx_t, dy_t = [], []
for a in range(512):
    c = math.cos(2 * math.pi * a / 512)
    s = math.sin(2 * math.pi * a / 512)
    dx_t.append(inv_abs(c))
    dy_t.append(inv_abs(s))
put(L["DXT"], b"".join(u16(v) for v in dx_t))
put(L["DYT"], b"".join(u16(v) for v in dy_t))

# per column: angle offset (units of 1/512 turn) and fish-eye cosine
ango, coff = [], []
for col in range(NUM_COLS):
    xn = (col + 0.5 - NUM_COLS / 2) / (NUM_COLS / 2)       # -1..1, 90 degree fov
    ango.append(round(math.atan(xn) * 512 / (2 * math.pi)))
    coff.append(round(32767 / math.sqrt(1 + xn * xn)))
put(L["ANGO"], b"".join(s16(v) for v in ango))
put(L["COFF"], b"".join(u16(v) for v in coff))

# ------------------------------------- wall records, one per perp>>3 (8 bytes)
# [line height u16][texture step 8.8 u16][light level u8][pad x3]
def light_level(dist_tiles):
    return max(0, min(7, 7 - int(dist_tiles * 1.25)))


for idx in range(1024):
    perp = (idx * 8 + 4) / 256.0                              # tiles
    lh = min(1023, max(2, round(256.0 / perp)))      # wall is 2 units tall, eye at 0.5
    vs = max(8, round(16384.0 / lh))
    rec = u16(lh) + u16(vs) + bytes([light_level(perp), 0, 0, 0])
    put(L["REC"] + idx * 8, rec)

# ---------------------------------------------------------------- lookup LUT
# sets: 0-7 wall X faces, 8-15 wall Y faces, 16-23 floor, 24-31 ceiling
LIGHTS = {0: (-0.65, -0.75), 1: (0.65, -0.75), 2: (0.0, -1.0), 3: (0.0, 1.0)}
TILT = {1: (-1, 0), 2: (1, 0), 3: (0, -1), 4: (0, 1)}


def lut_entry(kind, light, c):
    """texel class c (see gen_art.py) -> palette index 1..15"""
    lit = light / 7.0
    if c >= 25:                                      # emissive accents: amber, green, red, cyan
        acc = c - 25
        if acc > 3:
            return 1
        return 12 + acc if (light >= 3 or (acc == 3 and light >= 1)) else 2 + light
    tilt, luma = c // 5, c % 5
    alb = luma / 4.0
    base = 1.0 + 7.4 * (0.03 + 0.97 * lit ** 1.6) * (0.25 + 0.75 * alb)
    if tilt in TILT:
        lu, lv = LIGHTS[kind]
        tu, tv = TILT[tilt]
        base += 1.7 * (tu * lu + tv * lv) * (0.3 + 0.7 * lit)
    return max(1, min(11, int(round(base))))


LIGHTS = {0: (-0.65, -0.75), 1: (0.65, -0.75), 2: (0.0, -1.0), 3: (0.0, 1.0)}
TILT = {1: (-1, 0), 2: (1, 0), 3: (0, -1), 4: (0, 1)}
STRIDE = 32

lut = bytearray()
for s_ in range(32):
    kind = s_ // 8
    light = s_ % 8
    for t in range(STRIDE):
        lut.append(lut_entry(kind, light, t))
put(L["LUT"], bytes(lut))

# ------------------------------------------------- floor / ceiling row table
# [D' u16][floor LUT addr u16][ceiling LUT addr u16][pad u16]
for k in range(NUM_ROWS):
    kk = max(k, 1)
    dprime = min(32767, round(131072.0 / kk))
    d_tiles = 64.0 / kk
    lf = light_level(d_tiles)
    dc = 192.0 / kk
    lc = max(0, light_level(dc) - 1)
    row = (u16(dprime) + u16(L["LUT"] + (16 + lf) * STRIDE) +
           u16(L["LUT"] + (24 + lc) * STRIDE) + u16(min(32767, round(393216.0 / kk))))
    put(L["ROWT"] + k * 8, row)

with open(os.path.join(OUT, "rowd.h"), "w") as f:
    f.write("static const u16 rowd_f[128] = {%s};\n" % ",".join(str(min(32767, round(131072.0 / max(k, 1)))) for k in range(128)))
    f.write("static const u16 rowd_c[128] = {%s};\n" % ",".join(str(min(32767, round(393216.0 / max(k, 1)))) for k in range(128)))

# ----------------------------------------------------------------- textures
# built from the concept art by tools/gen_art.py (run it first)
put(L["TEXW"], open(os.path.join(OUT, "tex_walls.bin"), "rb").read())
put(L["TEXF"], open(os.path.join(OUT, "tex_floor.bin"), "rb").read())

open(os.path.join(OUT, "gpram.bin"), "wb").write(bytes(img))

# --------------------------------------------------------------- palette (BG1)
def rgb15(r, g, b):
    return (b << 10) | (g << 5) | r


ramp = []
for i in range(11):
    f = i / 10.0
    DIM = 0.62                                   # the level is dark: the brightest wall is 62 % of the old ramp
    r = int((1 + 17 * f ** 1.3) * DIM)
    g = int((2 + 21 * f ** 1.25) * DIM)
    b = int((4 + 24 * f ** 1.15) * DIM)
    ramp.append((min(r, 31), min(g, 31), min(b, 31)))
pal = [rgb15(0, 0, 0)] + [rgb15(*c) for c in ramp] + [
    rgb15(31, 19, 3),    # 12 amber
    rgb15(9, 28, 10),    # 13 green
    rgb15(31, 6, 5),     # 14 red
    rgb15(7, 27, 31),    # 15 cyan
]
open(os.path.join(OUT, "bg_pal.bin"), "wb").write(b"".join(u16(v) for v in pal))

def tint(colors, mul, add):
    out = []
    for (r, g, b) in colors:
        out.append((max(0, min(31, int(r * mul[0] + add[0]))),
                    max(0, min(31, int(g * mul[1] + add[1]))),
                    max(0, min(31, int(b * mul[2] + add[2])))))
    return out


def lum(r, g, b):
    return 0.3 * r + 0.59 * g + 0.11 * b


base = [(0, 0, 0)] + ramp + [(31, 19, 3), (9, 28, 10), (31, 6, 5), (7, 27, 31)]
variants = [
    base,                                              # 0 normal
    tint(base, (1.0, 0.98, 0.9), (7, 6, 2)),           # 1 muzzle flash
    tint(base, (1.25, 0.55, 0.55), (5, 0, 0)),         # 2 hurt
    tint(base, (0.55, 0.2, 0.2), (1, 0, 0)),           # 3 death
    tint(base, (1.15, 0.6, 0.6), (3, 0, 0)),           # 4 alarm
    tint(base, (1.35, 0.75, 0.6), (3, 0, 0)),          # 5 super attack: warm red
    [(int(0.25 * 31 * (lum(r, g_, b) / 31.0) ** 0.6), min(31, int(31 * (lum(r, g_, b) / 31.0) ** 0.6 * 1.15) + 1), int(0.3 * 31 * (lum(r, g_, b) / 31.0) ** 0.6)) for (r, g_, b) in base],   # 6 night vision: green, lifts the dark
    [(min(31, int(31 * (lum(r, g_, b) / 31.0) ** 0.8 * 1.15) + 1), min(31, int(31 * (lum(r, g_, b) / 31.0) ** 0.8 * 1.0) + 1), int(0.25 * 31 * (lum(r, g_, b) / 31.0) ** 0.8)) for (r, g_, b) in base],   # 7 optovisor: yellow
    [(min(31, int(31 * (lum(r, g_, b) / 31.0) ** 0.75 * 1.2) + 2), int(0.22 * 31 * (lum(r, g_, b) / 31.0) ** 0.75), int(0.22 * 31 * (lum(r, g_, b) / 31.0) ** 0.75)) for (r, g_, b) in base],   # 8 redvisor: red
]
# ---- wall colour groups: the source textures are almost grey, so every wall id gets a hue (a Doom-like mix)
GROUP_TINT = [None, (1.55, 0.92, 0.55),    # 1 rust
              (0.70, 1.22, 0.82),          # 2 green panel
              (0.78, 0.95, 1.40),          # 3 steel blue
              (1.35, 1.18, 0.55)]          # 4 khaki / hazard yellow
WALL_GRP = [0, 0, 4, 2, 1, 3, 0, 1, 4, 3, 0, 0, 0, 0, 0, 0]      # wall id -> group (0 = base palette)
_sj = {}
try:
    import json as _json
    _p = os.environ.get("STAGE_JSON")
    if _p and os.path.exists(_p):
        _sj = _json.load(open(_p))
except Exception:
    pass
for _k, _g in (_sj.get("wall_group") or {}).items():
    if 1 <= int(_k) <= 15 and 0 <= int(_g) <= 4:
        WALL_GRP[int(_k)] = int(_g)
for _k, _t in (_sj.get("group_tint") or {}).items():
    if 1 <= int(_k) <= 4:
        GROUP_TINT[int(_k)] = tuple(float(x) for x in _t)


# v11.2: groups from the real textures: every wall id has a colour curve (gen_art.py -> res/wall_curves.json);
# the 14 curves are clustered into the 4 groups, a group's palette is the mean curve of its walls
import json as _json2
GROUP_CURVE = [None] * 5
_cp = os.path.join(OUT, "wall_curves.json")
if os.path.exists(_cp):
    _cur = _json2.load(open(_cp))
    import random as _rnd
    _vec = [[v for st in (3, 5, 7, 9) for v in c[st]] for c in _cur]

    def _d2(a, b):
        return sum((x - y) ** 2 for x, y in zip(a, b))
    _rnd.seed(3)
    _cen = [_vec[i] for i in (_rnd.sample(range(len(_vec)), 4))]
    for _ in range(30):                                          # k-means, k = 4
        _asg = [min(range(4), key=lambda k: _d2(v, _cen[k])) for v in _vec]
        for k in range(4):
            mem = [_vec[i] for i in range(len(_vec)) if _asg[i] == k]
            if mem:
                _cen[k] = [sum(x) / len(mem) for x in zip(*mem)]
    for i, k in enumerate(_asg):
        WALL_GRP[i + 1] = k + 1
    for k in range(4):
        mem = [_cur[i] for i in range(len(_cur)) if _asg[i] == k]
        if mem:
            GROUP_CURVE[k + 1] = [[sum(m[st][ch] for m in mem) / len(mem) for ch in range(3)] for st in range(11)]
    for _k, _g in (_sj.get("wall_group") or {}).items():          # stage.json overrides
        if 1 <= int(_k) <= 15 and 0 <= int(_g) <= 4:
            WALL_GRP[int(_k)] = int(_g)
    print("wall groups:", WALL_GRP[1:15])


def group_base(g):
    if g == 0:
        return base
    if GROUP_CURVE[g] and not (_sj.get("group_tint") or {}).get(str(g)):
        GAIN = 2.5
        out = [(0, 0, 0)]
        for i, (r, g_, b) in enumerate(ramp):
            L_ = lum(r, g_, b)
            cr = GROUP_CURVE[g][i]
            rr, gg, bb = [1.0 + (x - 1.0) * GAIN for x in cr]
            kk = max(1e-3, lum(rr, gg, bb))
            out.append(tuple(max(0, min(31, int(L_ * x / kk + 0.5))) for x in (rr, gg, bb)))
        return out + base[12:]
    tr, tg, tb = GROUP_TINT[g]
    k = lum(tr, tg, tb)
    out = [(0, 0, 0)]
    for (r, g_, b) in ramp:
        L_ = lum(r, g_, b)
        out.append((min(31, int(L_ * tr / k + 0.5)), min(31, int(L_ * tg / k + 0.5)), min(31, int(L_ * tb / k + 0.5))))
    return out + base[12:]


def make_variants(b):
    return [
        b,
        tint(b, (1.0, 0.98, 0.9), (7, 6, 2)),
        tint(b, (1.25, 0.55, 0.55), (5, 0, 0)),
        tint(b, (0.55, 0.2, 0.2), (1, 0, 0)),
        tint(b, (1.15, 0.6, 0.6), (3, 0, 0)),
        tint(b, (1.35, 0.75, 0.6), (3, 0, 0)),
        [(int(0.25 * 31 * (lum(r, g_, bb) / 31.0) ** 0.6), min(31, int(31 * (lum(r, g_, bb) / 31.0) ** 0.6 * 1.15) + 1), int(0.3 * 31 * (lum(r, g_, bb) / 31.0) ** 0.6)) for (r, g_, bb) in b],
        [(min(31, int(31 * (lum(r, g_, bb) / 31.0) ** 0.8 * 1.15) + 1), min(31, int(31 * (lum(r, g_, bb) / 31.0) ** 0.8 * 1.0) + 1), int(0.25 * 31 * (lum(r, g_, bb) / 31.0) ** 0.8)) for (r, g_, bb) in b],
        [(min(31, int(31 * (lum(r, g_, bb) / 31.0) ** 0.75 * 1.2) + 2), int(0.22 * 31 * (lum(r, g_, bb) / 31.0) ** 0.75), int(0.22 * 31 * (lum(r, g_, bb) / 31.0) ** 0.75)) for (r, g_, bb) in b],
    ]


_gv = [make_variants(group_base(g)) for g in range(5)]
variants_g = [[_gv[g][v] for g in range(5)] for v in range(9)]
assert variants_g[0][0] == variants[0]

with open(os.path.join(OUT, "palette.h"), "w") as f:
    f.write("/* generated by tools/gen_tables.py */\n#define BG_PAL_VARIANTS 9\nstatic const u16 bg_pal[9][16] = {\n")
    for v in variants:
        f.write("{" + ",".join("0x%04X" % rgb15(*c) for c in v) + "},\n")
    f.write("};\n")
    # wall palette groups 1..4 (CGRAM 64..127, BG1 palettes 4..7), one block of 4x16 per variant; group 0 is bg_pal
    f.write("static const u16 bg_palg[9][64] = {\n")
    for v in range(9):
        cols = []
        for g in range(1, 5):
            cols += variants_g[v][g]
        f.write("{" + ",".join("0x%04X" % rgb15(*c) for c in cols) + "},\n")
    f.write("};\n")
    f.write("static const u8 wall_grp[16] = {%s};\n" % ",".join(str(x) for x in WALL_GRP))
    f.write("static const s16 sin8[512] = {\n")
    f.write(",".join(str(round(math.sin(2 * math.pi * a / 512) * 256)) for a in range(512)) + "\n};\n")

with open(os.path.join(OUT, "layout.h"), "w") as f:
    f.write("/* generated by tools/gen_tables.py */\n#ifndef LAYOUT_H\n#define LAYOUT_H\n")
    for k, v in L.items():
        f.write("#define GP_%s 0x%04X\n" % (k, v))
    f.write("#define GP_IMAGE_BASE 0x%04X\n#define GP_IMAGE_SIZE %d\n" % (BASE, len(img)))
    f.write("#endif\n")
with open(os.path.join(OUT, "layout.inc"), "w") as f:
    for k, v in L.items():
        f.write(".EQU %s $%04X\n" % (k, v))
print("gpram.bin %d bytes, end $%04X" % (len(img), L["GPEND"]))
