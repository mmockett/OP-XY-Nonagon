#!/usr/bin/env python3
"""Renders Recur's sample, samples/break.wav: a four-bar funk break played on MD KIT.

The sound is MD KIT's own engine (OP-XY-MNMD, machines/mdkit/engine.cpp; the
shipped file came from OP-XY-MNMD b43e907), driven by hits.cpp. The groove is
the well-known four-bar break written out by hand as a drum part (PATTERN below).

    python tools/break/make_break.py --mnmd ../OP-XY-MNMD --sdk ../open-xy \
        [--bpm 120] [--out samples/break.wav] [--grid pattern.txt]

Needs numpy and a C++ compiler (--cxx, default clang++).

The loop is exactly bars * 4 * 60/bpm seconds at 44.1 kHz; every tail wraps
round the loop point, and the bus runs over three tiled passes of which the
middle one is kept, so the loop is seamless. 16-bit mono WAV, -4 dBFS peak.
"""
import argparse, os, subprocess, tempfile, wave
import numpy as np

SR = 44100
HERE = os.path.dirname(os.path.abspath(__file__))
HITS = None   # the compiled hits.cpp

# MD KIT machines (kit.h model numbers) and per-instrument controls, 0..127 in each
# machine's own slot order. Layers sound together on each hit.
TRX_BD, TRX_SD, TRX_CH, TRX_OH, TRX_CY, PI_BD, PI_SD, PI_RC, PI_CC = 0, 1, 6, 7, 8, 21, 22, 27, 28
KIT = {
    # kick: P-I BD (PTCH DEC HARD HAMR TENS DAMP), a hard beater on a short, damped
    # shell, with a short TRX-BD (PTCH DEC RAMP RDEC STRT NOIS HARM CLIP) under it for punch
    'K': [(PI_BD, [70, 14, 100, 90, 60, 70, 0, 0], 1.30),
          (TRX_BD, [56, 28, 56, 30, 0, 24, 0, 10], 0.65)],
    # snare: P-I SD (PTCH DEC HARD RING TENS RVOL RDEC) tuned down with more shell ring,
    # mid-heavy, with TRX-SD (PTCH DEC BUMP BENV SNAP TONE TUNE CLIP) for the crack
    'S': [(PI_SD, [56, 56, 110, 100, 90, 80, 70, 0], 1.00),
          (TRX_SD, [50, 36, 50, 30, 110, 60, 50, 40], 0.45)],
    # ghost: the same snare struck softly, no crack layer
    'g': [(PI_SD, [56, 50, 50, 100, 90, 80, 60, 0], 1.00)],
    # ride: P-I RC (PTCH DEC HARD RING AG AU BR GRAB) tuned up and short: a dry, bright
    # ping, about 230 ms to -40 dB, so each eighth stands apart
    'R': [(PI_RC, [76, 95, 110, 30, 64, 0, 44, 127], 1.05)],
    # crash: P-I CC long for the attack, TRX-CY as the CC key (RICH DEC TOP TTUN SIZE PEAK) for the wash
    'C': [(PI_CC, [64, 127, 90, 0, 64, 0, 64, 127], 0.85),
          (TRX_CY, [100, 110, 64, 90, 40, 64, 0, 0], 0.65)],
    # closed hat: TRX-CH (GAP DEC HPF LPF MTAL), short and a little darker than the key's
    'h': [(TRX_CH, [0, 40, 40, 110, 80, 0, 0, 0], 0.55)],
    # foot chick: the PH key's pedal hat (TRX-CH, very short, high-passed)
    'p': [(TRX_CH, [0, 8, 90, 100, 30, 0, 0, 0], 0.55)],
    # open hat: TRX-OH, shortened, closed by the next hat
    'o': [(TRX_OH, [32, 50, 30, 110, 70, 0, 0, 0], 0.50)],
}
CHOKED = 'o'      # rings until the next hat (h or p), then closes
HATS = 'hpo'

