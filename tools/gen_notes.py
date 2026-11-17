#!/usr/bin/env python3
"""gen_notes.py - the small "Note:" lines under the cut-scene titles -> res/spr_note.bin (5 canvases of 64x64 px, 2048 B each, one weapon slot each)
and res/note.h (sprite count, x start). The line is a strip of 16x16 sprites; sprite k = canvas cell (k%4, k//4), tiles 128 + 32*(k//4) + 2*(k%4). pixel value 2 = text (palette 3)"""
import os
HERE = os.path.dirname(os.path.abspath(__file__)); OUT = os.path.join(HERE, "..", "res")
NOTES = ["Note: Anyone can hide from the eyes", "Note: Anyone can return a gift", "Note: Anyone can wait or not", "Note: Anyone can find a exit", "Note: Find all light crystals"]
# glyph: (first row, rows); 9 rows cell, x-height letters rows 2..6, ascenders 0..6, descenders to 8
G = {}
def add(c, r0, rows): G[c] = (r0, rows.split())
add('a', 2, "01110 00001 01111 10001 01111"); add('c', 2, "01110 10001 10000 10001 01110")
add('d', 0, "00001 00001 01111 10001 10001 10001 01111"); add('e', 2, "01110 10001 11111 10000 01110")
add('f', 0, "00110 01001 01000 11100 01000 01000 01000"); add('g', 2, "01111 10001 10001 10001 01111 00001 01110")
add('h', 0, "10000 10000 11110 10001 10001 10001 10001"); add('i', 0, "00100 00000 01100 00100 00100 00100 01110")
add('j', 0, "00010 00000 00110 00010 00010 00010 00010 10010 01100"); add('k', 0, "10000 10000 10010 10100 11000 10100 10010")
add('l', 0, "01100 00100 00100 00100 00100 00100 01110"); add('m', 2, "11010 10101 10101 10101 10101")
add('n', 2, "11110 10001 10001 10001 10001"); add('o', 2, "01110 10001 10001 10001 01110")
add('p', 2, "11110 10001 10001 10001 11110 10000 10000"); add('q', 2, "01111 10001 10001 10001 01111 00001 00001")
add('r', 2, "10110 11001 10000 10000 10000"); add('s', 2, "01111 10000 01110 00001 11110")
add('t', 0, "01000 01000 11100 01000 01000 01001 00110"); add('u', 2, "10001 10001 10001 10011 01101")
add('v', 2, "10001 10001 10001 01010 00100"); add('w', 2, "10001 10101 10101 10101 01010")
add('x', 2, "10001 01010 00100 01010 10001"); add('y', 2, "10001 10001 10001 01111 00001 10001 01110")
add('z', 2, "11111 00010 00100 01000 11111")
add('N', 0, "10001 11001 10101 10101 10011 10001 10001"); add('A', 0, "01110 10001 10001 11111 10001 10001 10001")
add('F', 0, "11111 10000 11110 10000 10000 10000 10000"); add(':', 2, "00000 00100 00000 00000 00100")
def to_tiles(idx):
    out = bytearray()
    for ty in range(8):
        for tx in range(8):
            b = bytearray(32)
            for y in range(8):
                p = [0, 0, 0, 0]
                for x in range(8):
                    c = int(idx[ty * 8 + y][tx * 8 + x]) & 15
                    for k in range(4):
                        p[k] |= ((c >> k) & 1) << (7 - x)
                b[y * 2], b[y * 2 + 1], b[16 + y * 2], b[16 + y * 2 + 1] = p
            out += b
    return bytes(out)
blob = bytearray(); hdr = []
for s in NOTES:
    L = len(s) * 6
    n = (L + 15) // 16
    assert n <= 16
    strip = [[0] * 256 for _ in range(9)]
    for i, ch in enumerate(s):
        if ch not in G: continue
        r0, rows = G[ch]
        for j, row in enumerate(rows):
            for x, v in enumerate(row):
                if v == '1': strip[r0 + j][i * 6 + x] = 2
    cv = [[0] * 64 for _ in range(64)]
    for k in range(n):
        cx, cy = (k % 4) * 16, (k // 4) * 16
        for y in range(9):
            for x in range(16):
                cv[cy + y][cx + x] = strip[y][k * 16 + x]
    blob += to_tiles(cv)
    hdr.append((n, (256 - L) // 2))
open(os.path.join(OUT, "spr_note.bin"), "wb").write(bytes(blob))
open(os.path.join(OUT, "note.h"), "w").write("static const u8 note_n[5] = { %s };\nstatic const u8 note_x[5] = { %s };\n" % (", ".join(str(a) for a, b in hdr), ", ".join(str(b) for a, b in hdr)))
print(len(blob), hdr)
