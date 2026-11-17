#!/usr/bin/env python3
"""
gen_art.py - turns the hand-drawn concept art in tools/art_src/ into game data.

  scene_a.png / scene_b.png  corridor concept (walls, panels, floor, blaster, alien)
  robot_wheel.png, robot_tank.png, penguin.png   enemy concepts (white background)

Outputs in ../res:
  tex_walls.bin   8 wall textures, 32x32, u-major, 1 byte per texel = texel class
  tex_floor.bin   4 floor/ceiling textures, 16x16
  spr_enemy0..3.bin   4 enemy types x 22 frames x 1408 bytes
  spr_weapon.bin  3 blaster frames 64x64
  art_pal.json    OBJ palettes + hit boxes (read by gen_sprites.py / gen_tables.py)
  sheets/*.png    contact sheets for checking the result

Texel classes (5 bits, the LUT of gen_tables.py shades them by distance and by
the light direction of the wall face = pseudo normal mapping):
  class = tilt * 5 + luma          tilt 0 flat, 1 left, 2 right, 3 up, 4 down
                                   (tilt = edge direction found in the art)
  25..28 = emissive accents        amber, green, red, cyan
"""
import json, os, colorsys
import numpy as np
from PIL import ImageOps, ImageFilter,  Image, ImageDraw
from scipy import ndimage

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "art_src")
OUT = os.path.join(HERE, "..", "res")
SHEETS = os.path.join(OUT, "sheets")
os.makedirs(SHEETS, exist_ok=True)

SCENE_A = Image.open(os.path.join(SRC, "scene_a.png")).convert("RGB")
SCENE_B = Image.open(os.path.join(SRC, "scene_b.png")).convert("RGB")

LUMA_LEVELS = 5
ACC_BASE = 25


# ================================================================ textures
def accent_of(rgb):
    """rgb 0-255 arrays (...,3) -> accent index 0..3 or -1"""
    r, g, b = [rgb[..., i] / 255.0 for i in range(3)]
    mx = np.maximum(np.maximum(r, g), b)
    mn = np.minimum(np.minimum(r, g), b)
    v = mx
    s = np.where(mx > 0, (mx - mn) / np.maximum(mx, 1e-6), 0)
    d = np.maximum(mx - mn, 1e-6)
    h = np.where(mx == r, ((g - b) / d) % 6,
                 np.where(mx == g, (b - r) / d + 2, (r - g) / d + 4)) * 60.0
    out = -np.ones(r.shape, dtype=int)
    out[(h >= 15) & (h <= 65) & (s > 0.45) & (v > 0.55)] = 0          # amber / yellow
    out[(h > 65) & (h <= 165) & (s > 0.35) & (v > 0.45)] = 1          # green
    out[((h < 15) | (h > 335)) & (s > 0.5) & (v > 0.5)] = 2           # red
    out[(h >= 175) & (h <= 235) & (s > 0.3) & (v > 0.82)] = 3         # cyan / bright blue
    return out


def classify(img, size):
    """RGB image -> [v][u] texel classes"""
    ss = 4
    big = np.asarray(img.resize((size * ss, size * ss), Image.LANCZOS), dtype=float)
    acc = accent_of(big)
    small = ImageOps.autocontrast(img.resize((size, size), Image.LANCZOS), cutoff=1)
    small = small.filter(ImageFilter.UnsharpMask(radius=1.0, percent=170, threshold=2))
    y = np.asarray(small.convert("L"), dtype=float)
    lo, hi = np.percentile(y, 3), np.percentile(y, 97)
    yn = np.clip((y - lo) / max(hi - lo, 1), 0, 1) ** 0.9
    luma = np.minimum((yn * LUMA_LEVELS).astype(int), LUMA_LEVELS - 1)
    ys = ndimage.gaussian_filter(yn, 0.6)
    gx = ndimage.sobel(ys, axis=1) / 8.0
    gy = ndimage.sobel(ys, axis=0) / 8.0
    thr = 0.17
    tilt = np.zeros(y.shape, dtype=int)
    horiz = np.abs(gx) >= np.abs(gy)
    tilt[horiz & (gx > thr)] = 1
    tilt[horiz & (gx < -thr)] = 2
    tilt[~horiz & (gy > thr)] = 3
    tilt[~horiz & (gy < -thr)] = 4
    cls = tilt * LUMA_LEVELS + luma
    # accent when enough of the block is accent coloured
    for a in range(4):
        m = (acc == a).reshape(size, ss, size, ss).mean(axis=(1, 3)) > 0.34
        cls[m] = ACC_BASE + a
    return cls


SK = 1024.0 / 1408.0
SHEET_BOX = dict(
    A1=(6, 6, 186, 166), A2=(382, 6, 560, 166), A3=(708, 6, 840, 166), A4=(1036, 6, 1215, 166),
    B1=(6, 198, 186, 358), B2=(382, 198, 560, 358), B3=(708, 198, 840, 358), B4=(1036, 198, 1215, 358),
    C1=(6, 390, 186, 550), C2=(382, 390, 560, 550), C3=(708, 390, 840, 550), C4=(1036, 390, 1215, 550),
    D1=(6, 582, 186, 742), D2=(192, 582, 372, 742), D3=(380, 582, 560, 742), D4=(568, 582, 700, 742),
    E1=(708, 582, 840, 742), E2=(848, 582, 1028, 742), E3=(1036, 582, 1215, 742), E4=(1222, 582, 1400, 742))
NEW = os.path.join(SRC, "new")
TA = Image.open(os.path.join(NEW, "texA.png")).convert("RGB")
TB = Image.open(os.path.join(NEW, "texB.png")).convert("RGB")
DOOR = Image.open(os.path.join(NEW, "door.png")).convert("RGB")


def box(k):
    return tuple(int(round(v * SK)) for v in SHEET_BOX[k])


# wall ids 1..9, then doors 10..14 (auto, K1D blue, K2D yellow, K3D red, TRD)
WALL_SRC = [(TB, box("C4")), (TB, box("B2")), (TB, box("C1")), (TA, box("D3")), (TB, box("B3")),
            (TB, box("D1")), (TB, box("A3")), (TB, box("C2")), (TA, box("D2"))]
FLOOR_SRC = [(TA, box("A1")), (TA, box("A2")), (TB, box("A2")), (TA, box("A3"))]
DOOR_BOX = (46, 44, 476, 522)
DOOR_ACC = [26, 28, 25, 27, 27]       # lamp colour per door id 10..14


