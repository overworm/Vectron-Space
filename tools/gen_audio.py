#!/usr/bin/env python3
"""gen_audio.py - builds snd/music.it (a track around the user's mp3 'dark space') and snd/sfx.it (shooter-style effects)
as Impulse Tracker modules; opensnes-music turns them into the SNESMOD soundbank at build time.

music: the user's sample (a 3 s loop cut from the mp3, 8 kHz), a saw bass, a square arpeggio and three drum samples
sfx:   24 synthesised effects, one instrument + one sample each (no song)
"""
import os, struct, subprocess, sys
import numpy as np
import scipy.io.wavfile as wavfile

HERE = os.path.dirname(os.path.abspath(__file__))
SND = os.path.join(HERE, "..", "snd")
MP3 = sys.argv[1] if len(sys.argv) > 1 else os.path.join(SND, "dark_space.mp3")
TEMPLATE_IT = os.path.join(HERE, "..", "..", "..", "audio", "snesmod_music", "sfx", "neo_lumen_sfx.it")
os.makedirs(SND, exist_ok=True)
rng = np.random.default_rng(7)


# ------------------------------------------------------------------ IT writer
class Sample:
    def __init__(self, name, data, rate, loop=None):
        self.name, self.rate, self.loop = name, rate, loop
        d = np.clip(np.asarray(data, dtype=float), -1, 1)
        self.pcm = (d * 30000).astype("<i2")


def instrument_template():
    d = open(TEMPLATE_IT, "rb").read()
    ordn, ins, smp, pat = struct.unpack("<HHHH", d[32:40])
    o = 192 + ordn
    off = struct.unpack("<%dI" % ins, d[o:o + 4 * ins])
    return bytearray(d[off[0]:off[0] + 554])


def write_it(path, title, samples, pattern_bytes=None, orders=(0, 255), rows=64, speed=6, tempo=125, nchan=8):
    ins_t = instrument_template()
    n = len(samples)
    npat = len(pattern_bytes)
    hdr = bytearray(192)
    hdr[0:4] = b"IMPM"
    hdr[4:4 + len(title)] = title.encode()
    struct.pack_into("<HHHHHHHH", hdr, 32, len(orders), n, n, npat, 0x0214, 0x0214, 0x000D, 0)
    hdr[48], hdr[49], hdr[50], hdr[51], hdr[52], hdr[53] = 128, 48, speed, tempo, 128, 0
    for c in range(64):
        hdr[64 + c] = 32
        hdr[128 + c] = 64
    pans = [32, 20, 44, 32, 26, 38, 32, 32]
    for c in range(8):
        hdr[64 + c] = pans[c]
    body = bytearray()
    ord_off = 192
    ins_off0 = ord_off + len(orders)
    smp_off0 = ins_off0 + 4 * n
    pat_off0 = smp_off0 + 4 * n
    data_off = pat_off0 + 4 * npat
    ins_blocks, smp_blocks, pat_blocks = [], [], []
    pos = data_off
    ins_offs, smp_offs, pat_offs = [], [], []
    for i, s in enumerate(samples):
        b = bytearray(ins_t)
        b[32:58] = s.name.encode()[:25].ljust(26, b"\0")
        b[4:16] = s.name.upper().encode()[:11].ljust(12, b"\0")
        for k in range(120):
            b[64 + 2 * k] = k
            b[64 + 2 * k + 1] = i + 1
        ins_offs.append(pos)
        pos += len(b)
        ins_blocks.append(bytes(b))
    smp_hdr_pos = pos
    for i, s in enumerate(samples):
        smp_offs.append(pos)
        pos += 80
    for i, s in enumerate(samples):
        pass
    sample_ptr = []
    for i, s in enumerate(samples):
        sample_ptr.append(pos)
        pos += len(s.pcm) * 2
    for pb in pattern_bytes:
        pat_offs.append(pos)
        pos += 8 + len(pb)
    out = bytearray(hdr)
    out += bytes(orders)
    out += struct.pack("<%dI" % n, *ins_offs)
    out += struct.pack("<%dI" % n, *smp_offs)
    out += struct.pack("<%dI" % npat, *pat_offs)
    for b in ins_blocks:
        out += b
    for i, s in enumerate(samples):
        h = bytearray(80)
        h[0:4] = b"IMPS"
        h[4:16] = s.name.upper().encode()[:11].ljust(12, b"\0")
        h[17] = 64
        flg = 1 | 2 | (16 if s.loop else 0)
        h[18] = flg
        h[19] = 64
        h[20:46] = s.name.encode()[:25].ljust(26, b"\0")
        h[46] = 1
        h[47] = 0
        lb, le = (s.loop if s.loop else (0, 0))
        struct.pack_into("<IIIIIII", h, 48, len(s.pcm), lb, le, s.rate, 0, 0, sample_ptr[i])
        out += h
    for s in samples:
        out += s.pcm.tobytes()
    for i, pb in enumerate(pattern_bytes):
        out += struct.pack("<HHI", len(pb), rows[i] if isinstance(rows, (list, tuple)) else rows, 0)
        out += pb
    open(path, "wb").write(bytes(out))


