#!/usr/bin/env python3
"""
gen_sprites.py - procedural pixel art for the raycaster prototype.

Everything is drawn with indexed colour (no anti-aliasing) and converted to
SNES 4bpp planar tiles.  Replace the PNG contact sheets / these functions with
hand-made art later; the sizes, frame order and VRAM layout stay the same.

Outputs in ../res:
  spr_enemy.bin   22 frames x [near 32x64 (1024) | far 16x32 (256) | tiny 16x16 (128)]
  spr_weapon.bin  3 frames of the blaster, 64x64 (2048 each)
  spr_obj1.bin    OBJ name table 1 (8192): HUD font, crosshair, big plasma
  spr_fx0.bin     fx region of OBJ table 0 (cols 8-15, rows 8-15), 8 rows x 256
  boss.bin        boss tiles for BG2 (3 sizes x 3 frames)
  spr_pal.bin     4 OBJ palettes (enemy, weapon, fx, enemy-hurt) + boss palette
  spr_defs.h      C defines
  sheets/*.png    contact sheets for checking the art
"""
import math, os, struct
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "res")
SHEETS = os.path.join(OUT, "sheets")
os.makedirs(SHEETS, exist_ok=True)


def rgb(r, g, b):
    return (r, g, b)


# ------------------------------------------------------------------ palettes
PAL_ENEMY = [
    (0, 0, 0), (3, 3, 6), (6, 6, 10), (10, 10, 16), (17, 18, 24), (28, 3, 3),
    (31, 20, 14), (5, 8, 7), (12, 12, 14), (21, 22, 25), (31, 28, 8),
    (31, 31, 31), (14, 10, 8), (28, 14, 4), (13, 2, 3), (2, 2, 4)]
PAL_WEAPON = [
    (0, 0, 0), (3, 2, 3), (5, 5, 7), (9, 9, 12), (18, 8, 2), (26, 14, 3),
    (31, 24, 8), (8, 9, 12), (14, 15, 19), (23, 24, 28), (4, 24, 26),
    (18, 31, 31), (31, 31, 31), (31, 29, 10), (31, 16, 4), (6, 10, 6)]
PAL_FX = [
    (0, 0, 0), (2, 2, 4), (31, 31, 31), (14, 2, 2), (28, 5, 4), (31, 20, 8),
    (31, 28, 8), (3, 12, 4), (8, 26, 10), (22, 31, 22), (4, 22, 28),
    (18, 31, 31), (31, 31, 28), (6, 12, 30), (13, 14, 16), (31, 22, 6)]
PAL_BOSS = [
    (0, 0, 0), (2, 2, 5), (6, 7, 10), (11, 12, 17), (19, 21, 27), (3, 6, 10),
    (6, 24, 28), (20, 31, 31), (30, 4, 4), (6, 30, 10), (29, 14, 3),
    (31, 28, 12), (31, 31, 31), (9, 6, 8), (30, 25, 4), (3, 3, 3)]