# The pattern, one line per instrument, 16 sixteenths a bar, four bars.
# R ride, C crash, h closed hat, o open hat, p foot chick, S snare backbeat,
# g ghost snare, K kick. Velocity comes from the letter and the step (below).
PATTERN = {
    'ride ': ['R.R.R.R.R.R.R.R.', 'R.R.R.R.R.R.R.R.', 'R.R.R.R.R.R.R.R.', 'R.R.R.R.R.C.R.R.'],
    'hat  ': ['...h.......h...h', '...h...h...h..o.', '...h.......h...h', '...h...h........'],
    'foot ': ['....p.......p...', '....p.......p...', 'p...p.......p...', '....p...........'],
    'snare': ['....S..g.g..S..g', '....S..g.g..S..g', '....S..g.g....S.', '.g..S..g.g....S.'],
    'kick ': ['K.K.......KK....', 'K.K.......KK....', 'K.K.......K.....', '..KK......K.....'],
}

SWING = 0.0012     # seconds late on every off-sixteenth (the "e" and "a")
LAYBACK = 0.0030   # backbeat snares sit a touch behind
JITTER = 0.0012    # +- seconds of human timing, fixed per hit (seeded)


def velocity(kind, bar, step):
    if kind == 'R':
        return 1.0 if step % 4 == 0 else 0.72        # quarters lead, the "and"s lighter
    if kind == 'g':
        return {7: 0.40, 9: 0.34, 15: 0.42, 1: 0.36}.get(step, 0.38)
    if kind == 'S':
        return 1.0 if step in (4, 12) else 0.95       # the delayed backbeat in bars 3 and 4
    if kind == 'K':
        return 1.0 if step in (0, 10) else 0.86       # the second of a pair a little softer
    if kind == 'h':
        return {3: 0.30, 7: 0.24, 11: 0.34, 15: 0.40}.get(step, 0.3)   # pickups, the last leaning in
    if kind == 'p':
        return 0.45 if step in (4, 12) else 0.55      # the chick closing the open hat a little firmer
    if kind == 'o':
        return 0.55
    return 1.0


def events(bpm, bars=4):
    rng = np.random.default_rng(1969)
    sixteenth = 60.0 / bpm / 4
    out = []
    for name, rows in PATTERN.items():
        for bar, row in enumerate(rows[:bars]):
            for step, ch in enumerate(row):
                if ch == '.':
                    continue
                t = (bar * 16 + step) * sixteenth
                if step % 2:
                    t += SWING
                if ch == 'S':
                    t += LAYBACK
                t += float(rng.uniform(-JITTER, JITTER))
                v = velocity(ch, bar, step) * float(rng.uniform(0.96, 1.0))
                out.append((t, ch, v))
    return out


def render_dry(bpm, bars, frames):
    hits = sorted(events(bpm, bars))
    hat_starts = sorted(int(round(t * SR)) for t, ch, _ in hits if ch in HATS)
    lines = [str(frames)]
    for t, ch, v in hits:
        start = int(round(t * SR))
        choke = 0
        if ch in CHOKED:   # until the next hat, round the loop
            later = [h for h in hat_starts if h > start] or [h + frames for h in hat_starts]
            choke = later[0] - start
        for model, ctl, gain in KIT[ch]:
            lines.append(f"{start} {model} {' '.join(map(str, ctl))} {gain * v:.4f} {choke}")
    raw = subprocess.run([HITS], input=('\n'.join(lines) + '\n').encode(),
                         capture_output=True, check=True).stdout
    return np.frombuffer(raw, dtype=np.float32).astype(np.float64)


def onepole_hp(x, hz):
    a = np.exp(-2 * np.pi * hz / SR)
    y = np.zeros_like(x); prev_x = prev_y = 0.0
    for i, s in enumerate(x):
        prev_y = a * (prev_y + s - prev_x); prev_x = s; y[i] = prev_y
    return y


def onepole_lp(x, hz):
    a = np.exp(-2 * np.pi * hz / SR)
    y = np.zeros_like(x); p = 0.0
    for i, s in enumerate(x):
        p = (1 - a) * s + a * p; y[i] = p
    return y


def comb(x, delay, fb, damp):
    y = np.zeros_like(x); buf = np.zeros(delay); idx = 0; lp = 0.0
    for i, s in enumerate(x):
        o = buf[idx]; lp = o * (1 - damp) + lp * damp
        buf[idx] = s + lp * fb; idx = (idx + 1) % delay; y[i] = o
    return y


def allpass(x, delay, g=0.5):
    y = np.zeros_like(x); buf = np.zeros(delay); idx = 0
    for i, s in enumerate(x):
        b = buf[idx]; o = -s + b; buf[idx] = s + b * g; idx = (idx + 1) % delay; y[i] = o
    return y


