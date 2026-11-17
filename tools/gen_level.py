#!/usr/bin/env python3
"""gen_level.py - the ship level (from the hand sketch): 32x64 map, doors, items, spawn points."""
import os
from collections import deque
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "res")
W, H = 32, 64
HULL, HAZ, CON, PIPE, WIRE, SCR, GRAT, SEC, BOSSW = 1, 2, 3, 4, 5, 6, 7, 8, 9
D_AUTO, D_K1, D_K2, D_K3, D_TRD = 10, 11, 12, 13, 14
free = [[False] * W for _ in range(H)]
theme = [[0] * W for _ in range(H)]
TH = {"tip": 0, "w1": 1, "w2": 2, "w3": 3, "wing": 4, "m": 5, "w4": 6, "lab": 7, "hall": 8, "boss": 9}


def rect(x0, y0, x1, y1, t):
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            free[y][x] = True
            theme[y][x] = TH[t]


for i, y in enumerate(range(62, 56, -1)):      # triangle tip
    rect(15 - i, y, 16 + i, y, "tip")
rect(10, 53, 21, 55, "w1")
rect(10, 44, 21, 51, "w2")
rect(10, 37, 21, 42, "w3")
rect(23, 31, 27, 41, "wing")
rect(4, 29, 8, 40, "wing")
rect(10, 31, 15, 35, "m")
rect(17, 31, 21, 35, "m")
rect(10, 22, 21, 29, "w4")
rect(4, 14, 8, 20, "lab")
rect(10, 14, 15, 20, "lab")
rect(17, 14, 21, 20, "lab")
rect(23, 14, 27, 20, "lab")
rect(19, 8, 21, 12, "hall")
rect(11, 8, 12, 12, "hall")
rect(8, 6, 27, 7, "hall")
rect(8, 1, 27, 4, "boss")

doors = []          # (x, y, kind) kind: 0 auto, 1..3 key, 4 TRD plate


def door(x, y, kind):
    free[y][x] = True
    doors.append((x, y, kind))
    theme[y][x] = 0


for n, (x, y) in enumerate([(13, 56), (13, 52), (18, 52), (18, 56)]):
    door(x, y, 0)
door(13, 43, 1); door(18, 43, 1)
door(22, 39, 0); door(22, 33, 0); door(16, 33, 0); door(9, 33, 0); door(9, 29, 0)
door(13, 21, 2); door(19, 21, 3)
door(22, 17, 0); door(20, 13, 0)
for x in range(15, 20):
    door(x, 5, 4)
free[17][9] = free[18][9] = True
theme[17][9] = theme[18][9] = TH["lab"]

STYLE = {0: [HULL, CON, HULL, WIRE, HULL, SCR], 1: [HULL, HAZ, HULL, SEC], 2: [HULL, WIRE, HULL, CON, GRAT],
         3: [HULL, CON, HAZ, HULL, PIPE], 4: [PIPE, HULL, GRAT, PIPE, HAZ], 5: [CON, HULL, SCR, HULL, WIRE],
         6: [HULL, GRAT, HULL, CON, SEC], 7: [SEC, HULL, CON, WIRE, HULL, HAZ], 8: [PIPE, HAZ, PIPE, HULL], 9: [BOSSW]}
mp = [[HULL] * W for _ in range(H)]
for y in range(H):
    for x in range(W):
        if free[y][x]:
            mp[y][x] = 0
for y in range(H):
    for x in range(W):
        if free[y][x]:
            continue
        ths = [theme[y + dy][x + dx] for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))
               if 0 <= x + dx < W and 0 <= y + dy < H and free[y + dy][x + dx]
               and not any(d[0] == x + dx and d[1] == y + dy for d in doors)]
        if ths:
            st = STYLE[ths[0]]
            mp[y][x] = st[(x * 7 + y * 13 + x * y) % len(st)]
# TR terminal at the end of the left corridor
for x in (11, 12):
    mp[13][x] = CON
for (x, y, k) in doors:
    mp[y][x] = D_AUTO + k
    free[y][x] = False          # solid for the connectivity check; doors open at runtime
# door frames: plain hull next to every door
for (x, y, k) in doors:
    for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
        xx, yy = x + dx, y + dy
        if not free[yy][xx] and mp[yy][xx] not in range(10, 15):
            mp[yy][xx] = HULL

