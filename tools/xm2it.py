#!/usr/bin/env python3
"""xm2it.py - squeeze a 24-channel FastTracker .xm into an 8-voice Impulse Tracker module for the SNES (SPC700).

 * notes are collected from all channels and re-assigned to 8 voices: rhythmic instruments first, pads fill the gaps,
   long notes are capped, what does not fit is dropped
 * samples are low-pass filtered and resampled to 4-8 kHz, long loops are shortened and cross-faded
 * the XM note C-4 (49) sounds at the sample's own pitch = IT note C-5 (60)
"""
import os, sys, collections
import numpy as np
from scipy.signal import resample_poly
from math import gcd
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import xmparse
import gen_audio as G

ROWSEC = 24 * 2.5 / 250
# instrument -> (target rate, kind, max rows, priority, sample group)
PLAN = {8: dict(rate=8000, prio=4, maxrows=5, keep=1.2),                         # "and fly"  rhythmic one-shot
        1: dict(rate=8000, prio=3, maxrows=14, att=0.2, body=0.7),                # "kb"       main pad / lead, looped
        9: dict(rate=4000, prio=2, maxrows=10, keep=2.5),                         # long swell, one-shot
        2: dict(rate=8000, prio=1, maxrows=8, att=0.2, body=0.8),                 # "obsessed maniacs" pad, looped
        3: dict(alias=2), 5: dict(alias=2)}


