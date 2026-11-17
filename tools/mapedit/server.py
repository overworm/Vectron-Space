#!/usr/bin/env python3
"""server.py - 2D map editor for the Vectron Space SNES game (web app, python3 standard library + Pillow).

    python3 tools/mapedit/server.py [port]        then open http://localhost:8766/

Maps live in  maps/stageN.json  (N = 1..6).  The build (tools/gen_level.py, gen_art.py, gen_screens.py,
tools/stage_music.py) takes maps/stage1.json.  Uploaded files go to maps/textures, maps/music, maps/briefings.
"""
import json, os, re, sys, io, zipfile, subprocess
from collections import deque
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
MAPS = os.environ.get("MAPEDIT_MAPS") or os.path.join(ROOT, "maps")
W, H = 32, 64
PLAYER = None
ITEM_KINDS = {0: "crystal", 1: "super bottle", 2: "blue card", 3: "yellow card", 4: "red card", 5: "quanton", 6: "plasmagun",
              7: "railgun", 8: "nightvisor", 12: "rocket launcher", 13: "beamchain", 14: "kozematy", 15: "optovisor",
              16: "redvisor", 17: "chaingun",
              18: "waterbox trigger (hydrant: lava -> water)", 19: "electrobox trigger (electro water -> water)", 20: "hiddenbox trigger (hidden walls off)"}
FLOOR_KINDS = {1: "checkpoint", 2: "lava floor", 3: "acid floor", 4: "water electro", 5: "water (safe)", 6: "dark hole", 7: "radiation zone", 8: "hidden wall"}
SPAWN_CLASSES = {0: "E1 wheel", 1: "E2 skeleton", 2: "E3 biped (cloaked)", 3: "E4 devourer", 4: "E5 soldier", 5: "BOSS penguin"}
MUSIC_LIB = {"calm": ["State (ambient, default)"], "action": ["DUNE (breakbeat, default)"], "boss": ["WIRED (rhythm, default)"]}
TEX_EXT = (".png", ".jpg", ".jpeg")
CATS = ["sky", "space", "scifi", "ship", "stone", "acid", "lava", "water", "sand", "misc"]        # suggested folders under maps/textures/
for d in ("textures", "music", "briefings"):
    os.makedirs(os.path.join(MAPS, d), exist_ok=True)


def blank(stage):
    t = [[1] * W for _ in range(H)]
    for y in range(56, 62):
        for x in range(12, 20):
            t[y][x] = 0
    for y in range(1, 5):
        for x in range(8, 28):
            t[y][x] = 0
    for y in range(5, 56):
        t[y][15] = t[y][16] = 0
    return dict(stage=stage, name="Stage %d" % stage, w=W, h=H, tiles=t, start=[15.5, 60.5], exit=[9, 1, 13, 4], plate=[12.0, 12.8],
                look=[15.5, 55.5], items=[[0, 20.5, 2.5]] * 0, spawns=[[5, 24.5, 2.5]], rooms=[], spawn_mult=1, brightness=100, hue=0, ghosts=[[15.5, 59.5], [13.5, 58.5], [17.5, 58.5]],
                floor_tex=0, ceil_tex=2, wall_tex={}, floor_tex_file=None, ceil_tex_file=None,
                music=dict(calm="lib:State", action="lib:DUNE", boss="lib:WIRED"), briefing=None)


def defaults(m):
    """older files get the new fields: open level, sky, per-tile floor layer"""
    m.setdefault("open", False)                    # open level: no ceiling, a sky instead
    m.setdefault("sky_tex_file", None)
    m.setdefault("sky_color", "#4a8fd8")
    m.setdefault("floor_set", [])                  # up to 8 floor textures ("textures/cat/name.png"); index 0 = m.floor_tex / floor_tex_file
    m.setdefault("floor_tiles", None)              # None = everything uses the level floor; else [H][W] of indices into floor_set + 1 (0 = level floor)
    m.setdefault("wall_tex", {})
    m.setdefault("floor_kind", [[0] * W for _ in range(H)])   # special floor per tile: see FLOOR_KINDS (0 = plain)
    m.setdefault("teleports", [])                              # [[in_x, in_y, out_x, out_y], ...] tile cells, up to 4 pairs
    return m


def load_map(stage):
    p = os.path.join(MAPS, "stage%d.json" % stage)
    if os.path.exists(p):
        return defaults(json.load(open(p)))
    return defaults(blank(stage))


