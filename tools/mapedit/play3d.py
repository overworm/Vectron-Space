#!/usr/bin/env python3
"""3D walk-through preview of a map made in the editor (pygame/SDL + numpy).

  python3 tools/mapedit/play3d.py [stage]        # maps/stageN.json
  python3 tools/mapedit/play3d.py --file x.json  # any map file
  pip install pygame numpy pillow

Keys: W/S or Up/Down move, A/D strafe, Left/Right or mouse turn, Shift run,
E open/close the door in front, Tab noclip, M minimap, F toggle fog, R reload,
PgUp/PgDn previous/next stage, Esc quit.  The map file is watched: save in the
editor and the view reloads by itself (your position is kept).
Draws: wall textures (wall_tex overrides), doors, floor/ceiling textures, per-tile
floor_set/floor_tiles, open levels (sky texture or colour), items/enemies as markers.
This is a preview of what the data says, not the game's own renderer.
"""
import argparse, json, math, os, sys, time
import numpy as np
import pygame

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
MAPS = os.environ.get("MAPEDIT_MAPS") or os.path.join(ROOT, "maps")
STATIC = os.path.join(HERE, "static", "tex")
TS = 64                                   # texture size used by the renderer
RW, RH = 320, 200                         # internal resolution
SCALE = 3
DOOR_NAMES = ["auto door", "blue card", "yellow card", "red card", "TR gate"]
ITEMS = {0: "crystal", 1: "super bottle", 2: "blue card", 3: "yellow card", 4: "red card", 5: "quanton", 6: "plasmagun",
         7: "railgun", 8: "nightvisor", 12: "rocket launcher", 13: "beamchain", 14: "kozematy", 15: "optovisor",
         16: "redvisor", 17: "chaingun"}
KIND_COL = {0: (79, 227, 255), 1: (125, 255, 125), 2: (79, 123, 255), 3: (255, 225, 79), 4: (255, 90, 79), 5: (201, 201, 201),
            6: (125, 255, 125), 7: (255, 255, 255), 8: (125, 255, 125), 12: (255, 154, 60), 13: (79, 208, 255),
            14: (176, 79, 255), 15: (255, 216, 79), 16: (255, 79, 79), 17: (255, 183, 79)}


def load_tex(path, fallback=None):
    """-> uint8 array (TS,TS,3)"""
    for p in (path, fallback):
        if p and os.path.isfile(p):
            try:
                s = pygame.image.load(p).convert()
                s = pygame.transform.scale(s, (TS, TS))
                return np.ascontiguousarray(pygame.surfarray.array3d(s).swapaxes(0, 1))
            except pygame.error:
                pass
    a = np.zeros((TS, TS, 3), np.uint8)
    a[::8] = a[:, ::8] = (255, 0, 255)
    return a


def mpath(rel):
    return os.path.join(MAPS, rel) if rel else None