def resample(x, fs_from, fs_to):
    g = gcd(int(round(fs_from)), int(fs_to))
    return resample_poly(x, int(fs_to) // g, int(round(fs_from)) // g)


def xfade_loop(body, n):
    """make a seamless loop of `body` by cross-fading its last n samples into its first n (returns shorter array)"""
    t = np.linspace(0, np.pi / 2, n)
    head, tail = body[:n], body[-n:]
    mid = body[n:-n]
    return np.concatenate([head * np.sin(t) + tail * np.cos(t), mid])


def build_samples(m):
    order = [8, 1, 9, 2]
    out, idx = [], {}
    for k, inst_no in enumerate(order):
        plan = PLAN[inst_no]
        s = m['insts'][inst_no - 1]['samples'][0]
        fs0 = 8363 * 2 ** ((s['rel'] + s['fin'] / 128) / 12)
        R = plan['rate']
        x = s['data']
        if s['typ'] and 'body' in plan:
            ls, ll = s['ls'], s['ll']
            att = x[:int(plan['att'] * fs0)]
            body = x[ls:ls + min(ll, int(plan['body'] * fs0))]
            xr_body = resample(body, fs0, R)
            xr_att = resample(att, fs0, R)
            n = len(xr_body) // 4
            loop = xfade_loop(xr_body, n)
            # join attack -> loop start with a short cross-fade
            j = min(len(xr_att), len(loop)) // 4
            t = np.linspace(0, np.pi / 2, j)
            seg = np.concatenate([xr_att[:-j], xr_att[-j:] * np.cos(t) + loop[:j] * np.sin(t), loop[j:]])
            start = len(xr_att)
            loop_len = (len(seg) - start) // 16 * 16
            seg = seg[:start + loop_len]
            smp = seg * (s['vol'] / 64)
            out.append(dict(name=("I%d" % inst_no), data=smp, rate=R, loop=(start, start + loop_len)))
        else:
            seglen = int(plan.get('keep', 2.0) * fs0)
            xr = resample(x[:seglen], fs0, R)
            fade = int(0.15 * R)
            xr[-fade:] *= np.linspace(1, 0, fade)
            out.append(dict(name=("I%d" % inst_no), data=xr * (s['vol'] / 64), rate=R, loop=None))
        idx[inst_no] = k + 1
    for a in (3, 5):
        idx[a] = idx[PLAN[a]['alias']]
    pk = max(np.abs(o['data']).max() for o in out) + 1e-9
    for o in out:
        o['data'] = o['data'] / pk * 0.95
    return out, idx


def collect(m):
    rows = []
    for o in m['orders']:
        rows.extend(m['pats'][o])
    nch = m['nch']
    last = [0] * nch
    notes = []
    by = collections.defaultdict(list)
    for t, row in enumerate(rows):
        for c, (n, i, v, e, p) in enumerate(row):
            if 1 <= n <= 96:
                if i: last[c] = i
                vol = 64
                if 0x10 <= v <= 0x50: vol = v - 0x10
                elif e == 0xC: vol = min(64, p)
                by[c].append((t, n, last[c], vol))
            elif n == 97:
                by[c].append((t, 97, 0, 0))
    for c, l in by.items():
        for k, (t, n, i, vol) in enumerate(l):
            if n == 97 or i == 0: continue
            end = l[k + 1][0] if k + 1 < len(l) else len(rows)
            notes.append(dict(t=t, end=end, c=c, n=n, inst=i, vol=vol))
    return rows, notes


def assign(rows, notes, nvoices=8):
    for nt in notes:
        p = PLAN[nt['inst']]
        pl = PLAN[p['alias']] if 'alias' in p else p
        nt['prio'] = pl['prio']
        nt['end'] = min(nt['end'], nt['t'] + pl['maxrows'])
    notes.sort(key=lambda x: (-x['prio'], x['t']))
    busy = [[] for _ in range(nvoices)]          # intervals per voice
    placed = []
    dropped = 0
    for nt in notes:
        t0, t1 = nt['t'], nt['end']
        ok = False
        for dur in (t1 - t0, max(2, (t1 - t0) // 2), 2):
            if dur < 1: continue
            for v in range(nvoices):
                if all(t0 + dur <= a or t0 >= b for a, b in busy[v]):
                    busy[v].append((t0, t0 + dur)); nt['voice'] = v; nt['end'] = t0 + dur
                    placed.append(nt); ok = True; break
            if ok: break
        if not ok: dropped += 1
    return placed, dropped


def convert(xm_path, out_path):
    m = xmparse.parse(xm_path)
    rows, notes = collect(m)
    smps, idx = build_samples(m)
    placed, dropped = assign(rows, notes)
    nrows = len(rows)
    npat = (nrows + 63) // 64
    evs = [dict() for _ in range(npat)]
    byvoice = collections.defaultdict(list)
    for nt in placed: byvoice[nt['voice']].append(nt)
    for v, l in byvoice.items():
        l.sort(key=lambda x: x['t'])
        for k, nt in enumerate(l):
            s = m['insts'][nt['inst'] - 1]['samples'][0]
            sv = s['vol'] / 64
            vol = max(1, min(64, int(nt['vol'] * 0.8)))
            note = nt['n'] + 11
            if not (0 <= note <= 119): continue
            evs[nt['t'] // 64][(nt['t'] % 64, v)] = (note, idx[nt['inst']], vol)
            nxt = l[k + 1]['t'] if k + 1 < len(l) else nrows
            if nt['end'] < nxt and nt['end'] < nrows and PLAN[nt['inst']].get('maxrows') and (m['insts'][nt['inst'] - 1]['samples'][0]['typ']):
                e = nt['end']
                if (e % 64, v) not in evs[e // 64]:
                    evs[e // 64][(e % 64, v)] = (254, None, None)
    pats = [G.build_pattern(64, e) for e in evs]
    samples = [G.Sample(o['name'], o['data'], o['rate'], loop=o['loop']) for o in smps]
    G.write_it(out_path, "the wing (xm)", samples, pats, orders=tuple(range(npat)) + (255,), rows=64, speed=24, tempo=250)
    print("xm: %d notes, %d placed, %d dropped, %d patterns, %d samples (%d points)" % (len(notes), len(placed), dropped, npat, len(samples), sum(len(o['data']) for o in smps)))


if __name__ == "__main__":
    convert(sys.argv[1], sys.argv[2])
