#!/usr/bin/env python3
"""xm2it_chip.py - a small FastTracker .xm chip tune (<= 8 channels) -> Impulse Tracker module for SNESMOD, 1:1 with effects.

Two sections of ONE module share the samples: CALM (IT orders 0..) and ACTION; the last row of each section jumps back to
its own first order (Bxx), the game switches with snesmodPlay(order).
XM note C-4 (49) at the sample's own pitch = IT note C-5 (60), so IT note = XM note + 11.
"""
import os, sys
import numpy as np
from scipy.signal import resample_poly
from math import gcd
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import xmparse
import gen_audio as G

CALM_ORDERS = [0, 1, 2, 3, 4, 5, 6, 7]            # XM order positions (kick drum groove)
ACTION_ORDERS = [22, 23, 24, 25, 26, 27]           # XM order positions (dense lead / arps)
DOWN = {4: 4, 16: 4, 6: 2, 8: 2, 2: 3, 7: 2}                  # instrument -> integer down-sampling factor (SPC RAM)


def resample(x, k):
    return resample_poly(x, 1, k) if k > 1 else x


def sample_for(inst_no, inst):
    s = inst['samples'][0]
    x = s['data'].copy()
    ls, ll, typ = s['ls'], s['ll'], s['typ']
    k = DOWN.get(inst_no, 1)
    rate = 8363 * 2 ** ((s['rel'] + s['fin'] / 128) / 12) / k
    x = resample(x, k)
    ls, ll = ls // k, ll // k
    loop = None
    if typ and ll > 16:
        end = ls + ll
        x = x[:end]
        n = min(ll // 6, 300)
        if n > 8:                                    # make the loop click-free (also replaces ping-pong)
            t = np.linspace(0, np.pi / 2, n)
            x[end - n:end] = x[end - n:end] * np.cos(t) + x[ls - n if ls >= n else 0:(ls - n if ls >= n else 0) + n] * np.sin(t) if ls >= n else x[end - n:end]
        ll16 = (end - ls) // 16 * 16
        x = x[:ls + ll16]
        loop = (ls, ls + ll16)
    return G.Sample("I%d" % inst_no, x * (s['vol'] / 64), int(rate), loop=loop)


def bcd(p):
    return (p >> 4) * 10 + (p & 15)


def convert_cell(cell, last_inst):
    n, i, v, e, p = cell
    note = inst = vol = None
    cmd = par = None
    if 1 <= n <= 96:
        note = n + 11
        if i: last_inst[0] = i
        inst = last_inst[0]
    elif n == 97:
        note = 254
    if 0x10 <= v <= 0x50:
        vol = v - 0x10
    if e == 0xC and vol is None:
        vol = min(64, p)
    elif e == 0x0 and p:
        cmd, par = 10, p                                    # arpeggio -> J
    elif e == 0x1 and p: cmd, par = 6, p                    # porta up   -> F
    elif e == 0x2 and p: cmd, par = 5, p                    # porta down -> E
    elif e == 0x3: cmd, par = 7, p                          # tone porta -> G
    elif e == 0x9: cmd, par = 15, p                         # sample offset -> O
    elif e == 0xA and p: cmd, par = 4, p                    # volume slide -> D
    elif e == 0xB: cmd, par = 2, p                          # jump
    elif e == 0xF and p: cmd, par = (1, p) if p < 32 else (20, p)
    elif e == 0xE:
        x, y = p >> 4, p & 15
        if x == 1 and y: cmd, par = 6, 0xF0 | y
        elif x == 2 and y: cmd, par = 5, 0xF0 | y
        elif x == 0xA and y: cmd, par = 4, (y << 4) | 0xF
        elif x == 0xB and y: cmd, par = 4, 0xF0 | y
        elif x == 0xC: cmd, par = 19, 0xC0 | y
        elif x == 0xD: cmd, par = 19, 0xD0 | y
        elif x == 9 and y: cmd, par = 17, y
    return note, inst, vol, cmd, par


def convert(xm_path, out_path):
    m = xmparse.parse(xm_path)
    nch = min(m['nch'], 8)
    seq = CALM_ORDERS + ACTION_ORDERS
    used = set()
    pats, starts = [], []
    last = [[0] for _ in range(nch)]
    for sec, orders in enumerate((CALM_ORDERS, ACTION_ORDERS)):
        starts.append(len(pats))
        for oi, o in enumerate(orders):
            ev = {}
            grid = m['pats'][m['orders'][o]]
            for r, row in enumerate(grid[:64]):
                for c in range(nch):
                    note, inst, vol, cmd, par = convert_cell(row[c], last[c])
                    if note is None and inst is None and vol is None and cmd is None: continue
                    if cmd == 2: cmd = None                      # section loops are ours
                    if note is not None and note != 254 and inst:
                        used.add(inst)
                    ev[(r, c)] = (note, inst if note not in (None, 254) else None, vol, cmd, par) if cmd is not None else (note, inst if note not in (None, 254) else None, vol)
            if oi == len(orders) - 1:                            # jump back to the section start
                ev[(len(grid[:64]) - 1, 7)] = (None, None, None, 2, starts[-1])
            pats.append(ev)
    inst_ids = sorted(used)
    idx = {k: n + 1 for n, k in enumerate(inst_ids)}
    samples = [sample_for(k, m['insts'][k - 1]) for k in inst_ids]
    pk = max(np.abs(s.pcm).max() for s in samples)
    pbytes = []
    for ev in pats:
        e2 = {}
        for key, val in ev.items():
            val = list(val)
            if val[1]: val[1] = idx[val[1]]
            e2[key] = tuple(val)
        pbytes.append(G.build_pattern(64, e2))
    first_speed = 3
    G.write_it(out_path, "universal network 2", samples, pbytes, orders=tuple(range(len(pats))) + (255,), rows=64, speed=m['tempo'], tempo=m['bpm'])
    print("xm chip: %d patterns (calm 0..%d, action %d..%d), %d samples, %d points, speed %d tempo %d"
          % (len(pats), starts[1] - 1, starts[1], len(pats) - 1, len(samples), sum(len(s.pcm) for s in samples), m['tempo'], m['bpm']))
    return starts


if __name__ == "__main__":
    convert(sys.argv[1], sys.argv[2])