class World:
    def __init__(self, m):
        self.m = m
        self.W, self.H = m.get("w", 32), m.get("h", 64)
        self.tiles = np.array(m["tiles"], np.int32).reshape(self.H, self.W)
        self.open = bool(m.get("open"))
        wt = m.get("wall_tex") or {}
        # texture stack by tile id: 0 unused, 1-9 walls, 10-14 doors
        self.walls = np.zeros((15, TS, TS, 3), np.uint8)
        for i in range(1, 10):
            o = wt.get(str(i)) or wt.get(i)
            self.walls[i] = load_tex(mpath(o), os.path.join(STATIC, "wall_%d.png" % i))
        for k in range(5):
            self.walls[10 + k] = load_tex(os.path.join(STATIC, "door_%d.png" % k))
        ft = m.get("floor_tex", 0)
        base = load_tex(mpath(m.get("floor_tex_file")), os.path.join(STATIC, "floor_%d.png" % ft))
        fs = [base] + [load_tex(mpath(p)) for p in (m.get("floor_set") or [])]
        self.floors = np.stack(fs)
        ft_ = m.get("floor_tiles")
        self.floor_idx = np.zeros((self.H, self.W), np.int32)
        if ft_:
            a = np.array(ft_, np.int32)
            if a.shape == (self.H, self.W):
                self.floor_idx = np.clip(a, 0, len(fs) - 1)
        ct = m.get("ceil_tex", 2)
        self.ceil = load_tex(mpath(m.get("ceil_tex_file")), os.path.join(STATIC, "floor_%d.png" % ct))
        self.bright = int(m.get("brightness", 100)); self.hue = int(m.get("hue", 0))
        sc = m.get("sky_color") or "#4a8fd8"
        self.sky_col = np.array([int(sc[i:i + 2], 16) for i in (1, 3, 5)], np.float32)
        self.sky = load_tex(mpath(m.get("sky_tex_file"))) if m.get("sky_tex_file") else None
        fk = m.get("floor_kind")
        self.fk = np.array(fk, np.int32).reshape(self.H, self.W) if fk else np.zeros((self.H, self.W), np.int32)
        # markers
        s = []
        for i, tp in enumerate(m.get("teleports") or []):
            s.append((tp[0] + .5, tp[1] + .5, (79, 227, 255), 0.7, "IN%d" % (i + 1), 0.0))
            s.append((tp[2] + .5, tp[3] + .5, (255, 183, 79), 0.7, "OUT%d" % (i + 1), 0.0))
        for k, x, y, *_z in m.get("items", []):
            s.append((x, y, KIND_COL.get(k, (255, 255, 255)), 0.30, ITEMS.get(k, "?")[:1].upper(), 0.35))
        for c, x, y in m.get("spawns", []):
            s.append((x, y, (255, 40, 40) if c == 5 else (255, 120, 120), 0.9 if c == 5 else 0.6, "B" if c == 5 else str(c + 1), 0.0))
        for g in m.get("ghosts", []):
            s.append((g[0], g[1], (197, 139, 255), 0.6, "G", 0.0))
        s.append((m["plate"][0], m["plate"][1], (255, 123, 213), 0.35, "TR", 0.3))
        s.append((m["look"][0], m["look"][1], (255, 225, 79), 0.35, "L", 0.3))
        e = m.get("exit")
        if e:
            s.append(((e[0] + e[2] + 1) / 2, (e[1] + e[3] + 1) / 2, (79, 227, 255), 1.0, "EXIT", 0.0))
        self.sprites = s
        self.start = tuple(m["start"])

    def solid(self, x, y):
        xi, yi = int(math.floor(x)), int(math.floor(y))
        if xi < 0 or yi < 0 or xi >= self.W or yi >= self.H:
            return True
        return self.tiles[yi, xi] != 0


FK_COL = {1: (80, 230, 120), 2: (255, 90, 20), 3: (110, 255, 50), 4: (40, 110, 255), 5: (70, 180, 255),
          6: (50, 10, 90), 7: (230, 255, 60), 8: (255, 80, 216)}


