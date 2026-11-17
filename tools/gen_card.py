#!/usr/bin/env python3
"""gen_card.py - the briefing card: narrow (6 px pitch) text tiles composed from the 5x7 font of res/hud_tiles.h.
writes res/card_tiles.bin (13 lines x 26 tiles x 32 bytes) and res/card.h (tile counts)"""
import re, os
HERE = os.path.dirname(os.path.abspath(__file__)); ROOT = os.path.join(HERE, "..")
PAGE1 = ["YOU ARE IN RADIOACTIVE AREA", "EVERY 15SEC IT TAKES 1 POWERCELL", "MEDKIT RESTORES THE POWERBAR", "YOU HAS 5 RESPAWN DEVICES",
         "DRINK STOPS RADIATION FOR 30SEC", "GUNS NEED TO RELOAD AMMO", "FUSER HAS INFINITE AMMO", "ENEMIES BLOCK GATES",
         "YOU NEED A CARD TO OPEN GATE", "YOU MUST TO FIND 9 AIR CRYSTALS", "", "", "~PAGE 1/2   PRESS RIGHT"]
PAGE2 = ["NIGHTVISOR UNCOVERS A DARK THING", "OPTOVISOR DECRYPTS A TEXT", "REDVISOR TURN OFF A SHIELD", "ALL VISORS ARE UNAVAILABLE", "~  IN COMBAT TO SAVE THE POWER",
         "REP:2 FULL REPAIR  MDK:5 MEDKIT", "ENM:04/10 KILLED / ALL ENEMIES", "", "", "", "", "", "~PAGE 2/2   PRESS LEFT"]
LINES = PAGE1 + PAGE2
t = open(os.path.join(ROOT, "res", "hud_tiles.h")).read()
b = bytes(int(x) for x in re.findall(r'\d+', t[t.index('{'):]))
def gidx(c):
    if c.isdigit(): return ord(c) - 48
    if c.isalpha(): return 10 + ord(c) - 65
    if c == '-': return 38
    if c == '/': return 39
    if c == ':': return 46
    return None
def row5(c, r):
    g = gidx(c)
    if g is None: return 0
    tl = b[g * 32:(g + 1) * 32]
    return ((tl[2 * r] & ~tl[2 * r + 1]) >> 2) & 0x1F
out = bytearray(); nts = []
for ln in LINES:
    s = ln[1:] if ln.startswith("~") else ("- " + ln if ln else "")
    nt = (len(s) * 6 + 7) // 8
    assert nt <= 26
    nts.append(nt)
    for T in range(26):
        tile = bytearray(32)
        for r in range(8):
            m = 0
            for i, c in enumerate(s):
                x0 = 6 * i - 8 * T
                if x0 > 7 or x0 + 5 <= 0: continue
                v = row5(c, r) << 3
                v = v >> x0 if x0 >= 0 else v << -x0
                m |= v & 0xFF
            tile[2 * r] = 0xFF; tile[2 * r + 1] = (~m) & 0xFF
        out += tile
open(os.path.join(ROOT, "res", "card_tiles.bin"), "wb").write(out)
open(os.path.join(ROOT, "res", "card.h"), "w").write("static const u8 card_nt[26] = { %s };\n" % ", ".join(map(str, nts)))
print(len(out), nts)