def room(x):
    """A small, dark room: four damped combs into two allpasses (decay about 0.4 s)."""
    src = onepole_hp(x, 180.0)
    wet = sum(comb(src, d, 0.72, 0.35) for d in (1116 // 3, 1188 // 3, 1277 // 3, 1356 // 3)) / 4
    wet = allpass(allpass(wet, 225), 341)
    return onepole_lp(wet, 6500.0)


def compress(x, threshold_db=-16.0, ratio=2.5, attack=0.004, release=0.09):
    a_att = np.exp(-1 / (attack * SR)); a_rel = np.exp(-1 / (release * SR))
    env = 0.0; g = np.ones_like(x); thr = 10 ** (threshold_db / 20)
    for i, s in enumerate(np.abs(x)):
        env = a_att * env + (1 - a_att) * s if s > env else a_rel * env + (1 - a_rel) * s
        if env > thr:
            g[i] = (thr * (env / thr) ** (1 / ratio)) / env
    return x * g


def bus(dry, peak_db):
    n = len(dry)
    x = np.tile(dry, 3)                      # the loop as it repeats: tails and the room wrap
    x = x + 0.13 * room(x)                   # a touch of room
    x = compress(x, -20.0, 4.0, 0.003, 0.07) # squeezed, as a 60s funk drum bus
    x = np.tanh(2.2 * x / np.max(np.abs(x))) # crunch
    x = x[n:2 * n]                           # the middle pass: seamless both ends
    x = x - x.mean()
    return x * (10 ** (peak_db / 20) / np.max(np.abs(x)))


def write_wav(path, x):
    pcm = np.clip(np.round(x * 32767), -32768, 32767).astype('<i2')
    with wave.open(path, 'wb') as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(SR); w.writeframes(pcm.tobytes())


def grid_text(bpm, bars, frames):
    lines = [f"Four-bar break on MD KIT, {bpm:g} BPM, {bars} bars, {frames} frames at 44.1 kHz "
             f"({frames / SR:.4f} s); 16 steps a bar, 1 e + a 2 e + a 3 e + a 4 e + a",
             "R ride, C crash, h closed hat, o open hat (closed by the next hat), p foot chick, S snare backbeat, g ghost snare, K kick", ""]
    for bar in range(bars):
        lines.append(f"bar {bar + 1}   |1   2   3   4   |")
        for name, rows in PATTERN.items():
            r = rows[bar]
            lines.append(f"  {name} |{r}|")
        lines.append("")
    lines.append(f"swing {SWING * 1000:.1f} ms on the off-sixteenths, backbeat layback {LAYBACK * 1000:.1f} ms, "
                 f"jitter +-{JITTER * 1000:.1f} ms (seeded)")
    return '\n'.join(lines) + '\n'


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--bpm', type=float, default=120.0)
    p.add_argument('--bars', type=int, default=4)
    p.add_argument('--out', default=os.path.join(HERE, '..', '..', 'samples', 'break.wav'))
    p.add_argument('--grid', default=None)
    p.add_argument('--peak', type=float, default=-4.0,
                   help='peak in dBFS: -4 keeps Recur\'s seam crossfades under its output ceiling')
    p.add_argument('--mnmd', required=True, help='OP-XY-MNMD checkout (MD KIT)')
    p.add_argument('--sdk', required=True, help='Open-XY SDK checkout (its sdk/include)')
    p.add_argument('--cxx', default='clang++')
    a = p.parse_args()
    global HITS
    HITS = os.path.join(tempfile.mkdtemp(), 'hits')
    subprocess.run([a.cxx, '-std=c++11', '-O2', '-I' + os.path.join(a.sdk, 'sdk', 'include'),
                    '-I' + os.path.join(a.mnmd, 'machines', 'mdkit'), os.path.join(HERE, 'hits.cpp'), '-o', HITS],
                   check=True)
    frames = int(round(a.bars * 4 * 60.0 / a.bpm * SR))
    x = bus(render_dry(a.bpm, a.bars, frames), a.peak)
    write_wav(a.out, x)
    if a.grid:
        with open(a.grid, 'w') as f:
            f.write(grid_text(a.bpm, a.bars, frames))
    print(f"{a.out}: {frames} frames, {frames / SR:.4f} s, peak {20 * np.log10(np.max(np.abs(x))):.2f} dBFS")


if __name__ == '__main__':
    main()