def tint_floor(fl, kind, ftx, fty, t):
    """fl (n,m,3) float; kind (n,m) -> animated colour of the special floors"""
    out = fl
    for k, col in FK_COL.items():
        msk = kind == k
        if not msk.any():
            continue
        u = ftx.astype(np.float32) / TS
        v = fty.astype(np.float32) / TS
        if k == 2:      mod = 0.75 + 0.25 * np.sin(u * 14 + v * 9 + t * 3)
        elif k == 3:    mod = 0.7 + 0.3 * np.sin(u * 20 - t * 4) * np.cos(v * 17 + t * 3)
        elif k in (4, 5):
            mod = 0.8 + 0.2 * np.sin(u * 12 + t * 2) * np.sin(v * 12 - t * 2)
            if k == 4:  mod = mod + (np.sin(u * 40 + v * 33 + t * 25) > 0.93) * 1.2
        elif k == 6:    mod = 0.5 + 0.5 * np.sin(np.hypot(u - .5, v - .5) * 18 - t * 3)
        elif k == 7:    mod = 0.6 + 0.4 * np.sin(t * 6)
        elif k == 8:    mod = 0.35 + 0.25 * (((fty // 8) + (ftx // 8)) & 1)
        else:           mod = 0.8 + 0.2 * np.sin(t * 4)
        mod = np.broadcast_to(np.asarray(mod, np.float32), u.shape)
        c = np.array(col, np.float32)[None, None, :] * mod[:, :, None]
        a = 0.35 if k == 8 else 0.8
        out = np.where(msk[:, :, None], out * (1 - a) + c * a, out)
    return out


def tint_frame(w, f):
    """approximate preview of the level brightness / hue"""
    import colorsys
    br = getattr(w, "bright", 100) / 100.0; hue = getattr(w, "hue", 0)
    if br == 1.0 and not hue:
        return f
    x = f.astype(np.float32) / 255.0
    if hue:
        a = np.deg2rad(hue); c, s = np.cos(a), np.sin(a)
        M = np.array([[.299 + .701 * c + .168 * s, .587 - .587 * c + .330 * s, .114 - .114 * c - .497 * s],
                      [.299 - .299 * c - .328 * s, .587 + .413 * c + .035 * s, .114 - .114 * c + .292 * s],
                      [.299 - .300 * c + 1.25 * s, .587 - .588 * c - 1.05 * s, .114 + .886 * c - .203 * s]], np.float32)
        x = x @ M.T
    return np.clip(x * br * 255.0, 0, 255).astype(np.uint8)

def render(w, px, py, ang, fog=True, t=0.0):
    """returns (frame (RH,RW,3) uint8, zbuf (RW,))"""
    plen = 0.66
    dx, dy = math.cos(ang), math.sin(ang)
    plx, ply = -dy * plen, dx * plen
    camx = (2 * (np.arange(RW) + 0.5) / RW - 1).astype(np.float64)
    rdx = dx + plx * camx
    rdy = dy + ply * camx
    with np.errstate(divide="ignore"):
        ddx = np.abs(1.0 / rdx)
        ddy = np.abs(1.0 / rdy)
    mx = np.full(RW, int(math.floor(px)), np.int32)
    my = np.full(RW, int(math.floor(py)), np.int32)
    stx = np.where(rdx < 0, -1, 1)
    sty = np.where(rdy < 0, -1, 1)
    sdx = np.where(rdx < 0, (px - mx) * ddx, (mx + 1.0 - px) * ddx)
    sdy = np.where(rdy < 0, (py - my) * ddy, (my + 1.0 - py) * ddy)
    hit = np.zeros(RW, bool)
    out = np.zeros(RW, bool)
    side = np.zeros(RW, np.int32)
    tile = np.zeros(RW, np.int32)
    for _ in range(80):
        act = ~(hit | out)
        if not act.any():
            break
        sx = (sdx < sdy) & act
        sy = (~(sdx < sdy)) & act
        sdx = np.where(sx, sdx + ddx, sdx)
        mx = np.where(sx, mx + stx, mx)
        sdy = np.where(sy, sdy + ddy, sdy)
        my = np.where(sy, my + sty, my)
        side = np.where(sx, 0, np.where(sy, 1, side))
        oob = (mx < 0) | (my < 0) | (mx >= w.W) | (my >= w.H)
        out |= act & oob
        t = w.tiles[np.clip(my, 0, w.H - 1), np.clip(mx, 0, w.W - 1)]
        h = act & ~oob & (t != 0)
        tile = np.where(h, t, tile)
        hit |= h
    perp = np.where(side == 0, sdx - ddx, sdy - ddy)
    perp = np.where(hit, np.maximum(perp, 0.05), 1e9)
    wallx = np.where(side == 0, py + perp * rdy, px + perp * rdx)
    wallx -= np.floor(wallx)
    tx = (wallx * TS).astype(np.int32)
    tx = np.where(((side == 0) & (rdx > 0)) | ((side == 1) & (rdy < 0)), TS - 1 - tx, tx)
    tx = np.clip(tx, 0, TS - 1)

    ys = np.arange(RH, dtype=np.float64)[:, None]
    half = RH / 2
    # floor
    rows = np.arange(RH // 2, RH, dtype=np.float64)
    rd = (half / (rows - half + 0.5))[:, None]
    fxw = px + rd * rdx[None, :]
    fyw = py + rd * rdy[None, :]
    fxi = np.clip(np.floor(fxw).astype(np.int32), 0, w.W - 1)
    fyi = np.clip(np.floor(fyw).astype(np.int32), 0, w.H - 1)
    ftx = ((fxw - np.floor(fxw)) * TS).astype(np.int32) % TS
    fty = ((fyw - np.floor(fyw)) * TS).astype(np.int32) % TS
    frame = np.zeros((RH, RW, 3), np.float32)
    fl = w.floors[w.floor_idx[fyi, fxi], fty, ftx].astype(np.float32)
    if fog:
        fl *= np.clip(1.25 - rd / 14.0, 0.15, 1.0)[:, :, None]
    fl = tint_floor(fl, w.fk[fyi, fxi], ftx, fty, t)
    frame[RH // 2:] = fl
    # ceiling / sky
    if not w.open:
        cy = np.arange(0, RH // 2, dtype=np.float64)
        rdc = (half / (half - cy - 0.5))[:, None]
        cxw = px + rdc * rdx[None, :]
        cyw = py + rdc * rdy[None, :]
        ctx = ((cxw - np.floor(cxw)) * TS).astype(np.int32) % TS
        cty = ((cyw - np.floor(cyw)) * TS).astype(np.int32) % TS
        c = w.ceil[cty, ctx].astype(np.float32)
        if fog:
            c *= np.clip(1.25 - rdc / 14.0, 0.15, 1.0)[:, :, None]
        frame[:RH // 2] = c
    else:
        if w.sky is not None:
            a = ang + np.arctan(camx * plen)
            u = ((a / (2 * math.pi) * 2.0) % 1.0 * TS * 2).astype(np.int32) % TS
            v = np.clip((np.arange(RH // 2) * TS // (RH // 2)), 0, TS - 1)
            frame[:RH // 2] = w.sky[v[:, None], u[None, :]].astype(np.float32)
        else:
            g = np.linspace(0.55, 1.0, RH // 2, dtype=np.float32)[:, None, None]
            frame[:RH // 2] = (w.sky_col[None, None, :] * g)
    # walls
    lh = RH / perp
    top = half - lh / 2
    mask = (ys >= top[None, :]) & (ys < (top + lh)[None, :]) & hit[None, :]
    ty = np.clip(((ys - top[None, :]) / lh[None, :] * TS).astype(np.int32), 0, TS - 1)
    wc = w.walls[np.clip(tile, 0, 14)[None, :], ty, tx[None, :]].astype(np.float32)
    sh = np.where(side == 1, 0.72, 1.0)
    if fog:
        sh = sh * np.clip(1.25 - perp / 14.0, 0.15, 1.0)
    wc *= sh[None, :, None]
    frame = np.where(mask[:, :, None], wc, frame)
    return np.clip(frame, 0, 255).astype(np.uint8), perp


def draw_sprites(w, surf, zbuf, px, py, ang, font):
    plen = 0.66
    dx, dy = math.cos(ang), math.sin(ang)
    plx, ply = -dy * plen, dx * plen
    det = plx * dy - dx * ply
    vis = []
    for (x, y, col, size, label, lift) in w.sprites:
        rx, ry = x - px, y - py
        tx = (dy * rx - dx * ry) / det
        ty = (-ply * rx + plx * ry) / det
        if ty > 0.15 and ty < 24:
            vis.append((ty, tx, col, size, label, lift))
    vis.sort(reverse=True)
    for ty, tx, col, size, label, lift in vis:
        sx = int(RW / 2 * (1 + tx / ty))
        sz = int(RH / ty * size)
        if sz < 2:
            continue
        cy = int(RH / 2 + RH / ty * (0.5 - size / 2 - lift) + sz / 2 - sz / 2)
        r = sz // 2
        for cx in range(max(0, sx - r), min(RW, sx + r)):
            if ty >= zbuf[cx]:
                continue
            u = (cx - sx) / max(r, 1)
            hh = int(r * math.sqrt(max(0.0, 1 - u * u)))
            if hh > 0:
                c = tuple(int(v * (0.65 + 0.35 * (1 - abs(u)))) for v in col)
                pygame.draw.line(surf, c, (cx, cy - hh), (cx, cy + hh))
        if 0 <= sx < RW and ty < zbuf[min(RW - 1, max(0, sx))] and sz > 10:
            t = font.render(label, False, (0, 0, 0))
            surf.blit(t, (sx - t.get_width() // 2, cy - t.get_height() // 2))


def minimap(w, surf, px, py, ang):
    c = 3
    r = 12
    ox, oy = 6, 6
    x0, y0 = int(px) - r, int(py) - r
    pygame.draw.rect(surf, (0, 0, 0), (ox - 2, oy - 2, 2 * r * c + 4, 2 * r * c + 4))
    for yy in range(2 * r):
        for xx in range(2 * r):
            tx, ty = x0 + xx, y0 + yy
            if 0 <= tx < w.W and 0 <= ty < w.H:
                t = w.tiles[ty, tx]
                col = (28, 32, 48) if t == 0 else (120, 120, 140) if t < 10 else (230, 200, 70)
                pygame.draw.rect(surf, col, (ox + xx * c, oy + yy * c, c - 1, c - 1))
    cx, cy = ox + (px - x0) * c, oy + (py - y0) * c
    pygame.draw.circle(surf, (111, 211, 138), (cx, cy), 3)
    pygame.draw.line(surf, (255, 255, 255), (cx, cy), (cx + math.cos(ang) * 10, cy + math.sin(ang) * 10))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("stage", nargs="?", type=int, default=1)
    ap.add_argument("--file")
    ap.add_argument("--shot")          # save a screenshot and exit (for tests)
    ap.add_argument("--at", help="x,y,angle_deg start override")
    a = ap.parse_args()
    stage = a.stage
    path = a.file or os.path.join(MAPS, "stage%d.json" % stage)
    pygame.init()
    scr = pygame.display.set_mode((RW * SCALE, RH * SCALE))
    pygame.display.set_caption("map preview")
    surf = pygame.Surface((RW, RH))
    font = pygame.font.Font(None, 14)
    big = pygame.font.Font(None, 18)

    def load():
        return World(json.load(open(path)))
    w = load()
    mtime = os.path.getmtime(path)
    px, py = w.start
    ang = -math.pi / 2
    if a.at:
        v = a.at.split(",")
        px, py, ang = float(v[0]), float(v[1]), math.radians(float(v[2]))
    noclip = mini = fog = True
    noclip = False
    doors_open = set()
    clock = pygame.time.Clock()
    last_chk = 0.0
    if not a.shot:
        pygame.event.set_grab(True)
        pygame.mouse.set_visible(False)
    frames = 0
    msg = ""
    while True:
        dt = min(clock.tick(30) / 1000.0, 0.1)
        for e in pygame.event.get():
            if e.type == pygame.QUIT:
                return
            if e.type == pygame.KEYDOWN:
                if e.key == pygame.K_ESCAPE:
                    return
                if e.key == pygame.K_TAB:
                    noclip = not noclip
                elif e.key == pygame.K_m:
                    mini = not mini
                elif e.key == pygame.K_f:
                    fog = not fog
                elif e.key == pygame.K_r:
                    w = load()
                elif e.key in (pygame.K_PAGEUP, pygame.K_PAGEDOWN) and not a.file:
                    stage = max(1, min(6, stage + (1 if e.key == pygame.K_PAGEDOWN else -1)))
                    path = os.path.join(MAPS, "stage%d.json" % stage)
                    if os.path.isfile(path):
                        w = load()
                        px, py = w.start
                        mtime = os.path.getmtime(path)
                    else:
                        stage -= 1 if e.key == pygame.K_PAGEDOWN else -1
                        path = os.path.join(MAPS, "stage%d.json" % stage)
                        msg = "no such stage file"
                elif e.key == pygame.K_e:
                    # door straight ahead within 1.6 tiles
                    for d in np.arange(0.4, 1.7, 0.1):
                        tx, ty = int(px + math.cos(ang) * d), int(py + math.sin(ang) * d)
                        if 0 <= tx < w.W and 0 <= ty < w.H and w.tiles[ty, tx] >= 10:
                            msg = "door: " + DOOR_NAMES[w.tiles[ty, tx] - 10]
                            w.tiles[ty, tx] = 0
                            doors_open.add((tx, ty))
                            break
            if e.type == pygame.MOUSEMOTION and not a.shot:
                ang += e.rel[0] * 0.004
        k = pygame.key.get_pressed()
        spd = (4.2 if k[pygame.K_LSHIFT] else 2.2) * dt
        if k[pygame.K_LEFT]:
            ang -= 2.2 * dt
        if k[pygame.K_RIGHT]:
            ang += 2.2 * dt
        mvx = mvy = 0.0
        fwd = (k[pygame.K_w] or k[pygame.K_UP]) - (k[pygame.K_s] or k[pygame.K_DOWN])
        strf = k[pygame.K_d] - k[pygame.K_a]
        mvx += math.cos(ang) * fwd * spd - math.sin(ang) * strf * spd
        mvy += math.sin(ang) * fwd * spd + math.cos(ang) * strf * spd
        rad = 0.22
        for (ddx, ddy) in ((mvx, 0), (0, mvy)):
            nx, ny = px + ddx, py + ddy
            if noclip or not any(w.solid(nx + sx * rad, ny + sy * rad) for sx in (-1, 1) for sy in (-1, 1)):
                px, py = nx, ny
        now = time.time()
        if now - last_chk > 0.6 and not a.shot:
            last_chk = now
            try:
                mt = os.path.getmtime(path)
                if mt != mtime:
                    time.sleep(0.05)
                    w = load()
                    mtime = mt
                    msg = "reloaded"
            except (OSError, ValueError):
                pass
        frame, zbuf = render(w, px, py, ang, fog, time.time() % 1000)
        frame = tint_frame(w, frame)
        pygame.surfarray.blit_array(surf, frame.swapaxes(0, 1))
        draw_sprites(w, surf, zbuf, px, py, ang, font)
        if mini:
            minimap(w, surf, px, py, ang)
        info = "%s  x %.1f y %.1f%s  fps %d" % (w.m.get("name", ""), px, py, "  NOCLIP" if noclip else "", clock.get_fps())
        t = big.render(info, False, (255, 255, 255), (0, 0, 0))
        surf.blit(t, (RW - t.get_width() - 3, 3))
        if msg:
            tm = big.render(msg, False, (255, 225, 79), (0, 0, 0))
            surf.blit(tm, (RW // 2 - tm.get_width() // 2, RH - 14))
            if frames % 90 == 89:
                msg = ""
        pygame.transform.scale(surf, scr.get_size(), scr)
        pygame.display.flip()
        frames += 1
        if a.shot and frames >= 3:
            pygame.image.save(scr, a.shot)
            return


if __name__ == "__main__":
    main()