START = (15.5, 61.5)
# items: kind 0 crystal 1 bottle 2 key blue 3 key yellow 4 key red 5 quanton 6 plasma 7 rail 8 nightvisor 12 rocketgun 13 beamchain 14 kozematy 15 optovisor 16 redvisor
ITEMS = [(2, 17.5, 60.5), (0, 20.5, 54.5), (0, 11.5, 49.5), (5, 19.5, 47.5),
         (1, 18.5, 40.5), (0, 20.5, 38.5), (6, 15.5, 39.5), (0, 25.5, 38.5), (0, 18.5, 34.5),
         (3, 11.5, 33.5), (0, 5.5, 37.5), (1, 5.5, 39.5), (0, 13.5, 27.5), (1, 18.5, 27.5),
         (7, 15.5, 25.5), (8, 12.5, 54.5), (15, 20.5, 28.5), (16, 8.5, 17.5), (12, 25.5, 40.5), (13, 6.5, 30.5), (14, 24.5, 18.5), (0, 14.5, 18.5), (4, 5.5, 19.5), (1, 24.5, 15.5), (0, 22.5, 2.5), (17, 14.5, 1.5)]
# enemy spawns: class 0..4 = E1..E5, 5 = boss
SPAWNS = [(0, 11.5, 46.5), (0, 20.5, 46.5), (0, 12.5, 49.5), (0, 19.5, 49.5),
          (1, 12.5, 39.5), (0, 19.5, 41.5), (1, 19.5, 38.5), (0, 12.5, 41.5),
          (1, 25.5, 33.5), (1, 25.5, 35.5), (0, 25.5, 37.5),
          (1, 19.5, 32.5), (1, 13.5, 32.5), (0, 13.5, 34.5),
          (0, 5.5, 31.5), (1, 5.5, 33.5), (0, 5.5, 35.5),
          (1, 12.5, 24.5), (1, 19.5, 24.5),
          (2, 11.5, 16.5), (1, 13.5, 16.5), (2, 5.5, 16.5), (2, 7.5, 16.5), (2, 17.5, 16.5), (3, 19.5, 16.5),
          (3, 9.5, 6.5), (4, 12.5, 6.5), (3, 11.5, 10.5), (4, 20.5, 6.5), (3, 24.5, 6.5),
          (4, 15.5, 23.5), (4, 24.5, 17.5), (4, 7.5, 18.5),
          (5, 24.5, 2.5)]
EXIT = (9, 1, 13, 4)
# arena rooms (x0, y0, x1, y1, kind): the doors around them lock during the fight.
#   kind 1: the enemies wake up when the crystal of the room is taken;  kind 2: 2 s pause after the hero enters
ROOMS = [(10, 44, 21, 51, 1), (10, 37, 21, 42, 1), (23, 31, 27, 41, 1), (10, 31, 15, 35, 2), (17, 31, 21, 35, 1),
         (4, 29, 8, 40, 1), (10, 22, 21, 29, 1), (23, 14, 27, 20, 2)]
def room_of(x, y):
    for n, r in enumerate(ROOMS):
        if r[0] <= int(x) <= r[2] and r[1] <= int(y) <= r[3]:
            return n
    return 255
SPAWN_ROOM = [room_of(s[1], s[2]) if s[0] != 5 else 255 for s in SPAWNS]
GHOSTS = [(15.5, 59.5), (13.5, 58.5), (17.5, 58.5)]          # class 6 (optovisor-only ghosts) wake up in the start room
TXT = (15.5, 57.0 + 0.27)                                     # "LOOK" on the north wall of the start room
PLATE = (12.0, 13.0 - 40 / 256.0)                             # the TR button
SKIP_CHECKS = False
ITEM_Z = None
RMULT = [0] * len(ROOMS)
SPAWN_MULT = 4
FK = [[0] * W for _ in range(H)]      # special floors: 1 checkpoint 2 lava 3 acid 4 electro water 5 water 6 dark hole 7 radiation 8 hidden wall
TPS = []                             # teleport pairs (in_x, in_y, out_x, out_y)
MAPJ = os.environ.get("STAGE_JSON") or os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "maps", "stage1.json")


def export_map(path):
    import json
    obj = dict(stage=1, name="Stage 1", w=W, h=H, tiles=[[mp[y][x] for x in range(W)] for y in range(H)],
               start=list(START), exit=list(EXIT), plate=list(PLATE), look=list(TXT),
               items=[list(i) for i in ITEMS], spawns=[list(s) for s in SPAWNS], rooms=[list(r) for r in ROOMS],
               ghosts=[list(g_) for g_ in GHOSTS], floor_tex=0, ceil_tex=2, wall_tex={}, floor_tex_file=None, ceil_tex_file=None,
               music=dict(calm="lib:State", action="lib:DUNE", boss="lib:WIRED"), briefing=None)
    json.dump(obj, open(path, "w"), indent=0, separators=(",", ":"))