def validate(m):
    """checks the things the game relies on; returns a list of (level, text)"""
    out = []
    t = m["tiles"]
    if len(t) != H or any(len(r) != W for r in t):
        return [("error", "the map must be %d x %d tiles" % (W, H))]
    free = lambda x, y: 0 <= x < W and 0 <= y < H and (t[y][x] == 0 or t[y][x] >= 10)
    edge = [(x, y) for y in range(H) for x in range(W) if (x in (0, W - 1) or y in (0, H - 1)) and (t[y][x] == 0 or t[y][x] >= 10)]
    if edge:
        out.append(("error", "open tiles on the map border (the game's rays would leave the map): %s" % ", ".join("%d,%d" % e for e in edge[:6])))
    sx, sy = int(m["start"][0]), int(m["start"][1])
    if t[sy][sx] != 0:
        out.append(("error", "the start point is inside a wall"))
    seen = {(sx, sy)}
    q = deque(seen)
    while q:
        x, y = q.popleft()
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            n = (x + dx, y + dy)
            if n not in seen and free(*n):
                seen.add(n); q.append(n)
    tot = sum(1 for y in range(H) for x in range(W) if free(x, y))
    if len(seen) != tot:
        out.append(("error", "%d free tiles cannot be reached from the start (doors count as open)" % (tot - len(seen))))
    for x in range(W):
        if t[0][x] == 0 or t[H - 1][x] == 0:
            out.append(("error", "the map border must be a wall (column %d)" % x)); break
    for y in range(H):
        if t[y][0] == 0 or t[y][W - 1] == 0:
            out.append(("error", "the map border must be a wall (row %d)" % y)); break
    for (x, y) in ((x, y) for y in range(H) for x in range(W) if t[y][x] >= 10):
        sol = lambda v: v != 0                                  # walls and neighbouring doors both close the frame
        a = (sol(t[y][x - 1]) if x > 0 else True) and (sol(t[y][x + 1]) if x < W - 1 else True)
        b = (sol(t[y - 1][x]) if y > 0 else True) and (sol(t[y + 1][x]) if y < H - 1 else True)
        if not (a or b):
            out.append(("error", "door at %d,%d needs walls on two opposite sides" % (x, y)))
    for k, x, y, *_z in m["items"]:
        if t[int(y)][int(x)] != 0:
            out.append(("error", "item %s at %.1f,%.1f is not on a floor tile" % (ITEM_KINDS.get(k, k), x, y)))
    for c, x, y in m["spawns"]:
        if t[int(y)][int(x)] != 0:
            out.append(("error", "spawn at %.1f,%.1f is not on a floor tile" % (x, y)))
    if sum(1 for s in m["spawns"] if s[0] == 5) != 1:
        out.append(("warn", "exactly one boss spawn (class BOSS) is expected"))
    if sum(1 for i in m["items"] if i[0] == 0) < 8:
        out.append(("warn", "fewer than 8 crystals - the exit needs 8"))
    ex = m["exit"]
    if not (0 <= ex[0] <= ex[2] < W and 0 <= ex[1] <= ex[3] < H):
        out.append(("error", "the exit rectangle is outside the map"))
    if len(m["spawns"]) > 60 or len(m["items"]) > 60:
        out.append(("warn", "many objects: the game was tuned for about 34 spawns and 26 items"))
    ft = m.get("floor_tiles")
    if ft is not None and (len(ft) != H or any(len(r) != W for r in ft)):
        out.append(("error", "floor_tiles must be %d x %d" % (W, H)))
    if len(m.get("floor_set") or []) > 8:
        out.append(("error", "at most 8 floor textures in the floor set"))
    fk = m.get("floor_kind")
    if fk is None or len(fk) != H or any(len(r) != W for r in fk):
        out.append(("error", "floor_kind must be %d x %d" % (W, H)))
    else:
        cnt = {}
        for y in range(H):
            for x in range(W):
                k = fk[y][x]
                if k:
                    cnt[k] = cnt.get(k, 0) + 1
                    if t[y][x] != 0:
                        out.append(("error", "special floor (%s) on a wall/door tile %d,%d" % (FLOOR_KINDS.get(k, k), x, y)))
        if cnt.get(1, 0) > 8:
            out.append(("error", "at most 8 checkpoints (the start point is the first one)"))
        if fk[sy][sx] not in (0, 1, 5):
            out.append(("error", "the start point stands on a dangerous floor"))
        kinds = {it[0] for it in m["items"]}
        if cnt.get(2) and 18 not in kinds:
            out.append(("warn", "lava floor but no waterbox trigger (hydrant): the lava can never be turned off"))
        if cnt.get(4) and 19 not in kinds:
            out.append(("warn", "electro water but no electrobox trigger"))
        if cnt.get(8) and 20 not in kinds:
            out.append(("warn", "hidden walls but no hiddenbox trigger"))
        zn = {2: 0, 4: 0, 8: 0}
        seenz = set()
        for y0 in range(H):
            for x0 in range(W):
                k0 = fk[y0][x0]
                if k0 in zn and (x0, y0) not in seenz:
                    zn[k0] += 1; st = [(x0, y0)]; seenz.add((x0, y0))
                    while st:
                        a, b = st.pop()
                        for c, d in ((a + 1, b), (a - 1, b), (a, b + 1), (a, b - 1)):
                            if 0 <= c < W and 0 <= d < H and fk[d][c] == k0 and (c, d) not in seenz:
                                seenz.add((c, d)); st.append((c, d))
        for k0, n0 in zn.items():
            if n0 > 8:
                out.append(("error", "at most 8 zones per hazard kind (kind %d has %d)" % (k0, n0)))
        for it in m["items"]:
            if it[0] >= 18 and len(it) > 3 and int(it[3]) > zn[{18: 2, 19: 4, 20: 8}[it[0]]]:
                out.append(("error", "trigger at %.1f,%.1f points at zone %d, but there are only %d" % (it[1], it[2], int(it[3]), zn[{18: 2, 19: 4, 20: 8}[it[0]]])))
        for (k, x, y, *_z) in m["items"]:
            if fk[int(y)][int(x)] in (2, 3, 4, 6, 7, 8):
                out.append(("warn", "item %s stands on a dangerous floor at %d,%d" % (ITEM_KINDS.get(k, k), int(x), int(y))))
    tp = m.get("teleports") or []
    if len(tp) > 4:
        out.append(("error", "at most 4 teleport pairs"))
    for p in tp:
        if len(p) != 4 or any(not (0 <= p[i] < (W if i % 2 == 0 else H)) for i in range(4)) or t[p[1]][p[0]] != 0 or t[p[3]][p[2]] != 0:
            out.append(("error", "teleport %s: both ends must be floor tiles inside the map" % (p,)))
    if m.get("open"):
        out.append(("warn", "open level: the game must draw the sky instead of the ceiling (the editor only stores the flag, sky texture and colour)"))
    for r in m["rooms"]:
        if len(r) > 6 and not (1 <= int(r[6]) <= 8):
            out.append(("error", "room multiplier must be 1..8"))
    if not (40 <= int(m.get("brightness", 100)) <= 200) or not (-180 <= int(m.get("hue", 0)) <= 180):
        out.append(("error", "brightness must be 40..200 and hue -180..180"))
    if not (1 <= int(m.get("spawn_mult", 1)) <= 8):
        out.append(("error", "spawn_mult must be 1..8"))
    if len(m["rooms"]) > 12:
        out.append(("error", "at most 12 arena rooms"))
    if not out:
        out.append(("ok", "map is valid: %d free tiles, %d items, %d spawns, %d arena rooms" % (tot, len(m["items"]), len(m["spawns"]), len(m["rooms"]))))
    return out