def empty_pattern(rows=64):
    return bytes(rows)          # one 0 byte per row = empty rows


def build_pattern(rows, events):
    """events: {(row, chan): (note, inst, vol[, cmd, param])}  note 0..119, 254 cut, None for none; cmd = IT effect number (A=1)"""
    out = bytearray()
    for r in range(rows):
        for c in range(8):
            e = events.get((r, c))
            if not e:
                continue
            note, inst, vol = e[0], e[1], e[2]
            cmd, par = (e[3], e[4]) if len(e) > 3 else (None, None)
            mask = 0
            vals = bytearray()
            if note is not None:
                mask |= 1
                vals.append(note)
            if inst is not None:
                mask |= 2
                vals.append(inst)
            if vol is not None:
                mask |= 4
                vals.append(vol)
            if cmd is not None:
                mask |= 8
                vals += bytes((cmd, par))
            out.append((c + 1) | 0x80)
            out.append(mask)
            out += vals
        out.append(0)
    return bytes(out)


# ------------------------------------------------------------------ synthesis helpers
def t_axis(dur, sr):
    return np.arange(int(dur * sr)) / sr


def env_ad(n, a=0.01, d=0.9, sr=1.0):
    e = np.ones(n)
    na = max(1, int(n * a))
    e[:na] = np.linspace(0, 1, na)
    e[na:] = np.linspace(1, 0, n - na) ** (1.0 + 2 * d)
    return e


def sweep(f0, f1, dur, sr, wave="sine", curve=1.0):
    t = t_axis(dur, sr)
    f = f0 + (f1 - f0) * (t / dur) ** curve
    ph = 2 * np.pi * np.cumsum(f) / sr
    if wave == "sine":
        return np.sin(ph)
    if wave == "square":
        return np.sign(np.sin(ph))
    if wave == "saw":
        return 2 * ((ph / (2 * np.pi)) % 1) - 1
    if wave == "tri":
        return 2 * np.abs(2 * ((ph / (2 * np.pi)) % 1) - 1) - 1
    raise ValueError(wave)


def noise(dur, sr):
    return rng.uniform(-1, 1, int(dur * sr))


def lowpass(x, k):
    k = max(1, int(k))
    return np.convolve(x, np.ones(k) / k, "same")


def highpass(x, k):
    return x - lowpass(x, k)


def fade_tail(x, ms=4, sr=8000):
    n = min(len(x), max(2, int(sr * ms / 1000)))
    x = x.copy()
    x[-n:] *= np.linspace(1, 0, n)
    return x


# ------------------------------------------------------------------ 8-bit style synthesis (NES-like channels, 60 Hz frame stepping)
SRC = 6000
SPF = SRC // 60