if os.path.exists(MAPJ):                     # the map editor's file replaces the built-in level
    import json
    J = json.load(open(MAPJ))
    mp = [[int(v) for v in row] for row in J["tiles"]]
    assert len(mp) == H and all(len(r) == W for r in mp), "map must be 32 x 64"
    START = tuple(J["start"]); EXIT = tuple(int(v) for v in J["exit"]); PLATE = tuple(J["plate"]); TXT = tuple(J["look"])
    ITEMS = [tuple(i[:3]) for i in J["items"]]; ITEM_Z = [(int(i[3]) - 1 if len(i) > 3 and int(i[3]) > 0 else 255) for i in J["items"]]; SPAWNS = [tuple(s) for s in J["spawns"]]
    _rr = [tuple(int(v) for v in r) for r in J["rooms"]]
    _rr = [r for r in _rr if len(r) < 6 or r[5]]                      # 6th field: waves on/off (off: no lock, its enemies are plain spawn points)
    ROOMS = [r[:5] for r in _rr]
    RMULT = [(r[6] if len(r) > 6 else 0) for r in _rr]                # 7th: enemy multiplier (0 = the old fixed totals 15 / 9)
    SPAWN_MULT = int(J.get("spawn_mult", 4))                          # enemies per spawn point outside the rooms (4 = the old behaviour)
    GHOSTS = [tuple(g_) for g_ in J["ghosts"]]
    FK = [[int(v) for v in r] for r in (J.get("floor_kind") or FK)]
    TPS = [tuple(int(v) for v in t_) for t_ in (J.get("teleports") or [])]
    while len(GHOSTS) < 3:
        GHOSTS.append(GHOSTS[0] if GHOSTS else START)
    doors = [(x, y, mp[y][x] - 10) for y in range(H) for x in range(W) if mp[y][x] >= 10]
    SPAWN_ROOM = [room_of(s[1], s[2]) if s[0] != 5 else 255 for s in SPAWNS]
    print("level from", os.path.relpath(MAPJ))
else:
    os.makedirs(os.path.dirname(MAPJ), exist_ok=True)
    export_map(MAPJ)

SPAWN_CNT = []
for _i, _s in enumerate(SPAWNS):
    SPAWN_CNT.append(1 if _s[0] == 5 else SPAWN_MULT)
ROOM_TOT = []
for _r in range(len(ROOMS)):
    _ids = [i for i in range(len(SPAWNS)) if SPAWN_ROOM[i] == _r]
    if RMULT[_r] and _ids:
        for i in _ids: SPAWN_CNT[i] = RMULT[_r]
        ROOM_TOT.append(RMULT[_r] * len(_ids))
    else:
        _t = 15 if ROOMS[_r][4] == 1 else 9
        for j, i in enumerate(_ids): SPAWN_CNT[i] = _t // len(_ids) + (1 if j < _t % len(_ids) else 0)
        ROOM_TOT.append(_t if _ids else 0)
assert all(v < 256 for v in ROOM_TOT)
# connectivity: flood fill with doors treated as open
seen = {(int(START[0]), int(START[1]))}
q = deque(seen)
openc = lambda x, y: mp[y][x] == 0 or mp[y][x] >= 10
while q:
    x, y = q.popleft()
    for t_ in TPS:                                   # a teleport joins its two ends
        if (t_[0], t_[1]) == (x, y) and (t_[2], t_[3]) not in seen: seen.add((t_[2], t_[3])); q.append((t_[2], t_[3]))
    for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
        n = (x + dx, y + dy)
        if n not in seen and openc(*n):
            seen.add(n)
            q.append(n)
tot = sum(1 for y in range(H) for x in range(W) if openc(x, y))
_un = [(x, y) for y in range(H) for x in range(W) if openc(x, y) and (x, y) not in seen]
_used = [(int(i[1]), int(i[2])) for i in ITEMS] + [(int(s[1]), int(s[2])) for s in SPAWNS]
assert not any(c in _used for c in _un), ("an item or an enemy stands in a closed pocket", [c for c in _un if c in _used][:4])
if _un: print("warning: %d closed floor tiles nobody can reach (empty pocket), e.g. %s" % (len(_un), _un[:3]))
for (k, x, y) in ITEMS:
    assert mp[int(y)][int(x)] == 0, ("item in wall", k, x, y)
for (k, x, y) in SPAWNS:
    assert mp[int(y)][int(x)] == 0, ("spawn in wall", k, x, y)
