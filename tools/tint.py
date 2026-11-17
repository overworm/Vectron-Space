#!/usr/bin/env python3
"""tint.py - per-level brightness and palette (hue) of the 3D view, the sprites, the enemies and the boss.
Runs after the generators (build_all.sh): the generated palette tables are rewritten in place.
STAGE_JSON fields: "brightness" 40..200 (percent, 100 = unchanged), "hue" -180..180 (degrees, 0 = unchanged)."""
import colorsys, json, os, re, sys
HERE = os.path.dirname(os.path.abspath(__file__)); RES = os.path.join(HERE, "..", "res")
J = {}
mp = os.environ.get("STAGE_JSON")
if mp and os.path.exists(mp):
    J = json.load(open(mp))
BR = max(40, min(200, int(J.get("brightness", 100)))); HUE = max(-180, min(180, int(J.get("hue", 0))))
if BR == 100 and HUE == 0:
    import shutil; shutil.copy(os.path.join(RES, "prop_pal.base.h"), os.path.join(RES, "prop_pal.h"))
    print("tint: none"); sys.exit(0)

def tint(c):
    r, g, b = c & 31, (c >> 5) & 31, (c >> 10) & 31
    if HUE:
        h, s, v = colorsys.rgb_to_hsv(r / 31, g / 31, b / 31)
        r, g, b = [x * 31 for x in colorsys.hsv_to_rgb((h + HUE / 360.0) % 1.0, s, v)]
    k = BR / 100.0
    r, g, b = [max(0, min(31, int(round(x * k)))) for x in (r, g, b)]
    return (b << 10) | (g << 5) | r

def hexsub(text):
    return re.sub(r"0x([0-9A-Fa-f]{4})", lambda m: "0x%04X" % tint(int(m.group(1), 16)), text)

def header(name, begins):                         # tint only the lines of the listed arrays (up to their closing "};")
    p = os.path.join(RES, name); out = []; on = False
    for ln in open(p).read().split("\n"):
        if any(b in ln for b in begins): on = True
        if on: ln = hexsub(ln)
        if on and ln.strip().startswith("};"): on = False
        out.append(ln)
    open(p, "w").write("\n".join(out))

header("palette.h", ["bg_pal["])
header("spr_pal.h", ["obj_pal[", "boss_pal["])
base = open(os.path.join(RES, "prop_pal.base.h")).read()        # hand-made table: always tinted from its untinted copy
open(os.path.join(RES, "prop_pal.h"), "w").write(hexsub(base))
for n in ("bg_pal.bin", "boss_pal.bin", "spr_pal.bin"):
    p = os.path.join(RES, n); d = bytearray(open(p, "rb").read())
    for i in range(0, len(d) - 1, 2):
        v = tint(d[i] | (d[i + 1] << 8)); d[i] = v & 255; d[i + 1] = v >> 8
    open(p, "wb").write(d)
print("tint: brightness %d%% hue %+d" % (BR, HUE))