def door_cls(n):
    c = classify(DOOR.crop(DOOR_BOX), 32)
    acc = DOOR_ACC[n]
    c[2:5, 12:21] = acc                 # status lamp box
    if n:
        c[10:12, 4:28] = acc            # colour band across the leaves
        c[24:26, 4:28] = acc
    if n == 4:
        c[7:9, :] = acc
        c[27:29, :] = acc
    return c


def stage_json():
    import json
    p = os.environ.get("STAGE_JSON") or os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "maps", "stage1.json")
    return json.load(open(p)) if os.path.exists(p) else {}


def custom_tex(path):
    p = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "maps", path)
    im = Image.open(p).convert("RGB")
    return im, (0, 0, im.width, im.height)


def export_thumbs():
    d = os.path.join(os.path.dirname(os.path.abspath(__file__)), "mapedit", "static", "tex")
    os.makedirs(d, exist_ok=True)
    for n, (im, bx) in enumerate(WALL_SRC):
        im.crop(bx).resize((64, 64), Image.LANCZOS).save(os.path.join(d, "wall_%d.png" % (n + 1)))
    for n, (im, bx) in enumerate(FLOOR_SRC):
        im.crop(bx).resize((64, 64), Image.LANCZOS).save(os.path.join(d, "floor_%d.png" % n))
    for n in range(5):
        DOOR.crop(DOOR_BOX).resize((64, 64), Image.LANCZOS).save(os.path.join(d, "door_%d.png" % n))


def build_textures():
    export_thumbs()
    SJ = stage_json()
    wall_src = list(WALL_SRC)
    for k, f in (SJ.get("wall_tex") or {}).items():            # map editor: replace wall texture id k (1..9) by an uploaded picture
        if 1 <= int(k) <= 9 and f:
            wall_src[int(k) - 1] = custom_tex(f)
    floor_src = list(FLOOR_SRC)
    fsel, csel = int(SJ.get("floor_tex", 0)), int(SJ.get("ceil_tex", 2))
    floor_slots = [floor_src[fsel] if fsel != 2 else floor_src[0], floor_src[1], None, floor_src[3]]
    if SJ.get("floor_tex_file"):
        floor_slots[0] = custom_tex(SJ["floor_tex_file"])
    if SJ.get("ceil_tex_file"):
        floor_slots[2] = custom_tex(SJ["ceil_tex_file"])
    elif csel != 2:
        floor_slots[2] = floor_src[csel]
    walls = bytearray()
    wall_rgb, floor_rgb = [], []                                 # v11: true colour textures for the 8 bpp view (tex_rgb.npz)
    curves = []                                                 # v11.2: per wall id, the colour of the real texture at 11 brightness steps (chroma ratio to luma), for the wall colour groups
    def curve_of(img):
        a = np.asarray(img.resize((32, 32), Image.LANCZOS).convert("RGB"), dtype=float).reshape(-1, 3)
        l = 0.3 * a[:, 0] + 0.59 * a[:, 1] + 0.11 * a[:, 2]
        order = np.argsort(l)
        out = []
        for i in range(11):
            q = i / 10.0
            lo = int(max(0, (q - 0.12)) * (len(a) - 1)); hi = int(min(1.0, (q + 0.12)) * (len(a) - 1)) + 1
            m = a[order[lo:hi]].mean(0)
            lm = max(1.0, 0.3 * m[0] + 0.59 * m[1] + 0.11 * m[2])
            out.append([float(x) for x in (m / lm)])
        return out
    sheet = Image.new("RGB", (14 * 34, 34 + 18 + 40), (20, 20, 20))
    for n, (im, bx) in enumerate(wall_src):
        c = np.tile(classify(im.crop(bx), 16), (2, 2))          # half size: the 16x16 detail repeated 2x2
        for (lu, lv) in ((3 + n % 5, 4), (20 - n % 4, 11 + n % 3), (11 + n % 6, 26)):
            c[lv, lu] = 28                                      # small bluish point lights
        for u in range(32):
            for v in range(32):
                walls.append(int(c[v, u]))
        sheet.paste(im.crop(bx).resize((32, 32), Image.LANCZOS), (n * 34, 0))
        curves.append(curve_of(im.crop(bx)))
        wall_rgb.append(np.asarray(im.crop(bx).resize((32, 32), Image.LANCZOS).convert('RGB'), dtype=np.uint8))
    ACCRGB = [(255, 160, 25), (75, 230, 80), (255, 50, 40), (60, 225, 255)]
    for n in range(5):
        curves.append(curve_of(DOOR.crop(DOOR_BOX)))
        dr = np.asarray(DOOR.crop(DOOR_BOX).resize((32, 32), Image.LANCZOS).convert('RGB'), dtype=np.uint8).copy()
        acc_ = ACCRGB[DOOR_ACC[n] - 25]
        dr[2:5, 12:21] = acc_
        if n:
            dr[10:12, 4:28] = acc_; dr[24:26, 4:28] = acc_
        if n == 4:
            dr[7:9, :] = acc_; dr[27:29, :] = acc_
        wall_rgb.append(dr)
        c = door_cls(n)
        for u in range(32):
            for v in range(32):
                walls.append(int(c[v, u]))
        sheet.paste(DOOR.crop(DOOR_BOX).resize((32, 32), Image.LANCZOS), ((9 + n) * 34, 0))
    fl = bytearray()
    for n, slot in enumerate(floor_slots):
        if slot is not None:
            im, bx = slot
            c = classify(im.crop(bx), 16)
        else:
            im, bx, c = None, None, None
        if n == 2 and slot is None:                                                # ceiling: flat light panel with a round black lamp
            c = np.full((16, 16), 4, dtype=int)
            yy, xx = np.mgrid[0:16, 0:16]
            rr = np.hypot(xx - 7.5, yy - 7.5)
            c[(rr > 3.0) & (rr < 5.2)] = 0
            c[rr <= 3.0] = 3
        if n == 3:                                                                 # the chase road (the floor of the bike room): asphalt, a dashed amber lane divider (the texture repeats every 256 units = 3 dividers across the road), tyre marks
            r = np.random.RandomState(7)
            c = np.full((16, 16), 2, dtype=int)
            c[r.rand(16, 16) < 0.18] = 3
            c[r.rand(16, 16) < 0.08] = 1
            for v in range(2, 13):
                for u in (14, 15, 0, 1):
                    c[v, u] = 25
            c[:, 7] = np.where(r.rand(16) < 0.55, 1, c[:, 7])                       # dark tyre streaks
            c[:, 8] = np.where(r.rand(16) < 0.3, 1, c[:, 8])
            c[:, 11] = np.where(r.rand(16) < 0.35, 1, c[:, 11])
        if im is not None:
            floor_rgb.append(np.asarray(im.crop(bx).resize((16, 16), Image.LANCZOS).convert('RGB'), dtype=np.uint8))
        else:                                                                          # procedural slots: ceiling panel / chase road
            fr = np.zeros((16, 16, 3), dtype=np.uint8)
            if n == 2:
                fr[:] = (150, 156, 166)
                yy, xx = np.mgrid[0:16, 0:16]; rr = np.hypot(xx - 7.5, yy - 7.5)
                fr[(rr > 3.0) & (rr < 5.2)] = (10, 10, 12); fr[rr <= 3.0] = (210, 220, 235)
            else:
                fr[:] = (70, 70, 74)
                for v in range(2, 13):
                    for u in (14, 15, 0, 1): fr[v, u] = (230, 160, 30)
                for col, pr in ((7, .55), (8, .3), (11, .35)):
                    for v in range(16):
                        if r.rand() < pr: fr[v, col] = (28, 28, 30)
            floor_rgb.append(fr)
        for v in range(16):
            for u in range(16):
                fl.append(int(c[v, u]))
        if im is not None:
            sheet.paste(im.crop(bx).resize((16, 16), Image.LANCZOS), (n * 18, 36))
    np.savez(os.path.join(OUT, "tex_rgb.npz"), walls=np.array(wall_rgb), floors=np.array(floor_rgb))
    import json as _j
    _j.dump(curves, open(os.path.join(OUT, "wall_curves.json"), "w"))
    open(os.path.join(OUT, "tex_walls.bin"), "wb").write(bytes(walls))
    open(os.path.join(OUT, "tex_floor.bin"), "wb").write(bytes(fl))
    sheet.resize((sheet.width * 3, sheet.height * 3), Image.NEAREST).save(os.path.join(SHEETS, "tex_src.png"))