CPS = [(int(START[0]), int(START[1]))] + [(x, y) for y in range(H) for x in range(W) if FK[y][x] == 1 and (x, y) != (int(START[0]), int(START[1]))]
assert len(CPS) <= 9, "at most 8 checkpoints besides the start"
assert len(TPS) <= 4
for (x, y) in CPS:
    assert mp[y][x] == 0, ("checkpoint in a wall", x, y)
for t_ in TPS:
    assert mp[t_[1]][t_[0]] == 0 and mp[t_[3]][t_[2]] == 0, ("teleport end in a wall", t_)
for y in range(H):
    for x in range(W):
        assert FK[y][x] == 0 or mp[y][x] == 0, ("special floor in a wall", x, y)
if ITEM_Z is None: ITEM_Z = [255] * len(ITEMS)
# hazard zones: connected blocks (4-neighbours) of the same kind 2 / 4 / 8, numbered per kind in scan order (the editor shows them as 1, 2, ...)
FZ = [[0] * W for _ in range(H)]
ZN = {2: 0, 4: 0, 8: 0}
for y in range(H):
    for x in range(W):
        k_ = FK[y][x]
        if k_ in ZN and not FZ[y][x]:
            ZN[k_] += 1; stack = [(x, y)]; FZ[y][x] = ZN[k_]
            while stack:
                cx_, cy_ = stack.pop()
                for nx_, ny_ in ((cx_ + 1, cy_), (cx_ - 1, cy_), (cx_, cy_ + 1), (cx_, cy_ - 1)):
                    if 0 <= nx_ < W and 0 <= ny_ < H and FK[ny_][nx_] == k_ and not FZ[ny_][nx_]:
                        FZ[ny_][nx_] = ZN[k_]; stack.append((nx_, ny_))
assert all(v <= 8 for v in ZN.values()), ("at most 8 zones per hazard kind", ZN)
for i_, it_ in enumerate(ITEMS):
    if it_[0] >= 18 and ITEM_Z[i_] != 255:
        assert ITEM_Z[i_] < ZN[{18: 2, 19: 4, 20: 8}[it_[0]]], ("trigger points at a zone that does not exist", it_)
FK_ANY = any(FK[y][x] not in (0, 1) for y in range(H) for x in range(W)) or bool(TPS)
sym = ".#" + "".join("123456789")
for y in range(H):
    print("".join(" " if mp[y][x] == 0 else (chr(48 + mp[y][x]) if mp[y][x] < 10 else "ABCDE"[mp[y][x] - 10]) for x in range(W)))
# sectors (Doom/Portal style): free tiles connected without crossing a door; doors are the portals between them
sec = [[255] * W for _ in range(H)]
NSEC = 0
for y0 in range(H):
    for x0 in range(W):
        if mp[y0][x0] == 0 and sec[y0][x0] == 255:
            sec[y0][x0] = NSEC
            qq = deque([(x0, y0)])
            while qq:
                x, y = qq.popleft()
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    nx, ny = x + dx, y + dy
                    if 0 <= nx < W and 0 <= ny < H and mp[ny][nx] == 0 and sec[ny][nx] == 255:
                        sec[ny][nx] = NSEC
                        qq.append((nx, ny))
            NSEC += 1
assert NSEC < 255
door_sec = []
for (x, y, k) in doors:
    ss = sorted({sec[y + dy][x + dx] for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)) if 0 <= x + dx < W and 0 <= y + dy < H and sec[y + dy][x + dx] != 255})
    door_sec.append((ss + [255, 255])[:2] if len(ss) < 2 else ss[:2])
sec_c = []
for n in range(NSEC):
    tl = [(x, y) for y in range(H) for x in range(W) if sec[y][x] == n]
    cx = (min(t[0] for t in tl) + max(t[0] for t in tl)) / 2.0
    cy = (min(t[1] for t in tl) + max(t[1] for t in tl)) / 2.0
    b = min(tl, key=lambda t: (t[0] - cx) ** 2 + (t[1] - cy) ** 2)
    sec_c.append((b[0] * 256 + 128, b[1] * 256 + 128))
