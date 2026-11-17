#!/usr/bin/env python3
"""Appends frame 23 of res/spr_big.bin: the four alien glyphs (res/alien_glyphs.txt, 16x16 each, palette indices 0/7/8/9)
arranged 2x2 in the 32x32 big block (OBJ cells 48,49,56,57). Run once after gen_art.py (skips if already appended)."""
import os
R = os.path.join(os.path.dirname(__file__), "..", "res")
blocks = [b for b in open(os.path.join(R, "alien_glyphs.txt")).read().split("\n\n") if b.strip()]
g = [[[int(c, 16) for c in line] for line in b.split("\n")] for b in blocks]
img = [[0] * 32 for _ in range(32)]
for n in range(4):
    for y in range(16):
        for x in range(16):
            img[(n // 2) * 16 + y][(n % 2) * 16 + x] = g[n][y][x]
out = bytearray()
for ty in range(4):
    for tx in range(4):
        t = bytearray(32)
        for y in range(8):
            row = img[ty * 8 + y][tx * 8:tx * 8 + 8]
            for p in range(4):
                v = 0
                for x, c in enumerate(row):
                    v |= ((c >> p) & 1) << (7 - x)
                t[(p >> 1) * 16 + y * 2 + (p & 1)] = v
        out += t
def tiles(img):
    out = bytearray()
    for ty in range(4):
        for tx in range(4):
            t = bytearray(32)
            for y in range(8):
                row = img[ty * 8 + y][tx * 8:tx * 8 + 8]
                for p in range(4):
                    v = 0
                    for x, c in enumerate(row):
                        v |= ((c >> p) & 1) << (7 - x)
                    t[(p >> 1) * 16 + y * 2 + (p & 1)] = v
            out += t
    return bytes(out)

# frame 24: four eyes 16x16 (pupil left / centre / right / centre again), white 2, rim 14, pupil 1 (OBJ palette 2)
def eye(cx):
    e = [[0] * 16 for _ in range(16)]
    for y in range(16):
        for x in range(16):
            d = ((x - 7.5) ** 2 + (y - 7.5) ** 2) ** 0.5
            if d <= 7.6:
                e[y][x] = 14 if d > 6.4 else 2
            if ((x - cx) ** 2 + (y - 8.0) ** 2) ** 0.5 <= 3.9:
                e[y][x] = 1
    e[6][int(cx + 1)] = 2; e[6][int(cx + 2)] = 2; e[5][int(cx + 2)] = 2          # the glint
    return e
eyes = [eye(4.5), eye(7.5), eye(10.5), eye(7.5)]
img2 = [[0] * 32 for _ in range(32)]
for n in range(4):
    for y in range(16):
        for x in range(16):
            img2[(n // 2) * 16 + y][(n % 2) * 16 + x] = eyes[n][y][x]
# frame 25: boss-room lamps 16x16: lit (white core, yellow, orange glow), lit pulse (bigger glow), off (grey disc, dark rim)
def lamp(kind):
    e = [[0] * 16 for _ in range(16)]
    for y in range(16):
        for x in range(16):
            d = ((x - 7.5) ** 2 + (y - 7.5) ** 2) ** 0.5
            if kind == 2:
                if d <= 6.3: e[y][x] = 1 if d > 5.2 else 14
            else:
                r = 7.4 if kind == 1 else 6.6
                if d <= r:
                    e[y][x] = 2 if d <= 2.6 else 12 if d <= 4.4 else 6 if d <= 6.0 else 5
    if kind == 2: e[5][5] = 2; e[5][6] = 2
    return e
lamps = [lamp(0), lamp(2), g[0], g[2]]       # cells: lit, off, glyph A, glyph B (the glyph wall behind the boss)
img3 = [[0] * 32 for _ in range(32)]
for n in range(4):
    for y in range(16):
        for x in range(16):
            img3[(n // 2) * 16 + y][(n % 2) * 16 + x] = lamps[n][y][x]
# frame 26: one quarter of the camera crack (32x32): the hole sits in the bottom-right corner, the branches run away from it; the main code flips it into 4 quarters
import random, math
rnd = random.Random(7)
img4 = [[0] * 32 for _ in range(32)]
def put(x, y, c):
    if 0 <= x < 32 and 0 <= y < 32: img4[y][x] = c
def branch(x, y, ang, ln, depth):
    for i in range(ln):
        x += math.cos(ang); y += math.sin(ang); ang += rnd.uniform(-0.12, 0.12)
        put(int(x) + 1, int(y) + 1, 1)
        put(int(x), int(y), 14 if i % 5 else 2)
        if depth < 2 and i > 4 and rnd.random() < 0.07:
            branch(x, y, ang + rnd.choice((-0.7, 0.7)), max(4, (ln - i) // 2), depth + 1)
for a0 in (-3.14159, -2.8, -2.45, -2.1, -1.75, -1.5, -1.15):
    branch(26.0, 26.0, a0 + rnd.uniform(-0.1, 0.1) + 0.0, rnd.randint(16, 30), 0)
for y in range(32):
    for x in range(32):
        dd = ((x - 31.5) ** 2 + (y - 31.5) ** 2) ** 0.5
        if dd <= 6.5:
            img4[y][x] = 5 if (x + y) % 3 else 14
        if dd <= 3.5:
            img4[y][x] = 0
        elif dd <= 4.7:
            img4[y][x] = 1
# frame 27: the explosion (the user's starburst) 32x32, palette 2 colours: white 2, pale 12, yellow 6, orange 15, 5, red 4, dark red 3
from PIL import Image
ex = Image.open(os.path.join(R, "..", "tools", "art_src", "new", "explosion.png")).convert("RGB")
exp = [(2, (31, 31, 31)), (12, (31, 31, 28)), (6, (31, 28, 8)), (15, (31, 22, 6)), (5, (31, 20, 8)), (4, (28, 5, 4)), (3, (14, 2, 2))]
sm = ex.resize((32, 32), Image.LANCZOS)
img5 = [[0] * 32 for _ in range(32)]
for y in range(32):
    for x in range(32):
        r, g, b = sm.getpixel((x, y))
        if min(r, g, b) > 228 and (max(r, g, b) - min(r, g, b)) < 30 and (r + g + b) > 700:
            continue                                   # the white background
        r5, g5, b5 = r // 8, g // 8, b // 8
        img5[y][x] = min(exp, key=lambda e: (e[1][0] - r5) ** 2 + (e[1][1] - g5) ** 2 + (e[1][2] - b5) ** 2)[0]
p = os.path.join(R, "spr_big.bin")
d = open(p, "rb").read()[:23 * 512]
d += bytes(out) + tiles(img2) + tiles(img3) + tiles(img4) + tiles(img5)
if len(d) == 28 * 512:
    open(p, "wb").write(d)
    print("appended")
else:
    print("size", len(d), "- nothing done")