def hurt_pal(p):
    out = []
    for i, (r, g, b) in enumerate(p):
        if i == 0:
            out.append((0, 0, 0))
        else:
            out.append((min(31, 16 + r // 2 + 4), min(31, 14 + g // 2), min(31, 14 + b // 2)))
    return out


def c15(c):
    r, g, b = c
    return (b << 10) | (g << 5) | r


def pal_bytes(p):
    return b"".join(struct.pack("<H", c15(c)) for c in p)


# --------------------------------------------------------------- tile helpers
def to_tiles(img):
    """indexed image (w,h multiple of 8) -> list of 32-byte tiles, row-major."""
    w, h = img.size
    px = img.load()
    tiles = []
    for ty in range(h // 8):
        for tx in range(w // 8):
            b = bytearray(32)
            for y in range(8):
                p0 = p1 = p2 = p3 = 0
                for x in range(8):
                    c = px[tx * 8 + x, ty * 8 + y] & 15
                    bit = 7 - x
                    p0 |= (c & 1) << bit
                    p1 |= ((c >> 1) & 1) << bit
                    p2 |= ((c >> 2) & 1) << bit
                    p3 |= ((c >> 3) & 1) << bit
                b[y * 2] = p0
                b[y * 2 + 1] = p1
                b[16 + y * 2] = p2
                b[16 + y * 2 + 1] = p3
            tiles.append(bytes(b))
    return tiles


def tile_rows(img):
    """bytes of tile rows (each row = all tiles of that row)."""
    w, h = img.size
    t = to_tiles(img)
    return t, w // 8, h // 8


def newimg(w, h):
    return Image.new("P", (w, h), 0)


def downscale(img):
    """2x2 -> 1 keeping the most common opaque colour (outline survives)."""
    w, h = img.size
    out = newimg(w // 2, h // 2)
    s, d = img.load(), out.load()
    for y in range(h // 2):
        for x in range(w // 2):
            cnt = {}
            for dy in (0, 1):
                for dx in (0, 1):
                    c = s[x * 2 + dx, y * 2 + dy]
                    if c:
                        cnt[c] = cnt.get(c, 0) + 1
            if sum(cnt.values()) >= 2:
                # prefer the colour with most votes; ties -> brighter detail
                best = max(cnt.items(), key=lambda kv: (kv[1], kv[0] in (5, 6, 10)))
                d[x, y] = best[0]
    return out


# ---------------------------------------------------------------- soldier art
def ik(hip, foot, l1, l2, bend):
    """two-bone IK in 2D, returns knee. bend=+1/-1 chooses the side."""
    hx, hy = hip
    fx, fy = foot
    dx, dy = fx - hx, fy - hy
    d = math.hypot(dx, dy)
    d = min(d, l1 + l2 - 0.01)
    d = max(d, abs(l1 - l2) + 0.01)
    a = (l1 * l1 - l2 * l2 + d * d) / (2 * d)
    h = math.sqrt(max(0.0, l1 * l1 - a * a))
    ux, uy = dx / math.hypot(dx, dy), dy / math.hypot(dx, dy)
    kx = hx + ux * a + (-uy) * h * bend
    ky = hy + uy * a + (ux) * h * bend
    return (kx, ky)


def soldier(p):
    im = newimg(32, 64)
    d = ImageDraw.Draw(im)
    dy = p.get("dy", 0)                  # body drop (crouch)
    lean = p.get("lean", 0)              # head/shoulder shift in x
    tilt = p.get("tilt", 0)
    ground = 62

    def line(a, b, w, c):
        d.line([a, b], fill=c, width=w)

    def poly(pts, c, o=1):
        d.polygon(pts, fill=c, outline=o)

    hipy = 34 + dy
    lhip, rhip = (12, hipy), (20, hipy)
    # legs
    for hip, foot, bend in ((lhip, p.get("lf", (11, ground)), -1),
                            (rhip, p.get("rf", (21, ground)), 1)):
        k = ik(hip, foot, 12, 13, bend * p.get("kneeb", 1))
        line(hip, k, 5, 1)
        line(k, foot, 5, 1)
        line(hip, k, 3, 2)
        line(k, foot, 3, 3)
        d.rectangle([foot[0] - 3, foot[1] - 2, foot[0] + 3, foot[1] + 1], fill=7, outline=1)
        d.point((int(k[0]), int(k[1])), fill=4)      # kneepad
    # torso
    ty = 16 + dy
    poly([(8 + lean, ty), (24 + lean, ty), (23 + lean // 2, hipy), (9 + lean // 2, hipy)], 3)
    d.rectangle([11 + lean, ty + 2, 21 + lean, ty + 10], fill=4)
    d.rectangle([13 + lean, ty + 4, 19 + lean, ty + 7], fill=3)
    d.line([(14 + lean, ty + 3), (18 + lean, ty + 3)], fill=11)
    for x in range(10, 23, 3):
        d.point((x + lean // 2, hipy - 2), fill=13)
    d.line([(9 + lean // 2, hipy - 1), (23 + lean // 2, hipy - 1)], fill=1)
    # head
    hy = 4 + dy + p.get("hdy", 0)
    hx = lean + p.get("hdx", 0)
    poly([(11 + hx, hy), (21 + hx, hy), (22 + hx, hy + 5), (21 + hx, hy + 11),
          (11 + hx, hy + 11), (10 + hx, hy + 5)], 2)
    d.line([(12 + hx, hy + 1), (20 + hx, hy + 1)], fill=4)
    d.rectangle([11 + hx, hy + 4, 21 + hx, hy + 6], fill=1)
    for ex in (12, 18):
        d.rectangle([ex + hx, hy + 4, ex + 2 + hx, hy + 5], fill=5)
        d.point((ex + 1 + hx, hy + 4), fill=6)
    d.rectangle([13 + hx, hy + 8, 19 + hx, hy + 10], fill=8, outline=1)
    d.line([(14 + hx, hy + 9), (18 + hx, hy + 9)], fill=1)
    d.rectangle([14 + lean, hy + 11, 18 + lean, max(ty, hy + 11)], fill=7)
    # shoulders
    for sx in (5, 21):
        d.rectangle([sx + lean, ty - 2, sx + 6 + lean, ty + 4], fill=2, outline=1)
        d.line([(sx + 1 + lean, ty - 1), (sx + 5 + lean, ty - 1)], fill=4)
    # arms + rifle
    lh = p.get("lh", (9, ty + 12))
    rh = p.get("rh", (23, ty + 13))
    ls = (7 + lean, ty + 3)
    rs = (25 + lean, ty + 3)
    for (sh, hand, b) in ((ls, lh, -1), (rs, rh, 1)):
        el = ik(sh, hand, 9, 9, b * 0.8)
        line(sh, el, 5, 1)
        line(el, hand, 5, 1)
        line(sh, el, 3, 3)
        line(el, hand, 3, 2)
        d.rectangle([hand[0] - 1, hand[1] - 1, hand[0] + 1, hand[1] + 1], fill=7, outline=1)
    gun_a = p.get("gun_a", (24, ty + 14))
    gun_b = p.get("gun_b", (7, ty + 9))
    if p.get("gun_front"):
        # pointing at the viewer: short thick barrel with the muzzle in front
        cx, cy = gun_b
        d.rectangle([cx - 4, cy - 3, cx + 4, cy + 3], fill=1)
        d.rectangle([cx - 3, cy - 2, cx + 3, cy + 2], fill=8)
        d.rectangle([cx - 1, cy - 1, cx + 1, cy + 1], fill=1)
        d.line([(cx - 3, cy - 2), (cx + 3, cy - 2)], fill=9)
    else:
        line(gun_a, gun_b, 5, 1)
        line(gun_a, gun_b, 3, 8)
        line((gun_a[0], gun_a[1] - 1), (gun_b[0] + 2, gun_b[1] - 1), 1, 9)
        d.point((int(gun_b[0]), int(gun_b[1])), fill=1)
    if p.get("flash"):
        cx, cy = p["flash"]
        d.polygon([(cx, cy - 6), (cx + 2, cy - 2), (cx + 6, cy), (cx + 2, cy + 2),
                   (cx, cy + 6), (cx - 2, cy + 2), (cx - 6, cy), (cx - 2, cy - 2)], fill=10)
        d.polygon([(cx, cy - 3), (cx + 3, cy), (cx, cy + 3), (cx - 3, cy)], fill=11)
    return im


def lying(stage):
    """death frames 2/3: collapsed body."""
    im = newimg(32, 64)
    d = ImageDraw.Draw(im)
    if stage == 2:                       # on the knees, falling back
        d.polygon([(8, 36), (24, 36), (26, 50), (8, 52)], fill=3, outline=1)
        d.rectangle([10, 22, 24, 38], fill=3, outline=1)
        d.rectangle([13, 24, 21, 30], fill=4)
        d.rectangle([11, 12, 22, 22], fill=2, outline=1)
        d.rectangle([12, 16, 21, 17], fill=1)
        d.rectangle([13, 16, 15, 17], fill=5)
        d.rectangle([18, 16, 20, 17], fill=5)
        d.line([(8, 50), (6, 62)], fill=1, width=5)
        d.line([(8, 50), (6, 62)], fill=3, width=3)
        d.line([(24, 50), (27, 61)], fill=1, width=5)
        d.line([(24, 50), (27, 61)], fill=3, width=3)
        d.line([(24, 30), (30, 40)], fill=1, width=5)
        d.line([(24, 30), (30, 40)], fill=2, width=3)
        d.rectangle([14, 36, 18, 38], fill=14)
    else:                                # flat on the floor
        d.ellipse([2, 56, 30, 63], fill=14)
        d.rectangle([4, 50, 14, 58], fill=2, outline=1)            # head
        d.rectangle([6, 53, 7, 54], fill=5)
        d.rectangle([10, 53, 11, 54], fill=5)
        d.rectangle([14, 49, 24, 58], fill=3, outline=1)           # torso
        d.rectangle([16, 51, 22, 55], fill=4)
        d.rectangle([24, 51, 31, 55], fill=7, outline=1)           # legs
        d.rectangle([24, 55, 31, 59], fill=3, outline=1)
        d.line([(8, 60), (22, 62)], fill=8, width=3)               # dropped rifle
        d.line([(8, 59), (22, 61)], fill=9, width=1)
    return im


def hurt_frame():
    return soldier(dict(lean=-2, hdy=1, hdx=-2, dy=1,
                        lh=(6, 28), rh=(27, 26),
                        lf=(10, 62), rf=(22, 62),
                        gun_a=(24, 28), gun_b=(12, 20)))


def enemy_frames():
    F = []
    base_rh, base_lh = (23, 29), (9, 27)
    F.append(soldier(dict()))                                                    # 0 idle
    # walk 1-4
    for lf, rf, dy in (((11, 62), (21, 58), 0), ((11, 62), (21, 62), 1),
                       ((11, 58), (21, 62), 0), ((11, 62), (21, 62), 1)):
        F.append(soldier(dict(lf=lf, rf=rf, dy=dy)))
    # run 5-8: lean forward, bigger stride
    for lf, rf, dy in (((8, 62), (24, 55), 1), ((13, 60), (19, 62), 3),
                       ((8, 55), (24, 62), 1), ((19, 60), (13, 62), 3)):
        F.append(soldier(dict(lf=lf, rf=rf, dy=dy, lean=2, hdy=2,
                              lh=(11, 34), rh=(24, 36), gun_a=(25, 38), gun_b=(8, 32))))
    # strafe 9-10
    for lf, rf in (((6, 62), (17, 62)), ((14, 62), (24, 62))):
        F.append(soldier(dict(lf=lf, rf=rf, lean=-1, kneeb=-1)))
    # crouch 11-12
    cr = dict(dy=14, lf=(9, 62), rf=(23, 62), lh=(10, 41), rh=(23, 43),
              gun_a=(24, 44), gun_b=(8, 38))
    F.append(soldier(dict(cr)))
    c2 = dict(cr)
    c2.update(gun_front=True, gun_b=(16, 42), flash=(16, 42), lh=(13, 42), rh=(20, 42))
    F.append(soldier(c2))
    # jump 13-14
    F.append(soldier(dict(dy=-2, lf=(10, 50), rf=(22, 52), lh=(5, 22), rh=(27, 22),
                          gun_a=(26, 22), gun_b=(5, 18))))
    F.append(soldier(dict(dy=1, lf=(9, 56), rf=(23, 56), lh=(7, 30), rh=(25, 30))))
    # shoot 15-16 (aiming at the viewer)
    sh = dict(gun_front=True, gun_b=(16, 28), lh=(13, 29), rh=(20, 29))
    F.append(soldier(sh))
    s2 = dict(sh)
    s2.update(flash=(16, 28))
    F.append(soldier(s2))
    # hurt 17
    F.append(hurt_frame())
    # death 18-21
    F.append(soldier(dict(lean=-4, hdy=1, hdx=-3, dy=3, lh=(5, 30), rh=(28, 26),
                          lf=(10, 62), rf=(22, 62), gun_a=(26, 22), gun_b=(14, 14))))
    F.append(lying(2))
    F.append(lying(3))
    F.append(lying(3))
    return F


# ---------------------------------------------------------------- the blaster
def blaster(frame):
    """Quake II style blaster, held at the lower right, barrel turned to the left."""
    im = newimg(64, 64)
    d = ImageDraw.Draw(im)
    ox, oy = (0, 0)
    if frame == 1:
        ox, oy = (2, 3)
    elif frame == 2:
        ox, oy = (1, 1)

    def P(pts, fill, o=1):
        d.polygon([(x + ox, y + oy) for x, y in pts], fill=fill, outline=o)

    # forearm / glove coming from the lower right
    P([(40, 63), (47, 44), (60, 50), (63, 63)], 2)
    P([(44, 62), (49, 49), (58, 53), (61, 62)], 3, 2)
    # grip
    P([(36, 50), (46, 44), (53, 56), (44, 63)], 5)
    P([(38, 51), (45, 47), (49, 55), (43, 60)], 4, 4)
    for i in range(3):
        d.line([(39 + i * 3 + ox, 56 - i + oy), (42 + i * 3 + ox, 52 - i + oy)], fill=1)
    # main body, pointing up-left
    P([(46, 44), (22, 22), (14, 28), (38, 52)], 5)
    P([(44, 42), (24, 25), (19, 29), (38, 48)], 6, 5)
    d.line([(26 + ox, 27 + oy), (42 + ox, 45 + oy)], fill=4)
    # side ribs
    for i in range(4):
        t = i / 3.0
        x0, y0 = 36 - 14 * t, 46 - 14 * t
        d.line([(x0 + ox, y0 + 1 + oy), (x0 - 4 + ox, y0 + 5 + oy)], fill=4)
    # barrel (metal)
    P([(22, 22), (6, 8), (0, 14), (14, 28)], 8)
    P([(20, 22), (8, 11), (4, 14), (15, 25)], 9, 8)
    d.line([(12 + ox, 12 + oy), (6 + ox, 18 + oy)], fill=7)
    # energy cell
    P([(30, 36), (22, 28), (26, 24), (34, 32)], 10)
    P([(30, 34), (25, 29), (27, 27), (32, 32)], 11, 10)
    # muzzle
    P([(6, 8), (2, 4), (-2, 8), (0, 14)], 7)
    if frame == 1:
        cx, cy = 1, 6
        d.polygon([(cx, cy - 9), (cx + 3, cy - 3), (cx + 9, cy), (cx + 3, cy + 3),
                   (cx, cy + 9), (cx - 3, cy + 3), (cx - 9, cy), (cx - 3, cy - 3)], fill=14)
        d.polygon([(cx, cy - 6), (cx + 2, cy - 2), (cx + 6, cy), (cx + 2, cy + 2),
                   (cx, cy + 6), (cx - 2, cy + 2), (cx - 6, cy), (cx - 2, cy - 2)], fill=13)
        d.ellipse([cx - 2, cy - 2, cx + 2, cy + 2], fill=12)
    else:
        d.rectangle([0 + ox, 6 + oy, 2 + ox, 9 + oy], fill=10)
    return im


# --------------------------------------------------------------- fx / bolts
def fx_images():
    """16x16 frames in order: bolt0 bolt1 bolt_far plasma0 plasma1 plasma2
       spark0 spark1 spark2 tele0 tele1 tele2 puff hitflash dummy dummy"""
    out = []

    def mk():
        im = newimg(16, 16)
        return im, ImageDraw.Draw(im)

    for k in range(2):                           # enemy bolt: red streak
        im, d = mk()
        d.ellipse([3, 4 + k, 12, 11 + k], fill=3)
        d.ellipse([4, 5 + k, 11, 10 + k], fill=4)
        d.ellipse([6, 6 + k, 9, 9 + k], fill=5)
        d.point((7, 7 + k), fill=2)
        out.append(im)
    im, d = mk()                                  # rocket: red ball with a white ring (cell 2)
    d.ellipse([1, 1, 14, 14], fill=2)
    d.ellipse([3, 3, 12, 12], fill=3)
    d.ellipse([4, 4, 11, 11], fill=4)
    d.ellipse([5, 5, 9, 9], fill=5)
    d.point((6, 6), fill=2)
    out.append(im)
    for k in range(3):                            # plasma ball (small)
        im, d = mk()
        r = 6 + (k % 2)
        d.ellipse([8 - r, 8 - r, 7 + r, 7 + r], fill=7)
        d.ellipse([8 - r + 1, 8 - r + 1, 6 + r, 6 + r], fill=8)
        d.ellipse([5 + k % 2, 5, 10, 10 - k % 2], fill=9)
        d.point((7, 7), fill=2)
        out.append(im)
    for k in range(3):                            # sparks
        im, d = mk()
        n = 3 + k * 2
        for a in range(8):
            ang = a * math.pi / 4 + k * 0.4
            x, y = 8 + math.cos(ang) * n, 8 + math.sin(ang) * n
            d.point((int(x), int(y)), fill=6 if a % 2 else 12)
        d.ellipse([7 - (2 - k), 7 - (2 - k), 8 + (2 - k), 8 + (2 - k)], fill=12)
        out.append(im)
    wide = newimg(32, 16)                          # spawn hole: black oval on the floor, in two halves
    dd = ImageDraw.Draw(wide)
    dd.ellipse([0, 4, 31, 13], fill=2)
    dd.ellipse([1, 5, 30, 12], fill=1)
    dd.ellipse([4, 6, 27, 11], fill=1)
    dd.arc([0, 4, 31, 13], 200, 340, fill=10)
    left = newimg(16, 16); right = newimg(16, 16)
    left.paste(wide.crop((0, 0, 16, 16)), (0, 0)); right.paste(wide.crop((16, 0, 32, 16)), (0, 0))
    out.append(left); out.append(right); out.append(newimg(16, 16))
    im, d = mk()                                  # smoke puff
    d.ellipse([3, 4, 12, 13], fill=14)
    d.ellipse([5, 5, 10, 10], fill=2)
    out.append(im)
    im, d = mk()                                  # hit flash
    d.polygon([(8, 0), (10, 6), (16, 8), (10, 10), (8, 16), (6, 10), (0, 8), (6, 6)], fill=6)
    d.ellipse([5, 5, 10, 10], fill=12)
    out.append(im)
    out.append(newimg(16, 16))
    out.append(newimg(16, 16))
    return out


def font_glyphs():
    chars = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ/:-!"
    try:
        f = ImageFont.load_default(size=13)
    except TypeError:
        f = ImageFont.load_default()
    glyphs = []
    for ch in chars:
        g = Image.new("L", (16, 16), 0)
        dr = ImageDraw.Draw(g)
        bb = dr.textbbox((0, 0), ch, font=f)
        w, h = bb[2] - bb[0], bb[3] - bb[1]
        dr.text((8 - w // 2 - bb[0], 8 - h // 2 - bb[1]), ch, fill=255, font=f)
        glyphs.append(g)
    return chars, glyphs


def glyph_to_index(g):
    px = g.load()
    out = newimg(16, 16)
    o = out.load()
    on = [[px[x, y] > 90 for x in range(16)] for y in range(16)]
    for y in range(16):
        for x in range(16):
            if on[y][x]:
                o[x, y] = 2
    for y in range(16):
        for x in range(16):
            if not on[y][x]:
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    xx, yy = x + dx, y + dy
                    if 0 <= xx < 16 and 0 <= yy < 16 and on[yy][xx]:
                        o[x, y] = 1
    return out


def special_glyphs():
    out = []
    im = newimg(16, 16)                           # crosshair
    d = ImageDraw.Draw(im)
    for (a, b) in (((7, 1), (7, 4)), ((7, 11), (7, 14)), ((1, 7), (4, 7)), ((11, 7), (14, 7))):
        d.line([a, b], fill=1, width=3)
        d.line([a, b], fill=11, width=1)
    d.point((7, 7), fill=5)
    out.append(im)
    im = newimg(16, 16)                           # life square (full)
    d = ImageDraw.Draw(im)
    d.rectangle([4, 4, 11, 11], fill=1)
    d.rectangle([5, 5, 10, 10], fill=3)
    d.rectangle([6, 6, 9, 9], fill=4)
    d.point((6, 6), fill=5)
    out.append(im)
    im = newimg(16, 16)                           # life square (empty)
    d = ImageDraw.Draw(im)
    d.rectangle([4, 4, 11, 11], fill=1)
    d.rectangle([5, 5, 10, 10], fill=14)
    out.append(im)
    im = newimg(16, 16)                           # left bracket
    d = ImageDraw.Draw(im)
    d.line([(4, 1), (4, 14)], fill=1, width=4)
    d.line([(4, 1), (10, 1)], fill=1, width=4)
    d.line([(4, 14), (10, 14)], fill=1, width=4)
    d.line([(4, 2), (4, 13)], fill=10, width=2)
    d.line([(4, 2), (9, 2)], fill=10, width=2)
    d.line([(4, 13), (9, 13)], fill=10, width=2)
    out.append(im)
    out.append(im.transpose(Image.FLIP_LEFT_RIGHT))
    out.append(arrow_cell(0))
    return out[:-0] if False else out



def arrow_cell(deg):
    """ONE arrow model (outer + inner polygon) rotated clockwise by deg: used for N (0), NE (45) and E (90); other directions are flips"""
    import math
    im = newimg(16, 16)
    d = ImageDraw.Draw(im)
    c, sn = math.cos(math.radians(deg)), math.sin(math.radians(deg))
    def rot(pts):
        return [(8 + (x - 8) * c - (y - 8) * sn + 0.01, 8 + (x - 8) * sn + (y - 8) * c + 0.01) for x, y in pts]
    d.polygon(rot([(8, 1), (14, 14), (8, 10.5), (2, 14)]), fill=1)
    d.polygon(rot([(8, 3.5), (11.5, 12), (8, 9), (4.5, 12)]), fill=12)
    return im


def beam_cell(s):
    """a thin beam piece for the beamchain: 16x16, a 2 px white core with cyan edges rising to the right with slope s"""
    im = newimg(16, 16)
    px = im.load()
    for x in range(16):
        yc = int(round(8 - s * (x - 7.5)))
        for dy, col in ((-1, 10), (0, 2), (1, 2), (2, 10)):
            y = yc + dy
            if 0 <= y < 16: px[x, y] = col
    return im


def extra_cells():
    """cells 50..61 of table 1: arrow NE, arrow E, small squares FF/F_/__, charge block, (60,61 = visor icons, placed by main)"""
    out = {}
    out[50] = arrow_cell(45)
    out[51] = arrow_cell(90)
    out[62] = beam_cell(0.17)
    out[63] = beam_cell(0.36)
    for cell, (a, b) in ((52, (1, 1)), (53, (1, 0)), (58, (0, 0))):    # two small squares, pitch 5
        im = newimg(16, 16)
        d = ImageDraw.Draw(im)
        for k, on in enumerate((a, b)):
            x = k * 5
            d.rectangle([x, 6, x + 3, 9], fill=1)
            d.rectangle([x + 1, 7, x + 2, 8], fill=4 if on else 14)
        out[cell] = im
    im = newimg(16, 16)                           # charge bar block (colour index 15 of the weapon palette)
    d = ImageDraw.Draw(im)
    d.rectangle([0, 5, 15, 10], fill=1)
    d.rectangle([0, 6, 15, 9], fill=15)
    out[59] = im
    return out


def big_plasma(k):
    im = newimg(32, 32)
    d = ImageDraw.Draw(im)
    r = 14 + (k % 2)
    d.ellipse([16 - r, 16 - r, 15 + r, 15 + r], fill=7)
    d.ellipse([16 - r + 2, 16 - r + 2, 13 + r, 13 + r], fill=8)
    d.ellipse([10 + k % 2, 9, 22, 22 - k % 2], fill=9)
    d.ellipse([13, 13, 18, 18], fill=2)
    for a in range(6):
        ang = a * math.pi / 3 + k * 0.5
        d.line([(16, 16), (16 + math.cos(ang) * 14, 16 + math.sin(ang) * 14)], fill=9)
    return im


# ------------------------------------------------------------- boss computer
def boss_big(frame):
    """80x80 supercomputer. frame 0/1 idle (LEDs alternate), 2 charging."""
    im = newimg(80, 80)
    d = ImageDraw.Draw(im)
    # base and cables
    d.rectangle([2, 70, 77, 79], fill=13, outline=1)
    d.rectangle([6, 72, 73, 74], fill=14)
    for x in range(8, 72, 8):
        d.rectangle([x, 72, x + 3, 74], fill=15)
    # side racks
    for (x0, x1) in ((4, 24), (56, 76)):
        d.rectangle([x0, 14, x1, 70], fill=2, outline=1)
        for y in range(18, 66, 8):
            d.rectangle([x0 + 3, y, x1 - 3, y + 5], fill=3, outline=1)
            for i in range(4):
                led_on = ((i + y // 8 + (frame & 1)) % 3 == 0)
                d.point((x0 + 5 + i * 3, y + 2), fill=9 if led_on else 5)
                d.point((x0 + 5 + i * 3, y + 3), fill=8 if (i + frame) % 4 == 0 else 5)
        d.line([(x0 + 1, 15), (x1 - 1, 15)], fill=4)
    # centre tower
    d.rectangle([22, 6, 58, 72], fill=3, outline=1)
    d.rectangle([24, 8, 56, 70], fill=2)
    d.line([(24, 8), (56, 8)], fill=4)
    # screen
    d.rectangle([27, 12, 53, 30], fill=1)
    d.rectangle([28, 13, 52, 29], fill=5)
    for y in range(15, 28, 3):
        w = 6 + ((y * 7 + frame * 5) % 18)
        d.line([(30, y), (30 + w, y)], fill=6)
    d.rectangle([44, 15, 50, 17], fill=7 if frame != 1 else 6)
    # core (plasma cannon)
    cx, cy = 40, 48
    glow = 11 if frame == 2 else 10
    d.ellipse([cx - 13, cy - 13, cx + 13, cy + 13], fill=1)
    d.ellipse([cx - 11, cy - 11, cx + 11, cy + 11], fill=13)
    d.ellipse([cx - 9, cy - 9, cx + 9, cy + 9], fill=glow)
    d.ellipse([cx - 6, cy - 6, cx + 6, cy + 6], fill=11 if frame != 1 else 10)
    d.ellipse([cx - 3, cy - 3, cx + 3, cy + 3], fill=12)
    if frame == 2:
        for a in range(8):
            ang = a * math.pi / 4
            d.line([(cx, cy), (cx + math.cos(ang) * 17, cy + math.sin(ang) * 17)], fill=11)
    # hazard strips and vents
    for x in range(24, 56, 4):
        d.rectangle([x, 62, x + 1, 69], fill=14 if (x // 4) % 2 else 15)
    for y in range(33, 38):
        d.line([(27, y), (53, y)], fill=1 if y % 2 else 3)
    # antenna
    d.line([(40, 6), (40, 0)], fill=4, width=1)
    d.point((40, 0), fill=8 if frame != 1 else 9)
    return im


def boss_scaled(big, size):
    """nearest-neighbour style resample with outline protection."""
    w, h = big.size
    out = newimg(size, size)
    s, d = big.load(), out.load()
    for y in range(size):
        for x in range(size):
            sx0, sx1 = x * w // size, max(x * w // size + 1, (x + 1) * w // size)
            sy0, sy1 = y * h // size, max(y * h // size + 1, (y + 1) * h // size)
            cnt = {}
            for yy in range(sy0, min(sy1, h)):
                for xx in range(sx0, min(sx1, w)):
                    c = s[xx, yy]
                    if c:
                        cnt[c] = cnt.get(c, 0) + 1
            tot = sum(cnt.values())
            area = (min(sy1, h) - sy0) * (min(sx1, w) - sx0)
            if tot * 2 >= area and tot:
                d[x, y] = max(cnt.items(), key=lambda kv: (kv[1], kv[0] in (8, 9, 10, 11, 6, 7)))[0]
    return out


# ------------------------------------------------------------------- build
def sheet(images, cols, scale, path, bgc=(40, 40, 60), pal=None):
    w = max(i.size[0] for i in images)
    h = max(i.size[1] for i in images)
    rows = (len(images) + cols - 1) // cols
    sh = Image.new("RGB", (cols * (w + 2) * scale, rows * (h + 2) * scale), bgc)
    for n, im in enumerate(images):
        rgbim = Image.new("RGB", im.size, bgc)
        pxs, d = im.load(), rgbim.load()
        for y in range(im.size[1]):
            for x in range(im.size[0]):
                c = pxs[x, y]
                if c:
                    r, g, b = pal[c]
                    d[x, y] = (r * 8, g * 8, b * 8)
        rgbim = rgbim.resize((im.size[0] * scale, im.size[1] * scale), Image.NEAREST)
        sh.paste(rgbim, ((n % cols) * (w + 2) * scale, (n // cols) * (h + 2) * scale))
    sh.save(path)


def main():
    # ---- enemies and the blaster come from tools/gen_art.py (spr_enemy0..3.bin, spr_weapon.bin)
    import json
    art = json.load(open(os.path.join(OUT, "art_pal.json")))
    data = wdata = b""

    # ---- fx for table 0, region cols 8..15 rows 8..15 (8x8 tiles = 16 frames of 16x16)
    fx = fx_images()
    import numpy as _np
    icells = _np.load(os.path.join(OUT, "item_cells.npy"))
    def cell_img(a):
        im = newimg(16, 16)
        pxx = im.load()
        for yy in range(16):
            for xx in range(16):
                pxx[xx, yy] = int(a[yy, xx])
        return im
    FXFREE = [1, 3, 4, 5, 11, 13, 14, 15]
    region = newimg(64, 64)
    for n, im in enumerate(fx):
        if n not in FXFREE:
            region.paste(im, ((n % 4) * 16, (n // 4) * 16))
    for k in range(8):
        region.paste(cell_img(icells[k]), ((FXFREE[k] % 4) * 16, (FXFREE[k] // 4) * 16))
    tiles = to_tiles(region)                         # 8 per row
    open(os.path.join(OUT, "spr_fx0.bin"), "wb").write(b"".join(tiles))
    sheet(fx, 8, 4, os.path.join(SHEETS, "fx.png"), pal=PAL_FX)

    # ---- table 1: glyphs + crosshair + plasma (256 x 16 tiles = 128x128 px)
    chars, gl = font_glyphs()
    gi = [glyph_to_index(g) for g in gl] + special_glyphs()
    t1 = newimg(128, 128)
    for n, im in enumerate(gi):                      # 8 glyphs per row (16 px each)
        t1.paste(im, ((n % 8) * 16, (n // 8) * 16))
    for k in range(2):                               # item cells 8,9 -> glyph cells 46,47
        t1.paste(cell_img(icells[8 + k]), (((46 + k) % 8) * 16, ((46 + k) // 8) * 16))
    for k in range(2):                               # item cells 10, 11 (gun crate) -> the corner (x 96.., y 96..); cells 62, 63 there belong to the beams
        t1.paste(cell_img(icells[10 + k]), (96 + (k & 1) * 16, 96 + (k >> 1) * 16))
    plas = [big_plasma(0)]
    for k, im in enumerate(plas):                    # one big plasma ball (flipped for animation), rows 12..15
        t1.paste(im, (k * 32, 96))
    for cell, im in extra_cells().items():
        t1.paste(im, ((cell % 8) * 16, (cell // 8) * 16))
    t1.paste(cell_img(icells[12]), ((37 % 8) * 16, (37 // 8) * 16))     # nightvisor and medkit icons: the unused glyph cells ':' and '!'
    t1.paste(cell_img(icells[13]), ((39 % 8) * 16, (39 // 8) * 16))
    for k in range(2):                               # visor icons: opto, red -> cells 60, 61
        t1.paste(cell_img(icells[14 + k]), (((60 + k) % 8) * 16, ((60 + k) // 8) * 16))
    tiles = to_tiles(t1)                             # 16 per row
    open(os.path.join(OUT, "spr_obj1.bin"), "wb").write(b"".join(tiles))
    sheet([t1], 1, 4, os.path.join(SHEETS, "obj1.png"), pal=PAL_FX)

    # ---- boss: 3 frames x (80, 48, 24)
    bigs = [boss_big(i) for i in range(3)]
    sizes = (80, 48, 24)
    bdata = bytearray()
    order = []
    for size in sizes:
        for f in range(3):
            im = bigs[f] if size == 80 else boss_scaled(bigs[f], size)
            order.append(im)
            bdata += b"".join(to_tiles(im))
    open(os.path.join(OUT, "boss.bin"), "wb").write(bytes(bdata))
    sheet(order, 9, 2, os.path.join(SHEETS, "boss.png"), pal=PAL_BOSS)

    # ---- OBJ palettes: 0 alien, 1 blaster, 2 fx/font, 3 hurt, 4 wheel, 5 tank, 6 penguin, 7 spare
    ep = [[tuple(c) for c in p] for p in art["enemy_pals"]]
    wpal = [tuple(c) for c in art["weapon_pal"]]
    hpal = [tuple(c) for c in art["hurt_pal"]]
    ip = [[tuple(c) for c in p] for p in art["item_pals"]]
    objp = [ep[0], wpal, PAL_FX, hpal, ep[1], ip[0], ip[1], ip[2]]
    open(os.path.join(OUT, "spr_pal.bin"), "wb").write(b"".join(pal_bytes(p) for p in objp))
    open(os.path.join(OUT, "boss_pal.bin"), "wb").write(pal_bytes(PAL_BOSS))
    bbox = art["enemy_bb"]

    # ---- boss palette variants: normal, hit flash, dead
    boss_var = [PAL_BOSS,
                [(0, 0, 0)] + [(min(31, 14 + r // 2 + 8), min(31, 14 + g // 2 + 8), min(31, 14 + b // 2 + 8)) for (r, g, b) in PAL_BOSS[1:]],
                [(0, 0, 0)] + [(r // 3, g // 3, b // 3) for (r, g, b) in PAL_BOSS[1:]]]
    # ---- C header (arrays emitted by gen_data.py through .incbin in the loader)
    with open(os.path.join(OUT, "spr_defs.h"), "w") as f:
        f.write("/* generated by tools/gen_sprites.py */\n#ifndef SPR_DEFS_H\n#define SPR_DEFS_H\n")
        f.write("#define ENEMY_FRAMES 20\n#define ENEMY_TYPES 7\n#define ENEMY_LEVELS 10\n")
        f.write("#define GLYPH_CHARS \"%s\"\n" % chars)
        f.write("#define GLYPH_CROSS %d\n#define GLYPH_BARFULL %d\n#define GLYPH_BAREMPTY %d\n#define GLYPH_BRL %d\n#define GLYPH_BRR %d\n#define GLYPH_ARN %d\n#define GLYPH_ARNE 50\n#define GLYPH_ARE 51\n#define GLYPH_SQ2 52\n#define GLYPH_SQ1 53\n#define GLYPH_SQ0 58\n#define GLYPH_CHG 59\n"
                % (len(chars), len(chars) + 1, len(chars) + 2, len(chars) + 3, len(chars) + 4, len(chars) + 5))
        f.write("__attribute__((unused)) static const u8 enemy_hp[7] = {%s};\n" % ",".join(str(v) for v in art["hp"]))
        f.write("static const u32 enemy_kc[7] = {%s};\n" % ",".join(str(v) + "UL" for v in art["kc"]))
        f.write("static const u16 enemy_pal_data[7][16] = {%s};\n" % ",".join("{" + ",".join("0x%04X" % c15(c) for c in p) + "}" for p in ep))
        f.write("static const u16 weapon_pal_data[8][16] = {%s};\n" % ",".join("{" + ",".join("0x%04X" % c15(tuple(c)) for c in p) + "}" for p in art["weapon_pals"]))
        f.write("#endif\n")
    with open(os.path.join(OUT, "spr_pal.h"), "w") as f:
        f.write("/* generated */\nstatic const u16 obj_pal[128] = {\n")
        words = [c15(c) for p in objp for c in p]
        f.write(",".join("0x%04X" % w for w in words) + "\n};\n")
        f.write("static const u16 boss_pal[3][16] = {\n")
        for v in boss_var:
            f.write("{" + ",".join("0x%04X" % c15(c) for c in v) + "},\n")
        f.write("};\n")
        f.write("static const u8 enemy_bb[7][10][20][4] = {\n")
        for bb in bbox:
            f.write("{\n")
            for lvl in bb:
                f.write("{")
                for (t, b_, l, r) in lvl:
                    f.write("{%d,%d,%d,%d}," % (t, b_, l, r))
                f.write("},\n")
            f.write("},\n")
        f.write("};\n")
    print("sprites ok", len(bdata))


if __name__ == "__main__":
    main()