# ================================================================ enemies
LEVELS = [(1.0, 0), (0.79, 0), (0.63, 0), (0.5, 1), (0.4, 1), (0.32, 1), (0.25, 2), (0.2, 2), (0.16, 2), (0.125, 2)]   # (scale, canvas class)
CANVAS = [64, 32, 16]
NFRONT, NBACK = 15, 5
NFRAMES = NFRONT + NBACK          # 0 idle, 1-6 walk, 7 aim, 8 fire, 9 hurt, 10 crouch, 11 jump,
                                  # 12-14 death, 15-18 walking away (back view)
TYPES = [
    dict(name="pengi", k=0.80, hp=30),
    dict(name="wheel", k=0.90, hp=2),
    dict(name="skel", k=1.08, hp=5),
    dict(name="biped", k=1.08, hp=3),
    dict(name="devourer", k=1.00, hp=7),
    dict(name="soldier", k=1.12, hp=10),
    dict(name="watcher", k=1.00, hp=5),
]


def cut_cells(path):
    im = Image.open(path).convert("RGB")
    a = np.asarray(im).astype(int)
    mx, mn = a.max(2), a.min(2)
    bg = (mn > 165) & ((mx - mn) < 22)
    fg = ndimage.binary_opening(~bg, iterations=1)
    lab, n = ndimage.label(fg, structure=np.ones((3, 3)))
    objs = ndimage.find_objects(lab)
    sizes = ndimage.sum(fg, lab, range(1, n + 1))
    comps = []
    for i, (o, sz) in enumerate(zip(objs, sizes)):
        if sz > 1500:
            comps.append((o[0].start // 200, o[1].start, i + 1))
    comps.sort()
    out = []
    for _, _, lid in comps:
        m = ndimage.binary_fill_holes(lab == lid)
        out.append(rgba_crop(im, m))
    return out


def rgba_crop(im, mask):
    ys, xs = np.where(mask)
    x0, x1, y0, y1 = xs.min(), xs.max() + 1, ys.min(), ys.max() + 1
    rgba = np.zeros((y1 - y0, x1 - x0, 4), dtype=np.uint8)
    rgba[..., :3] = np.asarray(im)[y0:y1, x0:x1]
    rgba[..., 3] = mask[y0:y1, x0:x1] * 255
    return Image.fromarray(rgba, "RGBA")


def xform(img, sy=1.0, rot=0.0, lean=0.0, dy=0):
    """simple pose changes of one cut-out: squash about the feet, rotate, shear, lift"""
    w, h = img.size
    pad = max(w, h)
    big = Image.new("RGBA", (w + 2 * pad, h + 2 * pad), (0, 0, 0, 0))
    if sy != 1.0:
        img = img.resize((w, max(1, int(h * sy))), Image.LANCZOS)
        h = img.height
    big.alpha_composite(img, (pad, pad + (big.height - 2 * pad - h)))
    feet = big.height - pad
    if lean:
        big = big.transform(big.size, Image.AFFINE, (1, lean / h, -lean * feet / h, 0, 1, 0), Image.BICUBIC)
    if rot:
        big = big.rotate(rot, resample=Image.BICUBIC, center=(pad + w // 2, feet))
    bb = big.getbbox()
    out = big.crop(bb)
    return out



BIKE = []


def weak_boss_frames(v):
    """the 'weak boss' penguin (tools/art_src/new/weak_boss.jpg): walk cycle + attack sequence; back/turn views stay from the old penguin sheet"""
    import sys
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import rotcut
    im = Image.open(os.path.join(SRC, "new", "weak_boss.jpg")).convert("RGB")
    x0, x1 = 22, 1385
    cw = (x1 - x0) / 7
    cells = []
    for (y0, y1) in ((60, 238), (242, 434)):
        for c in range(7):
            cells.append(rotcut.cut(im, (int(x0 + c * cw) + 6, y0 + 4, int(x0 + (c + 1) * cw) - 6, y1 - 4)))
    def clean(img):
        a = np.asarray(img).copy()
        m = ndimage.binary_opening(a[..., 3] > 0, structure=np.ones((3, 3)), iterations=2)
        lab, n = ndimage.label(ndimage.binary_dilation(m, iterations=2))
        if n:
            sz = ndimage.sum(m, lab, range(1, n + 1))
            m = m & (lab == 1 + int(np.argmax(sz)))
        a[..., 3] = (m * 255).astype(np.uint8)
        out = Image.fromarray(a, "RGBA")
        return out.crop(out.getbbox())
    att = [clean(rotcut.cut(im, b)) for b in ((255, 505, 515, 742), (520, 505, 830, 742), (905, 500, 1150, 742), (1150, 505, 1385, 742))]
    idle = cells[0]
    def fit(img, mw, mh):
        k = min(mw / img.width, mh / img.height, 1.0)
        return img.resize((max(1, int(img.width * k)), max(1, int(img.height * k))), Image.LANCZOS)
    F = [idle]
    for i, (ci, ln, sy) in enumerate(((1, 3, 1.0), (2, 0, 0.97), (3, -3, 1.0), (4, 0, 0.97), (7, 3, 1.0), (8, 0, 0.97))):
        F.append(xform(cells[ci], lean=ln, sy=sy))
    def toh(img):
        k = idle.height / img.height
        return img.resize((max(1, int(img.width * k)), idle.height), Image.LANCZOS)
    F += [toh(att[0]), toh(att[1])]     # 7 aim (3/4 view), 8 fire (muzzle flashes)
    hs = Image.open(os.path.join(SRC, "new", "boss_hurt.png")).convert("RGB")           # the screaming penguin of the user's sprite: shown for any damage
    F += [toh(clean(rotcut.cut(hs, (0, 0, hs.width, hs.height))))]                      # 9 hurt
    F += [xform(idle, sy=0.72), xform(idle, sy=1.06)]                         # 10 crouch, 11 jump
    sb = Image.open(os.path.join(SRC, "new", "boss_story.png")).convert("RGB")             # storyboard: Standing, Stumble, Prone, Push-Up Initial, Push-Up Mid, Rising, Recovered
    xs = [(12, 152), (158, 298), (300, 446), (452, 592), (596, 734), (736, 866), (868, 1005)]
    st = [clean(rotcut.cut(sb, (x0, 0, x1, 178))) for (x0, x1) in xs]
    ks = idle.height / st[0].height                                                  # the same scale for every cell
    def sc(img):
        return img.resize((max(1, int(img.width * ks)), max(1, int(img.height * ks))), Image.LANCZOS)
    F += [sc(st[1]), sc(st[4]), sc(st[2])]                                          # 12 stumble, 13 push-up, 14 prone (lying on the ground)
    def same(img):                                                                  # the same height as the standing penguin: front, back and turning views look alike in size
        k = idle.height / img.height
        return img.resize((max(1, int(img.width * k)), idle.height), Image.LANCZOS)
    bs = Image.open(os.path.join(SRC, "new", "bike_sheet.png")).convert("RGB")                    # the boss on the motorcycle: a 360 degree turn in 6 views (user's sheet)
    R1, R2 = (4, 136), (138, 262)
    CX = ((20, 215), (235, 400), (440, 590), (640, 810), (830, 1015))
    cells = (("back", R2, 2), ("sideR", R2, 4), ("frontR", R1, 3), ("front", R1, 2), ("frontL", R1, 1), ("sideL", R2, 0))
    for nm, (ya, yb), ci in cells:
        bk = clean(rotcut.cut(bs, (CX[ci][0], ya, CX[ci][1], yb))); k = 150.0 / bk.height
        BIKE.append(bk.resize((max(1, int(bk.width * k)), 150), Image.LANCZOS))
    # frames: 11 = back (the normal chase view), spin sequence 11, 3, 4, 5, 6, 10; the walk cycle of the boss is only frames 1-2 now (3-6 carry bike views)
    F[11], F[3], F[4], F[5], F[6], F[10] = BIKE[0], BIKE[1], BIKE[2], BIKE[3], BIKE[4], BIKE[5]
    r = same(v["rear"])
    F += [r, xform(r, sy=0.97)]
    F += [same(v["v20"]), same(v["v90"]), same(v["v135"])]
    return F


def saw_img(n):
    import math
    S = 4; N = n * S
    im = Image.new("RGBA", (N, N), (0, 0, 0, 0)); d = ImageDraw.Draw(im)
    c = N / 2; R = N / 2 - 2
    pts = []
    for i in range(32):
        for j, rr in ((0, R * 0.80), (0.5, R), (1, R), (1.5, R * 0.80)):
            a = 2 * math.pi * (i + j / 2) / 32
            pts.append((c + rr * math.cos(a), c + rr * math.sin(a)))
    d.polygon(pts, fill=(150, 156, 164, 255), outline=(60, 64, 72, 255))
    d.ellipse((c - R * 0.62, c - R * 0.62, c + R * 0.62, c + R * 0.62), fill=(112, 118, 128, 255), outline=(70, 74, 82, 255))
    for i in range(6):
        a = 2 * math.pi * i / 6
        d.line((c, c, c + R * 0.6 * math.cos(a), c + R * 0.6 * math.sin(a)), fill=(80, 84, 92, 255), width=S * 2)
    d.ellipse((c - R * 0.16, c - R * 0.16, c + R * 0.16, c + R * 0.16), fill=(200, 60, 40, 255), outline=(60, 20, 16, 255))
    return im.resize((n, n), Image.LANCZOS)


def type_frames(t):
    import sys
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import rotcut
    path = os.path.join(SRC, "rot", t["name"] if t["name"] != "pengi" else "pengi") + ".jpg"
    if t["name"] == "watcher":                     # class 6: the front/back sheets of the tank robot, first cell of each
        fr_ = rotcut.cut(Image.open(os.path.join(SRC, "watcher_front.jpg")).convert("RGB"), (20, 75, 205, 240))
        bk_ = rotcut.cut(Image.open(os.path.join(SRC, "watcher_back.jpg")).convert("RGB"), (20, 75, 205, 240))
        v = dict(front=fr_, v20=fr_, v90=fr_, v135=bk_, rear=bk_)
    elif t["name"] == "pengi":
        top, bot = rotcut.penguin(path)
        v = dict(front=top[0], v20=top[1], v90=bot[2], v135=bot[3], rear=bot[4])
        return weak_boss_frames(v)
    else:
        c = rotcut.five(path)
        v = dict(front=c[0], v20=c[1], v90=c[2], v135=c[3], rear=c[4])
    idle = v["front"]
    F = [idle]
    for i, (ln, sy) in enumerate(((3, 1.0), (0, 0.97), (-3, 1.0), (0, 0.97), (3, 1.0), (0, 0.97))):
        F.append(xform(idle, lean=ln, sy=sy))                # 1-6 walk bob
    F += [idle, xform(idle, sy=1.04)]                        # 7 aim, 8 fire
    F += [xform(idle, lean=-6, rot=-5)]                      # 9 hurt
    F += [xform(idle, sy=0.72)]                              # 10 crouch
    F += [xform(idle, sy=1.06)]                              # 11 jump
    F += [xform(idle, lean=-9, rot=-14), xform(idle, sy=0.55, rot=-35), xform(idle, sy=0.35, rot=-70)]   # 12-14 death
    r = v["rear"]
    F += [r, xform(r, sy=0.97)]                              # 15-16 walking away
    F += [v["v20"], v["v90"], v["v135"]]                     # 17-19 turning
    if t["name"] == "watcher":                               # frames 10/11 of the class-6 body are the rolling barrel (two views of the user's sheet: 0 deg and 45 deg)
        import rotcut as _r
        bv = Image.open(os.path.join(SRC, "new", "barrel_views.png")).convert("RGB")
        for fi, (y0, y1) in ((10, (8, 170)), (11, (338, 486))):
            br = _r.cut(bv, (4, y0, 369, y1)); br = br.crop(br.getbbox())
            k = 150.0 / max(br.width, br.height)
            F[fi] = br.resize((max(1, int(br.width * k)), max(1, int(br.height * k))), Image.LANCZOS)
    return F


def luma_of(c):
    return 0.3 * c[0] + 0.59 * c[1] + 0.11 * c[2]


def tone(im, gam=0.68):
    a = np.asarray(im).astype(float)
    a[..., :3] = 255.0 * (a[..., :3] / 255.0) ** gam
    return Image.fromarray(a.clip(0, 255).astype(np.uint8), "RGBA")


def lift_palette(pal):
    # brighter, flatter shades for the sprites (they get no distance light); matching uses the raw palette
    return pal


def make_palette(frames):
    px = []
    for im in frames:
        sm = im.resize((max(1, im.width // 3), max(1, im.height // 3)), Image.BOX).convert("RGBa").convert("RGBA")
        a = np.asarray(sm)
        px.append(a[..., :3][a[..., 3] > 140])
    flat = Image.fromarray(np.concatenate(px).reshape(-1, 1, 3).astype(np.uint8), "RGB")
    q = flat.quantize(colors=14, method=Image.MEDIANCUT, dither=Image.NONE)
    pal = np.array(q.getpalette()[:42]).reshape(14, 3)
    cols = [tuple(int(round(v / 255 * 31)) for v in c) for c in pal]
    cols.sort(key=luma_of)
    cols[0] = (min(cols[0][0], 2), min(cols[0][1], 2), min(cols[0][2], 4))
    return [(0, 0, 0)] + cols + [(3, 23, 21)]        # 15 = aura


def nearest_index(rgb, pal):
    p = np.array([[c * 8 + 4 for c in col] for col in pal[1:15]], dtype=float)
    d = ((rgb[..., None, :] - p[None, None, :, :]) ** 2 * np.array([0.9, 1.3, 0.7])).sum(axis=3)
    return d.argmin(axis=2) + 1


def render_level(img, scale, cd, pal, outline):
    """cut-out -> indexed cd x cd canvas, feet at the bottom centre"""
    w, h = img.size
    nw, nh = max(1, int(round(w * scale))), max(1, int(round(h * scale)))
    sm = img.convert("RGBa").resize((nw, nh), Image.BOX if scale < 0.6 else Image.LANCZOS).convert("RGBA")
    a = np.asarray(sm).astype(float)
    alpha = a[..., 3] > 120
    idx = np.where(alpha, nearest_index(a[..., :3], pal), 0)
    canvas = np.zeros((cd, cd), dtype=np.uint8)
    x0 = (cd - nw) // 2
    y0 = cd - nh - 1
    # clip to the canvas
    sx0, sy0 = max(0, -x0), max(0, -y0)
    dx0, dy0 = max(0, x0), max(0, y0)
    cw = min(nw - sx0, cd - dx0)
    ch = min(nh - sy0, cd - dy0)
    if cw > 0 and ch > 0:
        canvas[dy0:dy0 + ch, dx0:dx0 + cw] = idx[sy0:sy0 + ch, sx0:sx0 + cw]
    m = canvas > 0
    grown = ndimage.binary_dilation(m, structure=[[0, 1, 0], [1, 1, 1], [0, 1, 0]])
    canvas[grown & ~m] = 15                                  # thin glow around the creature
    return Image.fromarray(canvas, "P")


def build_enemies():
    pals, bbs, sheets = [], [], []
    for ti, t in enumerate(TYPES):
        frames = [tone(f) for f in type_frames(t)]
        idle = frames[0]
        s0 = min(58.0 / idle.width, 62.0 / idle.height)
        pal = make_palette(frames[:9] + frames[15:20])
        pals.append(lift_palette(pal))
        hi = {}
        bb = [[None] * NFRAMES for _ in LEVELS]
        near = [bytearray(), bytearray(), bytearray()]
        back = bytearray()
        s32 = [bytearray() for _ in range(NFRAMES)]
        s16 = [bytearray() for _ in range(NFRAMES)]
        backparts = {}
        for f in range(NFRAMES):
            for li, (sl, cc) in enumerate(LEVELS):
                cd = CANVAS[cc]
                im = render_level(frames[f], s0 * sl, cd, pal, outline=True)
                px = np.asarray(im)
                ys, xs = np.where(px > 0)
                bb[li][f] = (int(ys.min()), int(ys.max()), int(xs.min()), int(xs.max())) if len(ys) else (0, 0, 0, 0)
                tiles = b"".join(to_tiles(im))
                hi[(f, li)] = im
                if li < 3:
                    if f < NFRONT:
                        near[li] += tiles
                    else:
                        backparts[(li, f)] = tiles
                elif li < 6:
                    s32[f] += tiles
                else:
                    s16[f] += tiles
        for li in range(3):
            for f in range(NFRONT, NFRAMES):
                back += backparts[(li, f)]
        s32b = b"".join(bytes(x) for x in s32)
        s16b = b"".join(bytes(x) for x in s16)
        assert len(near[0]) == NFRONT * 2048 and len(back) == 3 * NBACK * 2048
        assert len(s32b) == NFRAMES * 1536 and len(s16b) == NFRAMES * 512
        for nm, data in (("n0", near[0]), ("n1", near[1]), ("n2", near[2]), ("nb", back), ("s32", s32b), ("s16", s16b)):
            assert len(data) <= 32000, (nm, len(data))
            open(os.path.join(OUT, "spr_e%d_%s.bin" % (ti, nm)), "wb").write(bytes(data))
        bbs.append(bb)
        sheets.append((t, pal, hi))
    for ti, (t, pal, hi) in enumerate(sheets):
        cols = NFRAMES
        sh = Image.new("RGB", (cols * 66 * 2, len(LEVELS) * 66 * 2), (70, 60, 100))
        for (f, li), im in hi.items():
            rgb = Image.new("RGB", im.size, (70, 60, 100))
            px, d = im.load(), rgb.load()
            for y in range(im.size[1]):
                for x in range(im.size[0]):
                    c = px[x, y]
                    if c:
                        r, g, b = lift_palette(pal)[c]
                        d[x, y] = (r * 8, g * 8, b * 8)
            sh.paste(rgb.resize((im.width * 2, im.height * 2), Image.NEAREST), (f * 132, li * 132))
        sh.save(os.path.join(SHEETS, "enemy_%s.png" % t["name"]))
    return pals, bbs


def hurt_palette():
    pal = [(0, 0, 0)]
    for i in range(1, 16):
        tt = (i - 1) / 14.0
        pal.append((min(31, int(9 + 22 * tt)), int(2 + 28 * tt ** 1.6), int(2 + 28 * tt ** 1.9)))
    return pal


def to_tiles(img):
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


# ================================================================ blaster (left side, aimed at the crosshair)
GUN_POLY = [(547, 259), (575, 235), (587, 246), (667, 290), (662, 316), (735, 328), (749, 326),
            (764, 346), (786, 366), (800, 391), (803, 450), (800, 470), (800, 554), (642, 554),
            (642, 480), (645, 427), (635, 420), (582, 386), (547, 350), (542, 320)]
MUZZLE = (745, 405)
GUN_ROT = 32          # degrees counter-clockwise: the barrel points up towards the crosshair


def star(draw, cx, cy, r):
    import math
    for rr0, col in ((1.0, (248, 208, 64, 255)), (0.5, (255, 255, 255, 255))):
        pts = []
        for k in range(16):
            rr = r * rr0 if k % 2 == 0 else r * rr0 * 0.38
            ang = k * math.pi / 8
            pts.append((cx + rr * math.cos(ang), cy + rr * math.sin(ang)))
        draw.polygon(pts, fill=col)


WEAPON_SRC = ["gun_a", "gun_b", "gun_c", "gun_c"]     # fuser, quanton (recoloured), plasmagun placeholder fixed below
WEAPON_NAMES = ["FUSER", "QUANTON", "PLASMAGUN", "RAILGUN"]


def gun_cut(name, swap=False):
    im = Image.open(os.path.join(NEW, name + ".png")).convert("RGB")
    a = np.asarray(im).astype(int)
    mx, mn = a.max(2), a.min(2)
    bg = (mn > 232)
    lab, n = ndimage.label(bg)
    edge = set(lab[0, :]) | set(lab[-1, :]) | set(lab[:, 0]) | set(lab[:, -1])
    edge.discard(0)
    bgm = np.isin(lab, list(edge))
    fg = ~bgm
    l2, n2 = ndimage.label(ndimage.binary_dilation(fg, iterations=6))
    if n2 > 1:
        sizes = ndimage.sum(fg, l2, range(1, n2 + 1))
        keep = 1 + int(np.argmax(sizes))
        bgm = bgm | (l2 != keep)             # drop the second, free hand
    body = ndimage.binary_opening(fg, structure=np.ones((3, 3)), iterations=9)       # thin lasers / plasma streaks vanish, the gun body stays
    near = ndimage.binary_dilation(body, structure=np.ones((3, 3)), iterations=16)
    bgm = bgm | (fg & ~near)
    if swap:
        a = a[..., ::-1]
    rgba = np.dstack([a, np.where(bgm, 0, 255)]).astype(np.uint8)
    img = Image.fromarray(rgba, "RGBA")
    img = img.crop(img.getbbox())
    return img if name.endswith("_new") and "koze" not in name else img.transpose(Image.FLIP_LEFT_RIGHT)      # *_new art already points to the right


AIM_DEG = 27.0


MUZ = []


def build_weapon():
    MUZ.clear()
    srcs = [("n_fuser", False), ("n_quanton", False), ("n_plasma", False), ("n_rail", False), ("n_rocket", False), ("n_beam", False), ("n_koze_new", False), ("n_chain", False)]
    CHG = [(31, 31, 31), (31, 28, 4), (6, 31, 8), (31, 31, 31), (31, 16, 2), (4, 26, 31), (27, 6, 31), (31, 24, 6)]
    allframes, pals = [], []
    for wi, (nm, sw) in enumerate(srcs):
        g = gun_cut(nm, sw)
        al = np.asarray(g)[..., 3] > 0
        W_ = al.shape[1]
        xs_ = np.arange(int(W_ * 0.35), int(W_ * 0.92))
        top = np.array([np.argmax(al[:, x]) if al[:, x].any() else np.nan for x in xs_])
        ok = ~np.isnan(top)
        slope = np.polyfit(xs_[ok], top[ok], 1)[0]                 # barrel top edge: y grows downwards
        th_src = float(np.degrees(np.arctan(-slope)))
        if (19, 19, 19, 16, 18, 19, 18, 24)[wi] is not None: th_src = (19, 19, 19, 16, 18, 19, 18, 24)[wi]      # measured by eye on the new art
        g = g.rotate(AIM_DEG + (0, 0, 0, 0, 0, 0, 0, 0)[wi] - th_src, resample=Image.BICUBIC, expand=True)       # barrel axis -> towards the cross-hair
        g = g.crop(g.getbbox())
        sc = min(62.0 / g.width, 56.0 / g.height)
        gw, gh = max(1, int(g.width * sc)), max(1, int(g.height * sc))
        gs = g.convert("RGBa").resize((gw, gh), Image.LANCZOS).convert("RGBA")
        ya, xa = np.nonzero(np.asarray(gs)[..., 3] > 120)
        k = np.argmax(xa * 1.0 - ya * 0.3)                # muzzle: far right, upper end
        muz = (int(xa[k]), int(ya[k]))
        MUZ.append((min(60, muz[0] + 2), 64 - gh + 3 + muz[1]))
        pal = make_palette([tone(gs, 0.85)])
        pal[15] = CHG[wi]
        cols = []
        for f in range(3):
            sh = {0: (0, 0), 1: (-2, 3), 2: (-1, 1)}[f]
            cv = Image.new("RGBA", (64, 64), (0, 0, 0, 0))
            ox = 0
            oy = 64 - gh + sh[1]
            cv.paste(gs, (ox, min(64 - gh + 3, oy)), gs)
            if f == 1:
                d = ImageDraw.Draw(cv)
                star(d, min(60, muz[0] + 2), 64 - gh + 3 + muz[1], 6)
            a_ = np.asarray(tone(cv, 0.85)).astype(float)
            al = a_[..., 3] > 120
            idx = np.where(al, nearest_index(a_[..., :3], pal), 0).astype(np.uint8)
            cols.append(Image.fromarray(idx, "P"))
        allframes.append(cols)
        pals.append(pal)
    data = b"".join(b"".join(to_tiles(im)) for fr in allframes for im in fr)
    open(os.path.join(OUT, "spr_weapon.bin"), "wb").write(data[:4 * 3 * 2048])
    open(os.path.join(OUT, "spr_weapon2.bin"), "wb").write(data[4 * 3 * 2048:])
    open(os.path.join(OUT, "weapon_muz.h"), "w").write("static const u8 weapon_muz[8][2] = {%s};\n" % ",".join("{%d,%d}" % m for m in MUZ))
    sh = Image.new("RGB", (24 * 66, 66), (70, 60, 100))
    for wi, fr in enumerate(allframes):
        for n, im in enumerate(fr):
            px = im.load()
            for y in range(64):
                for x in range(64):
                    c = px[x, y]
                    if c:
                        r, g, b = pals[wi][c]
                        sh.putpixel((x + (wi * 3 + n) * 66, y), (r * 8, g * 8, b * 8))
    sh.save(os.path.join(SHEETS, "weapon_art.png"))
    return pals


def item_frame(name, k):
    im = Image.open(os.path.join(NEW, name + ".png")).convert("RGB")
    xs = [(22, 172), (187, 337), (353, 503), (518, 668), (685, 835), (852, 1002)]
    x0, x1 = xs[k]
    c = im.crop((x0 + 6, 131, x1 - 5, 266))
    a = np.asarray(c).astype(int)
    bg = a.min(2) > 215
    lab, n = ndimage.label(bg)
    edge = set(lab[0, :]) | set(lab[-1, :]) | set(lab[:, 0]) | set(lab[:, -1])
    edge.discard(0)
    bgm = np.isin(lab, list(edge))
    rgba = Image.fromarray(np.dstack([a, np.where(bgm, 0, 255)]).astype(np.uint8), "RGBA")
    return rgba.crop(rgba.getbbox())


def fit16(img, bob=0):
    sc = min(15.0 / img.width, 15.0 / img.height)
    w, h = max(1, int(round(img.width * sc))), max(1, int(round(img.height * sc)))
    sm = img.convert("RGBa").resize((w, h), Image.LANCZOS).convert("RGBA")
    cv = Image.new("RGBA", (16, 16), (0, 0, 0, 0))
    cv.paste(sm, ((16 - w) // 2, 16 - h - bob), sm)
    return cv


def medkit_cut():
    im = Image.open(os.path.join(NEW, "medkit.png")).convert("RGB")
    a_ = np.asarray(im).astype(int)
    bg = a_.min(2) > 205
    lab, n = ndimage.label(bg)
    edge = set(lab[0, :]) | set(lab[-1, :]) | set(lab[:, 0]) | set(lab[:, -1])
    edge.discard(0)
    bgm = np.isin(lab, list(edge))
    rgba = Image.fromarray(np.dstack([a_, np.where(bgm, 0, 255)]).astype(np.uint8), "RGBA")
    return rgba.crop(rgba.getbbox())


def nv_icon(lens=(60, 255, 90, 255), shine=(220, 255, 230, 255)):
    S = 8
    im = Image.new("RGBA", (16 * S, 16 * S), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.rectangle([0, 6 * S, 16 * S, 10 * S], fill=(60, 66, 60, 255))                     # strap
    d.rounded_rectangle([1 * S, 3 * S, 15 * S, 13 * S], radius=2 * S, fill=(40, 44, 42, 255), outline=(10, 12, 10, 255), width=S // 2)
    for cx in (5, 11):
        d.ellipse([(cx - 3) * S, 5 * S, (cx + 3) * S, 11 * S], fill=(20, 24, 22, 255), outline=(150, 160, 150, 255), width=S // 2)
        d.ellipse([(cx - 2) * S, 6 * S, (cx + 2) * S, 10 * S], fill=lens)
        d.ellipse([(cx - 1.2) * S, 6.4 * S, (cx + 0.2) * S, 7.8 * S], fill=shine)
    return im.resize((64, 64), Image.LANCZOS)


SKETCH = "/mnt/user-data/attachments/c027472d-fcbf-5a5d-81ce-3f8ea3b3ae5b/0e80b3ee-image.png"
SKETCH_COPY = os.path.join(os.path.dirname(os.path.abspath(__file__)), "art_src", "new", "weapon_icons.png")


def weapon_icons():
    """the user's sketch: 8 round badges; 7 of them become the weapon pickups (quanton, plasma, rail, rocket, beamchain, koze, chaingun)"""
    from shutil import copyfile
    if os.path.exists(SKETCH):
        copyfile(SKETCH, SKETCH_COPY)
    im = Image.open(SKETCH_COPY).convert("RGB")
    a = np.asarray(im).astype(int)
    fg = ndimage.binary_closing(a.min(2) < 150, iterations=2)
    lab, n = ndimage.label(fg)
    sz = ndimage.sum(fg, lab, range(1, n + 1))
    boxes = []
    for i, o in enumerate(ndimage.find_objects(lab)):
        if sz[i] > 5000:
            boxes.append((o[0].start, o[0].stop, o[1].start, o[1].stop))
    boxes.sort(key=lambda b: (b[0] // 100, b[2]))
    pick = [0, 1, 3, 2, 7, 4, 5]          # tube, atom, lightning, rocket, magnet... order: quanton, plasma, rail, rocket, beamchain, koze, chaingun
    order = [0, 1, 3, 2, 5, 7, 4]
    out = []
    for k in order:
        y0, y1, x0, x1 = boxes[k]
        w_, h_ = x1 - x0, y1 - y0
        ins = int(w_ * 0.17)
        c = im.crop((x0 + ins, y0 + ins, x1 - ins, y1 - ins)).convert("RGB")
        c = c.resize((14, 14), Image.LANCZOS)
        c = Image.eval(c, lambda v: min(255, int(v * 1.25 + 8)))
        cv = Image.new("RGB", (16, 16), (2, 2, 10))
        cv.paste(c, (1, 1))
        arr = np.asarray(cv).astype(int).copy()
        yy, xx = np.mgrid[0:16, 0:16]
        r2 = (xx - 7.5) ** 2 + (yy - 7.5) ** 2
        ring = (r2 > 6.6 ** 2) & (r2 <= 8.0 ** 2)
        arr[ring] = (70, 90, 200)
        arr[r2 > 8.0 ** 2] = 0
        al = np.where(r2 <= 8.0 ** 2, 255, 0)
        arr = np.dstack([arr, al]).astype(np.uint8)
        out.append(Image.fromarray(arr, "RGBA"))
    return out


def build_items():
    """12 cells of 16x16: crystal x2, bottle x2, key blue/yellow/red x2, gun crate x2; palettes for OBJ 5,6,7"""
    gun = gun_cut("gun_c")
    nv = fit16(nv_icon())
    opto = fit16(nv_icon((255, 220, 40, 255), (255, 255, 200, 255)))
    redv = fit16(nv_icon((255, 40, 30, 255), (255, 200, 190, 255)))
    med = fit16(medkit_cut())
    groups = [
        [fit16(item_frame("crystal", 0)), fit16(item_frame("crystal", 3), 2)],
        [fit16(item_frame("bottle", 0)), fit16(item_frame("bottle", 3), 2)],
        [fit16(item_frame("card_b", 0)), fit16(item_frame("card_b", 3), 2),
         fit16(item_frame("card_y", 0)), fit16(item_frame("card_y", 3), 2),
         fit16(item_frame("card_r", 0)), fit16(item_frame("card_r", 3), 2)],
        [fit16(gun), fit16(gun, 2)],
    ]
    wic = weapon_icons()
    pal_sets = [groups[0] + [nv] + groups[1], wic, groups[2] + [med]]
    pals = [make_palette(g) for g in pal_sets]
    use = np.zeros(16)
    for fr in groups[0] + [nv] + groups[1]:
        a_ = np.asarray(fr).astype(float)
        m_ = a_[..., 3] > 110
        for v in nearest_index(a_[..., :3], pals[0])[m_]:
            use[v] += 1
    free_idx = [i for i in np.argsort(use[1:15]) + 1][:3]                      # the 3 least used colours become lens colours
    for i, c in zip(free_idx, [(7, 31, 11), (31, 28, 5), (31, 6, 5)]):
        pals[0][int(i)] = c
    order = [(0, 0), (0, 1), (1, 0), (1, 1), (2, 0), (2, 1), (2, 2), (2, 3), (2, 4), (2, 5), (3, 0), (3, 1), (4, 0), (5, 0), (6, 0), (7, 0)]
    groups.append([nv])
    groups.append([med])
    groups.append([opto])
    groups.append([redv])
    cells = []
    sheet = Image.new("RGB", (16 * 17, 16), (70, 60, 100))
    for n, (g, i) in enumerate(order):
        pi = 0 if g in (0, 1, 4, 6, 7) else (1 if g == 3 else 2)
        a_ = np.asarray(groups[g][i]).astype(float)
        idx = np.where(a_[..., 3] > 110, nearest_index(a_[..., :3], pals[pi]), 0).astype(np.uint8)
        cells.append(idx)
        for y in range(16):
            for x in range(16):
                if idx[y, x]:
                    r, g2, b = pals[pi][idx[y, x]]
                    sheet.putpixel((x + n * 17, y), (r * 8, g2 * 8, b * 8))
    np.save(os.path.join(OUT, "item_cells.npy"), np.array(cells))
    wb = bytearray()
    prev = Image.new("RGB", (16 * 7 * 4, 64), (70, 60, 100))
    for n, ic in enumerate(wic):                      # 7 icons x 128 bytes: tile row 0 (64 bytes), tile row 1 (64 bytes)
        a_ = np.asarray(ic).astype(float)
        idx = np.where(a_[..., 3] > 110, nearest_index(a_[..., :3], pals[1]), 0).astype(np.uint8)
        im_ = Image.fromarray(idx, "P")
        for t in to_tiles(im_):
            wb += t
        for y in range(16):
            for x in range(16):
                if idx[y, x]:
                    r, g2, b = pals[1][idx[y, x]]
                    for yy in range(4):
                        for xx in range(4):
                            prev.putpixel((n * 64 + x * 4 + xx, y * 4 + yy), (r * 8, g2 * 8, b * 8))
    open(os.path.join(OUT, "spr_wicons.bin"), "wb").write(bytes(wb))
    # 2x copies (32x32, 4 tile rows of 128 bytes) of the 16 item cells + the 7 weapon icons: the item that is close is drawn as a 32x32 sprite
    big = bytearray()
    smalls = [np.asarray(c, dtype=np.uint8) for c in cells]
    for ic in wic:
        a_ = np.asarray(ic).astype(float)
        smalls.append(np.where(a_[..., 3] > 110, nearest_index(a_[..., :3], pals[1]), 0).astype(np.uint8))
    for sm in smalls:
        b2 = np.kron(sm, np.ones((2, 2), dtype=np.uint8))
        for t in to_tiles(Image.fromarray(b2, "P")):
            big += t
    open(os.path.join(OUT, "spr_big.bin"), "wb").write(bytes(big))
    prev.save(os.path.join(SHEETS, "wicons.png"))
    sheet.resize((16 * 17 * 4, 64), Image.NEAREST).save(os.path.join(SHEETS, "items.png"))
    return pals


def main():
    build_textures()
    pals, bbs = build_enemies()
    wpals = build_weapon()
    ipals = build_items()
    json.dump(dict(enemy_pals=pals, enemy_bb=bbs, weapon_pal=wpals[0], weapon_pals=wpals, item_pals=ipals, hurt_pal=hurt_palette(),
                   hp=[t["hp"] for t in TYPES],
                   kc=[int(t["k"] * 135168) for t in TYPES],
                   levels=[l[0] for l in LEVELS]),
              open(os.path.join(OUT, "art_pal.json"), "w"))
    print("art ok")


if __name__ == "__main__":
    main()
