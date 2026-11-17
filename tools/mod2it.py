#!/usr/bin/env python3
"""mod2it.py - generic .xm / .s3m -> 8-voice Impulse Tracker module for SNESMOD with automatic sample reduction.
usage: mod2it.py in out.it orders(e.g. 2-5,7,8) point_budget title max_instruments role(all|perc|melodic)
"""
import os, sys
from fractions import Fraction
import numpy as np
from scipy.signal import resample_poly
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import xmparse, s3mparse
import gen_audio as G
from xm2it_chip import convert_cell

KEEP_FX = {1, 3, 4, 5, 6, 7, 10, 15, 20}


def load(path):
    if path.lower().endswith('.s3m'):
        m = s3mparse.parse(path)
        insts = []
        for i in m['insts']:
            if not i: insts.append(None); continue
            insts.append(dict(data=i['data'], fs0=i['fs0'], ls=i['ls'], ll=i['ll'], loop=i['loop'], vol=1.0))
        return dict(kind='s3m', orders=m['orders'], pats=m['pats'], insts=insts, nch=m['nch'], speed=m['speed'], tempo=m['tempo'])
    m = xmparse.parse(path)
    insts = []
    for i in m['insts']:
        if not i['samples']: insts.append(None); continue
        s = i['samples'][0]
        fs0 = 8363 * 2 ** ((s['rel'] + s['fin'] / 128) / 12)
        insts.append(dict(data=s['data'], fs0=fs0, ls=s['ls'], ll=s['ll'], loop=bool(s['typ']), vol=s['vol'] / 64))
    return dict(kind='xm', orders=m['orders'], pats=m['pats'], insts=insts, nch=m['nch'], speed=m['tempo'], tempo=m['bpm'])


def s3m_cell(cell, last):
    note, inst, vol, eff, par = cell
    n = i = v = c = p = None
    if 1 <= note <= 96:
        n = note + 11
        if inst: last[0] = inst
        i = last[0]
        if vol is not None: v = min(64, vol)
    elif note == 97:
        n = 254
    if eff in KEEP_FX and eff != 20 or eff == 20 and par >= 32:
        c, p = eff, par
    if eff == 1 and par == 0: c = p = None
    return n, i, v, c, p