fx = lambda v: int(round(v * 256))
with open(os.path.join(OUT, "level.h"), "w") as f:
    f.write("/* generated by tools/gen_level.py */\n#ifndef LEVEL_H\n#define LEVEL_H\n")
    f.write("static const u8 level_map[%d] = {\n" % (W * H))
    for y in range(H):
        f.write(",".join(str(mp[y][x]) for x in range(W)) + ",\n")
    f.write("};\n#define START_X %d\n#define START_Y %d\n" % (fx(START[0]), fx(START[1])))
    f.write("#define NUM_SECS %d\nstatic const u8 sec_map[%d] = {\n" % (NSEC, W * H))
    for y in range(H):
        f.write(",".join(str(sec[y][x]) for x in range(W)) + ",\n")
    f.write("};\nstatic const u8 door_sec[%d][2] = {%s};\n" % (len(doors), ",".join("{%d,%d}" % tuple(d) for d in door_sec)))
    f.write("static const s16 sec_c[%d][2] = {%s};\n" % (NSEC, ",".join("{%d,%d}" % c for c in sec_c)))
    f.write("#define NUM_DOORS %d\nstatic const u8 door_xyk[%d][3] = {%s};\n" % (
        len(doors), len(doors), ",".join("{%d,%d,%d}" % d for d in doors)))
    f.write("#define NUM_ITEMS %d\nstatic const u8 item_kind[%d] = {%s};\nstatic const s16 item_pos[%d][2] = {%s};\n" % (
        len(ITEMS), len(ITEMS), ",".join(str(i[0]) for i in ITEMS), len(ITEMS),
        ",".join("{%d,%d}" % (fx(i[1]), fx(i[2])) for i in ITEMS)))
    f.write("#define NUM_SPAWNS %d\nstatic const u8 spawn_cls[%d] = {%s};\nstatic const s16 spawn_pos[%d][2] = {%s};\n" % (
        len(SPAWNS), len(SPAWNS), ",".join(str(i[0]) for i in SPAWNS), len(SPAWNS),
        ",".join("{%d,%d}" % (fx(i[1]), fx(i[2])) for i in SPAWNS)))
    f.write("static const u8 item_zone[%d] = {%s};\n" % (len(ITEMS), ",".join(str(v) for v in ITEM_Z) if ITEMS else "0"))
    f.write("#define EXIT_X0 %d\n#define EXIT_Y0 %d\n#define EXIT_X1 %d\n#define EXIT_Y1 %d\n" % tuple(v * 256 for v in (EXIT[0], EXIT[1], EXIT[2] + 1, EXIT[3] + 1)))
    f.write("#define NUM_ROOMS %d\nstatic const u8 room_rc[%d][5] = {%s};\nstatic const u8 spawn_room[%d] = {%s};\n" % (
        len(ROOMS), len(ROOMS), ",".join("{%d,%d,%d,%d,%d}" % r for r in ROOMS), len(SPAWNS), ",".join(str(v) for v in SPAWN_ROOM)))
    f.write("static const u8 spawn_cnt[%d] = {%s};\nstatic const u8 room_tot[%d] = {%s};\n" % (len(SPAWNS), ",".join(str(v) for v in SPAWN_CNT), max(1, len(ROOMS)), ",".join(str(v) for v in ROOM_TOT) if ROOM_TOT else "0"))
    f.write("static const s16 ghost_pos[3][2] = {%s};\n" % ",".join("{%d,%d}" % (fx(a), fx(b)) for a, b in GHOSTS))
    f.write("#define NUM_CP %d\nstatic const u8 cp_xy[%d][2] = {%s};\n" % (len(CPS), len(CPS), ",".join("{%d,%d}" % c for c in CPS)))
    f.write("#define NUM_TP %d\nstatic const u8 tp_xy[%d][4] = {%s};\n" % (len(TPS), max(1, len(TPS)), ",".join("{%d,%d,%d,%d}" % t_ for t_ in TPS) if TPS else "{0,0,0,0}"))
    f.write("#define FK_RAD %d\n" % (1 if any(FK[y][x] == 7 for y in range(H) for x in range(W)) else 0))
    f.write("#define FK_ANY %d\nstatic const u8 fk_map[%d] = {\n" % (1 if FK_ANY else 0, W * H))
    for y in range(H):
        f.write(",".join(str(FK[y][x] | (max(0, FZ[y][x] - 1) << 4)) for x in range(W)) + ",\n")      # kind | zone << 4
    f.write("};\n")
    f.write("#define PLATE_X %d\n#define PLATE_Y %d\n#define TXT_X %d\n#define TXT_Y %d\n#define NUM_CRYSTALS %d\n#endif\n" % (fx(PLATE[0]), fx(PLATE[1]), fx(TXT[0]), fx(TXT[1]), sum(1 for i in ITEMS if i[0] == 0)))
print("level ok", len(seen), "free;", len(ITEMS), "items;", len(SPAWNS), "spawns")