def chip_track(frames, sr=SRC):
    """frames: list of (freq, vol 0..15, wave) one per 1/60 s; waves p12 p25 p50 tri noise; pitch steps once per frame"""
    out = []
    ph = 0.0
    for (f, v, wave) in frames:
        n = sr // 60
        t = ph + np.arange(n) * f / sr
        ph = float((t[-1] + f / sr) % 1.0)
        t = t % 1.0
        if wave == "p12":
            x = np.where(t < 0.125, 1.0, -1.0)
        elif wave == "p25":
            x = np.where(t < 0.25, 1.0, -1.0)
        elif wave == "p50":
            x = np.where(t < 0.5, 1.0, -1.0)
        elif wave == "tri":
            x = np.round((2 * np.abs(2 * t - 1) - 1) * 7.5) / 7.5
        else:                                                       # noise: random bits held for 1/f seconds
            k = max(1, int(sr / max(f, 1)))
            bits = rng.integers(0, 2, n // k + 2) * 2.0 - 1.0
            x = np.repeat(bits, k)[:n]
        out.append(x * (round(v) / 15.0))
    return np.concatenate(out) if out else np.zeros(1)


def lin(a, b, n):
    return [a + (b - a) * i / max(1, n - 1) for i in range(n)]


def fr(freqs, vols, wave):
    if not isinstance(vols, list):
        vols = [vols] * len(freqs)
    return [(f, v, wave) for f, v in zip(freqs, vols)]


def gun(name, dur=0.2, body=0.04, thump=(150, 60, 0.9, 0.05), tail=0.15, tail_decay=0.1, zap=None, ring=None, sr=8000, hp=2):
    """a gunshot: 1 ms click, bright noise crack, low thump, noise tail; optional sci-fi zap / ring on top"""
    n = int(dur * sr)
    t = np.arange(n) / sr
    x = np.zeros(n)
    x[:6] = np.array([1, -1, 0.9, -0.8, 0.6, -0.4])
    crack = highpass(rng.uniform(-1, 1, n), hp) * np.exp(-t / body)
    x += crack * 1.2
    f0, f1, vol, dec = thump
    ph = 2 * np.pi * np.cumsum(f0 + (f1 - f0) * np.minimum(1, t / 0.12)) / sr
    x += np.sin(ph) * np.exp(-t / dec) * vol
    x += lowpass(rng.uniform(-1, 1, n), 3) * np.exp(-t / tail_decay) * tail
    if zap:
        a, b, v = zap
        x += np.sign(np.sin(2 * np.pi * np.cumsum(a + (b - a) * np.minimum(1, t / dur)) / sr)) * np.exp(-t / (dur * 0.5)) * v
    if ring:
        f, v, d = ring
        x += np.sin(2 * np.pi * f * t) * np.exp(-t / d) * v
    x = np.tanh(6.0 * x)                      # hard saturation: much more energy for the same peak (shots were ~5x quieter than other sfx)
    x = x / (np.abs(x).max() + 1e-9) * 0.98
    S_.append(Sample(name, fade_tail(x, 4, sr), sr))


S_ = []


def sfx_samples():
    S = S_
    S.clear()

    def add(name, *tracks):
        n = max(len(t) for t in tracks)
        mix = np.zeros(n)
        for t in tracks:
            mix[:len(t)] += t
        mix = mix / (np.abs(mix).max() + 1e-9) * 0.9
        S.append(Sample(name, fade_tail(mix, 3, SRC), SRC))

    ch = chip_track
    gun("FUSER", 0.16, 0.035, (170, 70, 0.55, 0.04), 0.14, 0.08)
    gun("QUANTON", 0.22, 0.055, (115, 45, 0.9, 0.06), 0.22, 0.1, zap=(1400, 250, 0.18))
    gun("PLASMA", 0.2, 0.04, (150, 60, 0.7, 0.05), 0.15, 0.08, zap=(900, 200, 0.4))
    gun("RAIL", 0.26, 0.03, (110, 50, 0.5, 0.05), 0.12, 0.08, ring=(2400, 0.28, 0.12), hp=1)
    gun("ROCKET", 0.28, 0.07, (95, 40, 1.0, 0.09), 0.5, 0.14)
    gun("BEAM", 0.12, 0.022, (200, 90, 0.35, 0.03), 0.08, 0.05)
    gun("KOZE", 0.3, 0.085, (85, 35, 1.0, 0.1), 0.45, 0.15)
    gun("ESHOT", 0.13, 0.03, (230, 120, 0.4, 0.035), 0.1, 0.05, zap=(700, 300, 0.3))
    add("EHIT", ch(fr([5000, 4000, 3000], [15, 10, 5], "noise")), ch(fr(lin(420, 180, 4), [12, 9, 6, 3], "p25")))
    add("EDIE", ch(fr(lin(5000, 700, 16), lin(15, 0, 16), "noise")), ch(fr(lin(520, 55, 16), lin(13, 1, 16), "p25")))
    add("HURT", ch(fr([700, 700, 600, 600, 500, 500, 400, 400, 300, 300, 230, 230], [14] * 10 + [8, 4], "p25")), ch(fr([4000, 3000], [12, 6], "noise")))
    add("PICK", ch(fr([988] * 4 + [1319] * 8, [12] * 4 + lin(12, 0, 8), "p50")))
    add("DOOR", ch(fr(lin(1200, 300, 10), lin(10, 0, 10), "noise")), ch(fr(lin(90, 150, 10), [8] * 10, "tri")))
    add("JUMP", ch(fr(lin(260, 640, 9), [12] * 7 + [8, 4], "p50")))
    add("MELEE", ch(fr(lin(2500, 800, 6), lin(15, 2, 6), "noise")), ch(fr(lin(150, 70, 6), [12] * 6, "tri")))
    add("BLIP", ch(fr([1000] * 3, [10, 7, 3], "p50")))
    add("RICO", ch(fr(lin(3600, 1500, 10), lin(14, 2, 10), "p25")), ch(fr([5000, 4200, 3000, 2000], [15, 9, 5, 2], "noise")))
    gun("CHAIN", 0.12, 0.06, (160, 65, 1.0, 0.07), 0.3, 0.08, zap=(900, 300, 0.25), ring=(520, 0.35, 0.06))      # fuller body: it fires in a burst and was much quieter than the other guns
    gun("STEP", 0.075, 0.016, (150, 55, 1.0, 0.035), 0.1, 0.03, hp=4)      # a boot on a metal floor: click + thud
    return S


# ------------------------------------------------------------------ music: breakbeat chops of the user's track + 8-bit melody
BPM = 137.0
GRID_OFF = 0.38           # first beat of the mp3 (found by onset analysis)


def chops(bar_start_s, n=16, rate=12000):
    """n 16th-note slices of the bar starting at bar_start_s of the user's mp3, cut on the beat grid (137 bpm)."""
    wav = os.path.join(SND, "src_%d.wav" % rate)
    subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-i", MP3, "-ac", "1", "-ar", str(rate), wav], check=True)
    sr, a = wavfile.read(wav)
    a = a.astype(float) / 32768
    step = 60.0 / BPM / 4
    out = []
    for k in range(n):
        st = int((bar_start_s + k * step) * sr)
        L = int(step * sr)
        seg = a[st:st + L].copy()
        seg *= np.minimum(1, np.minimum(np.arange(len(seg)), len(seg) - np.arange(len(seg))) / (sr * 0.002))
        seg = highpass(seg, 40)
        out.append(seg)
    pk = max(np.abs(x).max() for x in out) + 1e-9
    return [Sample("CHOP%d" % k, x / pk * 0.95, rate) for k, x in enumerate(out)]


def wave_cycle(kind, n=32):
    x = np.arange(n) / n
    if kind == "p12":
        return np.where(x < 0.125, 0.7, -0.7)
    if kind == "p25":
        return np.where(x < 0.25, 0.7, -0.7)
    if kind == "p50":
        return np.where(x < 0.5, 0.7, -0.7)
    if kind == "tri":
        return np.round((2 * np.abs(2 * x - 1) - 1) * 7.5) / 7.5 * 0.9
    raise ValueError(kind)


NOTE = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}