def reduce_sample(d, rate_cap, len_cap):
    # len_cap: seconds of the ORIGINAL sample that are really played (or None = all)
    x = d['data'].astype(float).copy()
    fs0 = d['fs0']
    f = Fraction(min(fs0, rate_cap) / fs0).limit_denominator(48)
    if f < 1:
        x = resample_poly(x, f.numerator, f.denominator)
    rate = fs0 * float(f)
    ls, ll = int(d['ls'] * float(f)), int(d['ll'] * float(f))
    cap = int(len_cap * rate) if len_cap else len(x)
    loop = None
    if d['loop'] and ll > 32:
        att = int(0.8 * rate)
        if ls > att:
            x = x[ls - att:]; ls = att
        ll = min(ll, int(0.45 * rate) // 16 * 16)
        end = min(ls + ll, len(x), cap if ls + 32 < cap else ls + ll)
        if end - ls >= 32 and ls < cap:
            ll16 = (end - ls) // 16 * 16
            ls16 = ls
            seg = x[:ls16 + ll16].copy()
            n = min(ll16 // 6, 300)
            if n > 8 and ls16 >= n:
                t = np.linspace(0, np.pi / 2, n)
                seg[-n:] = seg[-n:] * np.cos(t) + seg[ls16 - n:ls16] * np.sin(t)
            x, loop = seg, (ls16, ls16 + ll16)
    if loop is None:
        x = x[:cap]
        n = min(len(x) // 12, 200)
        if n > 4:
            x[-n:] *= np.linspace(1, 0, n)
    x = x * d['vol']
    if len(x) < 32: x = np.concatenate([x, np.zeros(32 - len(x))])
    return x, int(rate), loop


def convert(path, out, orders_sel, budget, title="song", maxch=8, maxinst=16, role="all"):
    m = load(path)
    seq = [m['orders'][k] for k in orders_sel]
    # drop orders with markers
    nch = m['nch']
    count = [0] * nch
    for o in seq:
        for row in m['pats'][o]:
            for c in range(nch):
                cell = row[c]
                if cell and 1 <= cell[0] <= 96: count[c] += 1
    # channel roles: rhythmic ("perc": a note about every 0.4 s or faster) / melodic (slower notes, pads)
    rowt0 = m['speed'] * 2.5 / m['tempo']
    times = [[] for _ in range(nch)]; pit = [[] for _ in range(nch)]
    t = 0.0
    for o in seq:
        for row in m['pats'][o]:
            t += rowt0
            for c in range(nch):
                x = row[c]
                if x and 1 <= x[0] <= 97:
                    times[c].append(t)
                    if x[0] <= 96: pit[c].append(x[0])
    med = [np.median(np.diff(times[c])) if len(times[c]) > 3 else 99 for c in range(nch)]
    isperc = [med[c] <= 0.45 for c in range(nch)]
    ranked = sorted([c for c in range(nch) if count[c] > 4], key=lambda c: -count[c])
    if role == 'perc':
        pc = [c for c in ranked if isperc[c]][:maxch - 1]
        bs = sorted([c for c in ranked if not isperc[c] and count[c] > 20], key=lambda c: np.mean(pit[c]))[:1]
        chans = sorted(pc + bs)
    elif role == 'melodic':
        chans = sorted([c for c in ranked if not isperc[c]][:maxch])
    else:
        chans = sorted(ranked[:maxch])
    print('channels', chans, 'perc', [c for c in chans if isperc[c]])
    cmap = {c: k for k, c in enumerate(chans)}
    # patterns (one IT pattern per order entry; identical patterns reused)
    lastinst = {c: [0] for c in chans}
    pats, rows_l, used = [], [], set()
    tnow = 0.0; rowt = m['speed'] * 2.5 / m['tempo']
    run = {}; need = {}

    def endrun(c):
        if c in run:
            t0, i, n = run.pop(c)
            d = m['insts'][i - 1]
            if d is not None:
                v = (tnow - t0) * d['fs0'] * 2 ** ((n - 49) / 12)
                need[i] = max(need.get(i, 0), v / d['fs0'])
    cache = {}
    usecnt = {}
    for oi, o in enumerate(seq):
        grid = m['pats'][o]
        nrows = len(grid)
        ev = {}
        for r in range(nrows):
            brk = False
            tnow += rowt
            for c in chans:
                cell = grid[r][c]
                if not cell: continue
                if cell[0] and (1 <= cell[0] <= 96 or cell[0] == 97):
                    endrun(c)
                    if cell[0] <= 96: run[c] = (tnow, cell[1] or lastinst[c][0], cell[0])
                if m['kind'] == 'xm': note, inst, vol, cmd, par = convert_cell(cell, lastinst[c])
                else: note, inst, vol, cmd, par = s3m_cell(cell, lastinst[c])
                if cmd == 2: cmd = None
                if note is None and inst is None and vol is None and cmd is None: continue
                if note in (None, 254): inst = None
                else: usecnt[inst] = usecnt.get(inst, 0) + 1
                e = (note, inst, vol) + ((cmd, par) if cmd is not None else ())
                ev[(r, cmap[c])] = e
        pats.append(ev); rows_l.append(nrows)
    for c in list(run): endrun(c)
    keep = set(sorted(usecnt, key=lambda k: -usecnt[k])[:maxinst])
    used = keep
    for ev in pats:
        for key in list(ev):
            e = ev[key]
            if e[0] not in (None, 254) and e[1] not in keep:
                del ev[key]
    # section loop: jump back to order 0 at last row
    last = pats[-1]; lr = rows_l[-1] - 1
    for k in range(len(chans)):
        e = last.get((lr, k), (None, None, None))
        if len(e) <= 3:
            last[(lr, k)] = tuple(e[:3]) + (2, 0); break
    # sample reduction to the budget
    ids = sorted(i for i in used if m['insts'][i - 1])
    best = None
    for lscale in (1.0, 0.7, 0.5, 0.35):
        for rc in (22050, 18000, 16000, 14000, 12000, 11025, 10000, 9000, 8000, 7000, 6000, 5000, 4000):
            red = {}
            for i in ids:
                d = m['insts'][i - 1]
                lc = None if d['loop'] else max(0.06, (need.get(i, 0.3) * 1.15 + 0.03) * lscale)
                red[i] = reduce_sample(d, rc, lc)
            tot = sum(len(v[0]) for v in red.values())
            if tot <= budget: best = (rc, lscale, red, tot); break
        if best: break
    if not best:
        raise SystemExit("cannot fit")
    rc, lc, red, tot = best
    idx = {k: n + 1 for n, k in enumerate(ids)}
    samples = [G.Sample("I%d" % k, red[k][0], red[k][1], loop=red[k][2]) for k in ids]
    pb = []
    for ev in pats:
        e2 = {}
        for key, val in ev.items():
            val = list(val)
            if val[1]: val[1] = idx[val[1]] if val[1] in idx else None
            if val[1] is None and val[0] not in (None, 254): continue
            e2[key] = tuple(val)
        pb.append(G.build_pattern(max(r for r in rows_l), e2) if False else build(e2, rows_l[len(pb)]))
    G.write_it(out, title, samples, pb, orders=tuple(range(len(pats))) + (255,), rows=rows_l, speed=m['speed'], tempo=m['tempo'])
    print("%s: %d orders, ch %s, %d samples, %d pts (rate cap %d), pattern bytes %d, speed %d tempo %d" % (
        os.path.basename(path), len(pats), chans, len(samples), tot, rc, sum(map(len, pb)), m['speed'], m['tempo']))


def build(ev, rows):
    return G.build_pattern(rows, ev)


if __name__ == "__main__":
    a = sys.argv
    sel = []
    for part in a[3].split(','):
        if '-' in part: x, y = part.split('-'); sel += list(range(int(x), int(y) + 1))
        else: sel.append(int(part))
    convert(a[1], a[2], sel, int(a[4]), a[5], maxinst=int(a[6]), role=a[7], maxch=int(a[8]) if len(a) > 8 else 8)
