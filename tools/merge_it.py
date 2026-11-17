#!/usr/bin/env python3
"""merge_it.py a.it b.it out.it budget_pts - two modules made by gen_audio.write_it become ONE module:
order 0 = tempo setup of A, A's orders, then a tempo setup of B and B's orders. Each song loops inside its own part,
the game switches songs with snesmodPlay(position) (no module upload, no lag). Prints the two start positions."""
import struct, sys, os
from fractions import Fraction
import numpy as np
from scipy.signal import resample_poly
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_audio as G


def parse(path):
    d = open(path, 'rb').read()
    ordn, ins, smp, pat = struct.unpack('<HHHH', d[32:40])
    speed, tempo = d[50], d[51]
    o = 192
    orders = list(d[o:o + ordn]); o += ordn
    ioff = struct.unpack('<%dI' % ins, d[o:o + 4 * ins]); o += 4 * ins
    soff = struct.unpack('<%dI' % smp, d[o:o + 4 * smp]); o += 4 * smp
    poff = struct.unpack('<%dI' % pat, d[o:o + 4 * pat])
    samples = []
    for so in soff:
        h = d[so:so + 80]
        flg = h[18]
        ln, lb, le, rate, _, _, ptr = struct.unpack('<IIIIIII', h[48:76])
        pcm = np.frombuffer(d[ptr:ptr + ln * 2], '<i2').astype(float) / 30000.0
        samples.append(dict(name=h[20:46].split(b'\0')[0].decode(), data=pcm, rate=rate, loop=(lb, le) if flg & 16 else None))
    pats = []
    for po in poff:
        ln, rows = struct.unpack('<HH', d[po:po + 4])
        p = d[po + 8:po + 8 + ln]
        ev = {}; r = 0; i = 0
        while r < rows and i < len(p):
            b = p[i]; i += 1
            if b == 0: r += 1; continue
            c = (b - 1) & 63
            if b & 0x80: mask = p[i]; i += 1
            else: raise SystemExit('unsupported pattern coding')
            note = inst = vol = cmd = par = None
            if mask & 1: note = p[i]; i += 1
            if mask & 2: inst = p[i]; i += 1
            if mask & 4: vol = p[i]; i += 1
            if mask & 8: cmd, par = p[i], p[i + 1]; i += 2
            e = (note, inst, vol) + ((cmd, par) if cmd is not None else ())
            ev[(r, c)] = e
        pats.append((ev, rows))
    return dict(orders=[x for x in orders if x < 254], samples=samples, pats=pats, speed=speed, tempo=tempo)


def main(a, b, out, budget):
    A, B = parse(a), parse(b)
    nA = len(A['samples'])
    tot = sum(len(s['data']) for s in A['samples'] + B['samples'])
    f = min(1.0, budget / tot)
    smp = []
    for s in A['samples'] + B['samples']:
        x, rate, loop = s['data'], s['rate'], s['loop']
        if f < 1.0:
            fr = Fraction(f).limit_denominator(40)
            x = resample_poly(x, fr.numerator, fr.denominator)
            k = len(x) / len(s['data'])
            rate = max(1000, int(rate * k))
            if loop:
                lb = int(loop[0] * k) // 16 * 16
                le = min(len(x), lb + max(16, (int((loop[1] - loop[0]) * k) // 16) * 16))
                loop = (lb, le)
        smp.append(G.Sample(s['name'], x, rate, loop))
    pats, rows, orders = [], [], []

    def setup(sp, tp):
        pats.append(G.build_pattern(32, {(0, 0): (None, None, None, 1, sp), (0, 1): (None, None, None, 20, tp), (0, 2): (None, None, None, 3, 0)})); rows.append(32)
        return len(pats) - 1

    def add(song, ioff, start_order):
        for oi in song['orders']:
            ev, nr = song['pats'][oi]
            e2 = {}
            for k, e in ev.items():
                e = list(e)
                if e[1]: e[1] += ioff
                if len(e) > 3 and e[3] == 2: e[4] = start_order       # B: jump back to the start of THIS song
                e2[k] = tuple(e)
            pats.append(G.build_pattern(nr, e2)); rows.append(nr)
            orders.append(len(pats) - 1)
    posA = len(orders); orders.append(setup(A['speed'], A['tempo']))
    add(A, 0, posA)
    posB = len(orders); orders.append(setup(B['speed'], B['tempo']))
    add(B, nA, posB)
    G.write_it(out, 'merged', smp, pats, orders=tuple(orders) + (255,), rows=rows, speed=A['speed'], tempo=A['tempo'])
    print('merged: %d samples, %d pts (scale %.2f), %d patterns, pattern bytes %d; calm at order %d, battle at order %d' % (
        len(smp), sum(len(s.pcm) for s in smp), f, len(pats), sum(map(len, pats)), posA, posB))
    open(os.path.join(os.path.dirname(out), 'music_pos.h'), 'w').write('#define MUS_POS_CALM %d\n#define MUS_POS_BATTLE %d\n' % (posA, posB))


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4]))