def nn(name):
    """'F2' 'Ab5' 'C#4' -> IT note number (C-5 = 60)"""
    n = NOTE[name[0]]
    i = 1
    while name[i] in "b#":
        n += 1 if name[i] == "#" else -1
        i += 1
    return 12 * int(name[i:]) + n


def music():
    """Breakbeat, 137 bpm (the tempo of the user's mp3): syncopated drums, sub bass, stabs, lead, and 16th-note chops of the mp3."""
    brk = chops(GRID_OFF + 4 * (60.0 / BPM) * 64)               # bar 64 (112.5 s) of the mp3
    insts = [Sample("BASS", wave_cycle("tri"), 16744, loop=(0, 32)),
             Sample("ARP", wave_cycle("p12"), 16744, loop=(0, 32)),
             Sample("LEAD", wave_cycle("p25"), 16744, loop=(0, 32)),
             Sample("PAD", wave_cycle("p50"), 16744, loop=(0, 32))]
    sr = 8000
    n = int(0.17 * sr)
    kick = Sample("KICK", sweep(200, 45, 0.17, sr, "sine", 0.5) * env_ad(n, 0.01, 0.55), sr)
    n = int(0.16 * sr)
    snare = Sample("SNARE", (lowpass(noise(0.16, sr), 2) * 0.95 + sweep(260, 170, 0.16, sr, "sine") * 0.35) * env_ad(n, 0.01, 0.6), sr)
    n = int(0.05 * sr)
    hat = Sample("HAT", highpass(noise(0.05, sr), 3) * env_ad(n, 0.02, 0.8), sr)
    n = int(0.12 * sr)
    ohat = Sample("OHAT", highpass(noise(0.12, sr), 3) * env_ad(n, 0.02, 0.9), sr)
    samples = brk + insts + [kick, snare, hat, ohat]
    BRK0 = 1
    BASS, ARP, LEAD, PAD, KICK, SNARE, HAT, OHAT = 17, 18, 19, 20, 21, 22, 23, 24
    rows = 64
    prog = ("A", "A", "F", "G")                           # one chord per bar, A minor / F / G
    roots = {"A": "A", "F": "F", "G": "G", "E": "E"}

    def drums(ev, bar, kind, fill=False):
        r0 = bar * 16
        kicks = {0: (0, 3, 10), 1: (0, 3, 6, 10, 13), 2: (0, 10), 3: (0, 2, 7, 10, 14)}[kind]
        for st in kicks: ev[(r0 + st, 4)] = (60, KICK, 58 if st == 0 else 46)
        for st in (4, 12): ev[(r0 + st, 5)] = (60, SNARE, 50)
        if kind in (1, 3):
            for st, v in ((7, 18), (9, 14), (15, 22)): ev[(r0 + st, 5)] = (60, SNARE, v)      # ghost notes
        for st in range(0, 16, 2): ev[(r0 + st, 6)] = (60, HAT, 24 if st % 4 == 0 else 14)
        if kind >= 1: ev[(r0 + 14, 6)] = (60, OHAT, 22)
        if fill:
            for k, st in enumerate((10, 11, 12, 13, 14, 15)): ev[(r0 + st, 5)] = (60, SNARE, 26 + k * 7)

    def bass(ev, bar, root, mode):
        r0 = bar * 16
        pats = {0: ((0, 0, 3), (3, 0, 3), (6, 1, 2), (10, 0, 3), (12, 0, 2), (14, 1, 2)),
                1: ((0, 0, 6), (6, 0, 2), (8, 1, 2), (11, 0, 3), (14, 0, 2)),
                2: ((0, 0, 2), (2, 0, 1), (4, 1, 2), (7, 0, 2), (10, 0, 3), (13, 1, 2), (15, 0, 1))}[mode]
        for off, up, ln in pats:
            ev[(r0 + off, 1)] = (nn("%s%d" % (root, 2 + up)), BASS, 50)
            ev[(r0 + off + ln, 1)] = (254, None, None)

    def stabs(ev, bar, root, mode):
        r0 = bar * 16
        tri = {"A": ("A4", "C5", "E5"), "F": ("F4", "A4", "C5"), "G": ("G4", "B4", "D5"), "E": ("E4", "G4", "B4")}[root]
        spots = ((0, 0), (6, 0), (10, 0), (14, 0)) if mode == 0 else ((0, 0), (3, 0), (8, 0), (11, 0))
        for st, _ in spots:
            for k in range(3):
                ev[(r0 + st + k, 2)] = (nn(tri[k]), ARP, 22)
            ev[(r0 + st + 3, 2)] = (254, None, None)

    def chop_rows(ev, seq, vol=44):
        for k, sl in enumerate(seq):
            if sl is not None: ev[(k, 0)] = (60, BRK0 + sl, vol)

    ident = list(range(16)) * 4
    seq_a = ident
    seq_b = ([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 12, 13] + [0, 1, 2, 3, 8, 9, 10, 11, 4, 5, 6, 7, 12, 13, 14, 15]) * 2
    seq_c = ([0, 0, 2, 3, 4, 4, 6, 7, 8, 8, 10, 11, 12, 13, 13, 13] + [15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0]) * 2
    seq_stut = ([0, 1, 2, 3] * 2 + [4, 5, 4, 5, 8, 9, 8, 9]) * 2 + [None] * 0
    seq_stut = (seq_stut + seq_stut)[:64]

    def lead(ev, notes):
        for (row, note, ln) in notes:
            ev[(row, 3)] = (nn(note), LEAD, 38)
            ev[(row + ln, 3)] = (254, None, None)

    L1 = [(0, "E5", 3), (6, "A5", 2), (8, "C6", 4), (14, "B5", 2),
          (16, "A5", 3), (22, "C6", 2), (24, "A5", 4), (30, "F5", 2),
          (32, "C6", 3), (38, "A5", 2), (40, "F5", 4), (46, "G5", 2),
          (48, "D6", 3), (54, "B5", 2), (56, "G5", 4), (60, "B5", 3)]
    L2 = [(0, "A5", 2), (2, "C6", 2), (4, "E6", 4), (10, "D6", 2), (12, "C6", 4),
          (16, "A5", 2), (18, "C6", 2), (20, "E6", 4), (26, "G6", 2), (28, "E6", 4),
          (32, "A5", 2), (34, "C6", 2), (36, "F6", 4), (42, "E6", 2), (44, "C6", 4),
          (48, "B5", 2), (50, "D6", 2), (52, "G6", 4), (58, "F6", 2), (60, "D6", 4)]

    def pat(seq, kinds, bass_mode=0, stab=None, ld=None, fills=(), with_bass=True):
        ev = {}
        chop_rows(ev, seq)
        for bar in range(4):
            drums(ev, bar, kinds[bar], fill=(bar in fills))
            if with_bass: bass(ev, bar, roots[prog[bar]], bass_mode)
            if stab is not None: stabs(ev, bar, roots[prog[bar]], stab)
        if ld: lead(ev, ld)
        return build_pattern(rows, ev)

    P = []
    P.append(pat(seq_a, (0, 0, 0, 2), bass_mode=0, with_bass=False))                          # 0 intro: chops + drums
    P.append(pat(seq_a, (1, 1, 1, 1), 0, stab=0, fills=(3,)))                                  # 1 groove
    P.append(pat(seq_b, (1, 3, 1, 3), 1, stab=1, ld=L1, fills=(3,)))                           # 2 groove + lead
    P.append(pat(seq_stut, (2, 2, 2, 3), 2, fills=(3,)))                                       # 3 breakdown / stutter
    P.append(pat(seq_c, (3, 3, 3, 3), 1, stab=0, ld=L2, fills=(3,)))                           # 4 drop
    P.append(pat(seq_b, (1, 1, 3, 3), 2, stab=1, ld=L2, fills=(1, 3)))                         # 5 final
    orders = (0, 1, 2, 3, 4, 1, 2, 3, 4, 5, 255)
    write_it(os.path.join(SND, "music_break.it"), "Vectron breakbeat", samples, P, orders=orders, rows=rows, speed=6, tempo=137)
    return 0.0


def main():
    st = music()                                   # breakbeat -> snd/music_break.it (kept as an alternative)
    # music.it / action.it / boss.it come from tools/mod2it.py (State.xm, Darker.xm, WIRED96R.S3M)
    S = sfx_samples()
    write_it(os.path.join(SND, "sfx.it"), "Vectron effects", S, [empty_pattern(64)], orders=(0, 255), rows=64, speed=6, tempo=125)
    print("music: chops from %.1f s of the mp3; %d effects" % (st, len(S)))


if __name__ == "__main__":
    main()