def lib():
    def ls(d, exts):
        p = os.path.join(MAPS, d)
        return sorted(f for f in os.listdir(p) if f.lower().endswith(exts))
    tex = []
    base = os.path.join(MAPS, "textures")
    for dp, dn, fn in os.walk(base):
        for f in fn:
            if f.lower().endswith(TEX_EXT):
                rel = os.path.relpath(os.path.join(dp, f), base).replace(os.sep, "/")
                tex.append(dict(path="textures/" + rel, cat=rel.split("/")[0] if "/" in rel else "misc", name=f))
    tex.sort(key=lambda t: (t["cat"], t["name"].lower()))
    cats = sorted({t["cat"] for t in tex} | set(CATS), key=lambda c: (CATS.index(c) if c in CATS else 99, c))
    return dict(items=ITEM_KINDS, fkinds=FLOOR_KINDS, classes=SPAWN_CLASSES, textures=ls("textures", TEX_EXT), tex=tex, cats=cats,
                music=ls("music", (".xm", ".s3m")), briefings=ls("briefings", (".png", ".jpg", ".jpeg")),
                music_lib=MUSIC_LIB, stages=[n for n in range(1, 7) if os.path.exists(os.path.join(MAPS, "stage%d.json" % n))])


class H_(BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def send(self, code, body, ctype="application/json"):
        if isinstance(body, (dict, list)):
            body = json.dumps(body).encode()
        elif isinstance(body, str):
            body = body.encode()
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        u = urlparse(self.path)
        q = parse_qs(u.query)
        if u.path == "/api/lib":
            return self.send(200, lib())
        if u.path == "/api/map":
            return self.send(200, load_map(int(q.get("stage", ["1"])[0])))
        if u.path.startswith("/maps/"):
            p = os.path.normpath(os.path.join(MAPS, u.path[len("/maps/"):]))
            if p.startswith(MAPS + os.sep) and os.path.isfile(p):
                return self.send(200, open(p, "rb").read(), "image/png" if p.endswith("png") else "image/jpeg")
            return self.send(404, "no")
        rel = "index.html" if u.path in ("/", "") else u.path.lstrip("/")
        p = os.path.normpath(os.path.join(HERE, "static", rel))
        if p.startswith(os.path.join(HERE, "static")) and os.path.isfile(p):
            ct = {"html": "text/html", "js": "text/javascript", "png": "image/png", "css": "text/css"}.get(p.rsplit(".", 1)[-1], "application/octet-stream")
            return self.send(200, open(p, "rb").read(), ct + ("; charset=utf-8" if ct.startswith("text") else ""))
        self.send(404, "not found", "text/plain")

    def do_POST(self):
        u = urlparse(self.path)
        q = parse_qs(u.query)
        n = int(self.headers.get("Content-Length", 0))
        body = self.rfile.read(n)
        try:
            if u.path == "/api/map":
                m = json.loads(body)
                stage = int(q.get("stage", ["1"])[0])
                m["stage"] = stage
                res = validate(m)
                json.dump(m, open(os.path.join(MAPS, "stage%d.json" % stage), "w"), separators=(",", ":"))
                return self.send(200, dict(saved=True, checks=res))
            if u.path == "/api/play":                           # 3D preview window (needs a display on this machine)
                global PLAYER
                json.loads(body)
                pf = os.path.join(MAPS, "_preview.json")
                open(pf, "wb").write(body)
                if PLAYER is not None and PLAYER.poll() is None:
                    return self.send(200, dict(started=False))   # open window reloads the file by itself
                PLAYER = subprocess.Popen([sys.executable, os.path.join(HERE, "play3d.py"), "--file", pf],
                                          env=dict(os.environ, MAPEDIT_MAPS=MAPS))
                return self.send(200, dict(started=True))
            if u.path == "/api/validate":
                return self.send(200, dict(checks=validate(json.loads(body))))
            if u.path == "/api/upload":
                kind = q["kind"][0]
                name = re.sub(r"[^A-Za-z0-9_.-]", "_", q["name"][0])
                cat = re.sub(r"[^A-Za-z0-9_-]", "_", (q.get("cat", ["misc"])[0] or "misc"))
                if kind == "texzip":                                # a zip of pictures: its folders become categories
                    from PIL import Image
                    zf = zipfile.ZipFile(io.BytesIO(body))
                    n = 0
                    for zi in zf.infolist():
                        if zi.is_dir() or not zi.filename.lower().endswith(TEX_EXT) or zi.file_size > 16 * 1024 * 1024:
                            continue
                        parts = [re.sub(r"[^A-Za-z0-9_.-]", "_", x) for x in zi.filename.replace("\\", "/").split("/") if x and x not in (".", "..") and not x.startswith("__MACOSX")]
                        if not parts:
                            continue
                        c = re.sub(r"[^A-Za-z0-9_-]", "_", parts[-2]).lower() if len(parts) > 1 else cat
                        data = zf.read(zi)
                        try:
                            Image.open(io.BytesIO(data)).verify()
                        except Exception:                                # noqa
                            continue
                        os.makedirs(os.path.join(MAPS, "textures", c), exist_ok=True)
                        open(os.path.join(MAPS, "textures", c, parts[-1]), "wb").write(data)
                        n += 1
                    return self.send(200, dict(saved="%d textures" % n))
                if kind in ("texture", "briefing"):
                    from PIL import Image
                    im = Image.open(io.BytesIO(body)); im.verify()
                    if not name.lower().endswith((".png", ".jpg", ".jpeg")):
                        name += ".png"
                    d = ("textures/" + cat) if kind == "texture" else "briefings"
                    os.makedirs(os.path.join(MAPS, d), exist_ok=True)
                elif kind == "music":
                    if not name.lower().endswith((".xm", ".s3m")):
                        return self.send(400, dict(error="music must be .xm or .s3m (tracker modules)"))
                    d = "music"
                else:
                    return self.send(400, dict(error="unknown kind"))
                open(os.path.join(MAPS, d, name), "wb").write(body)
                return self.send(200, dict(saved=d + "/" + name))
        except Exception as e:                                      # noqa
            return self.send(400, dict(error=str(e)))
        self.send(404, dict(error="no such call"))


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8766
    print("map editor: http://localhost:%d/   (maps in %s)" % (port, MAPS))
    ThreadingHTTPServer(("127.0.0.1", port), H_).serve_forever()
