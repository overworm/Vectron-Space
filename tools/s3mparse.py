"""s3mparse.py - minimal ScreamTracker 3 .s3m reader (PCM samples, patterns); notes are returned XM-style (C-4 = 49)"""
import struct
import numpy as np


def parse(path):
    d = open(path, 'rb').read()
    ordnum, insnum, patnum, flags, cwt, ffv = struct.unpack('<6H', d[0x20:0x2C])
    assert d[0x2C:0x30] == b'SCRM'
    speed, tempo = d[0x31], d[0x32]
    chset = d[0x40:0x60]
    orders = [o for o in d[0x60:0x60 + ordnum] if o < 254]
    p = 0x60 + ordnum
    inspp = struct.unpack('<%dH' % insnum, d[p:p + 2 * insnum]); p += 2 * insnum
    patpp = struct.unpack('<%dH' % patnum, d[p:p + 2 * patnum])
    insts = []
    for pp in inspp:
        o = pp * 16
        if o == 0 or d[o] != 1:
            insts.append(None); continue
        hi = d[o + 13]; lo = struct.unpack('<H', d[o + 14:o + 16])[0]
        seg = ((hi << 16) | lo) * 16
        ln, lb, le = struct.unpack('<III', d[o + 16:o + 28])
        vol, _, pack, fl = d[o + 28], d[o + 29], d[o + 30], d[o + 31]
        c2 = struct.unpack('<I', d[o + 32:o + 36])[0]
        name = d[o + 48:o + 76].rstrip(b'\0').decode('latin1')
        bits16 = bool(fl & 4); stereo = bool(fl & 2)
        raw = d[seg:seg + ln * (2 if bits16 else 1) * (2 if stereo else 1)]
        if bits16:
            x = np.frombuffer(raw[:ln * 2], dtype='<i2').astype(float) / 32768
            if ffv == 2: x = (np.frombuffer(raw[:ln * 2], dtype='<u2').astype(float) - 32768) / 32768
        else:
            if ffv == 2: x = (np.frombuffer(raw[:ln], dtype=np.uint8).astype(float) - 128) / 128
            else: x = np.frombuffer(raw[:ln], dtype=np.int8).astype(float) / 128
        insts.append(dict(name=name, data=x, fs0=c2 or 8363, ls=lb, ll=max(0, le - lb), loop=bool(fl & 1), vol=vol))
    pats = []
    for pp in patpp:
        o = pp * 16
        if o == 0:
            pats.append([[None] * 32 for _ in range(64)]); continue
        ln, = struct.unpack('<H', d[o:o + 2]); q = o + 2
        grid = [[None] * 32 for _ in range(64)]
        r = 0
        while r < 64 and q < o + ln + 2:
            b = d[q]; q += 1
            if b == 0:
                r += 1; continue
            c = b & 31
            note = inst = vol = None; eff = par = 0
            if b & 32:
                nb, inst = d[q], d[q + 1]; q += 2
                if nb == 254: note = 97
                elif nb != 255: note = (nb >> 4) * 12 + (nb & 15) + 1
            if b & 64:
                vol = d[q]; q += 1
            if b & 128:
                eff, par = d[q], d[q + 1]; q += 2
            grid[r][c] = (note or 0, inst or 0, vol, eff, par)
        pats.append(grid)
    nch = 1 + max([c for g in pats for r in g for c in range(32) if r[c] is not None] or [0])
    return dict(speed=speed, tempo=tempo, orders=orders, pats=pats, insts=insts, nch=nch)
