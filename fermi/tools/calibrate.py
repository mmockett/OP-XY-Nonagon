#!/usr/bin/env python3
"""Calibrates Fermi's energy knob and recurrence clock by simulating FPUT chains
(numpy, double precision), and writes tools/calibration.json for make_tables.py.
Slow: a few minutes on 10 cores. Run it only after changing the chain's model.

The model (chain.h has the fixed-point version): N masses, fixed ends, springs
f(d) = d + d^2 (alpha + beta d), in strain-normalized units: a note loads mode m
with unit bond strain, x_j = sin(j m pi / (N+1)) / d0, d0 = 2 sin(m pi / 2(N+1)).
The energy knob sets a strength g; springs s splits it:
    alpha = (1 - s) g,   beta = C alpha^2 + s g,   C = 1.6
(the C alpha^2 floor keeps the quadratic chain's well bounded). Symplectic
Euler (kick, drift) with dt 1/2, 1/4 or 1/8, chosen from the springs' stiffness.

For each N, m and s it finds three strengths: `pure` (mode m keeps over 98% of
its energy), `deep` (it gives up 70% and comes back) and `thermal` (the
spectrum's effective mode count reaches 0.6 of N: equipartition). Then it
measures the breathing period, from the autocorrelation of mode m's share of
the energy, at nine knob positions."""
import json, pathlib, sys
from multiprocessing import Pool
import numpy as np

C = 1.6
NS = (8, 12, 16, 20, 24, 28, 32)
MS = (1, 2, 3, 4)
SS = (0.0, 0.25, 0.5, 0.75, 1.0)
ES = tuple(i / 8 for i in range(9))
OUT = pathlib.Path(__file__).resolve().parent / "calibration.json"


def stiffness(al, be):
    return 1 + 5 * al + 18.75 * be


def dt_for(al, be):
    f = stiffness(al, be)
    return 0.5 if f <= 2.25 else (0.25 if f <= 9 else 0.125)


def simulate(N, m, cfgs, units, every):
    """cfgs: (alpha, beta, dt). Returns mode energies, runs x samples x modes."""
    R = len(cfgs)
    j = np.arange(1, N + 1)
    S = np.sqrt(2 / (N + 1)) * np.sin(np.outer(j, j) * np.pi / (N + 1))
    w = 2 * np.sin(j * np.pi / (2 * (N + 1)))
    x = np.tile(np.sin(j * m * np.pi / (N + 1)) / w[m - 1], (R, 1)) + 1e-6 * np.sin(j * 7.3)[None, :]
    v = np.zeros((R, N))
    al = np.array([c[0] for c in cfgs])[:, None]
    be = np.array([c[1] for c in cfgs])[:, None]
    dts = np.array([c[2] for c in cfgs])[:, None]
    every_tick = (dts / 0.125).astype(int)
    z = np.zeros((R, 1))

    def accel(x):
        d = np.diff(np.concatenate([z, x, z], 1), axis=1)
        f = d + d * d * (al + be * d)
        return f[:, 1:] - f[:, :-1]

    a = accel(x)
    fac = 1 - (w[None, :] * dts) ** 2 / 4
    out = []
    ev = int(every / 0.125)
    with np.errstate(all="ignore"):
        for n in range(int(units / 0.125)):
            mask = (n % every_tick) == 0
            d = np.where(mask, dts, 0.0)
            v += d * a
            x += d * v
            a = np.where(mask, accel(x), a)
            if n % ev == 0:
                A = (x - dts * v / 2) @ S
                B = v @ S
                out.append(0.5 * (B * B * fac + w * w * A * A))
    return np.array(out).transpose(1, 0, 2)


def neff(E):
    p = np.clip(E / E.sum(-1, keepdims=True), 1e-30, 1)
    return np.exp(-(p * np.log(p)).sum(-1)) / E.shape[-1]


def thresholds(job):
    N, m = job
    H = 40 * (N + 1) ** 2 / m ** 2 + 3000
    g = np.geomspace(1e-5, 4, 34)
    result = {}
    for s in SS:
        cfgs = []
        for v in g:
            al = (1 - s) * v
            be = C * al * al + s * v
            cfgs.append((al, be, dt_for(al, be)))
        E = simulate(N, m, cfgs, H, 10.0)
        ne = neff(E)
        T = E.shape[1]
        rows = []
        for r in range(len(g)):
            if not np.isfinite(E[r]).all():
                continue
            e1 = E[r, :, m - 1] / E[r].sum(-1)
            rows.append((g[r], 1 - e1.min(), ne[r, -T // 4:].mean()))
        pure = max(v for v, dip, n in rows if dip < 0.02)
        deep = min(v for v, dip, n in rows if dip > 0.7)
        thermal = min(v for v, dip, n in rows if n > 0.6)
        cap = max(v for v, dip, n in rows if stiffness((1 - s) * v, C * ((1 - s) * v) ** 2 + s * v) <= 24)
        t = min(thermal * 1.3, cap)
        r_ = min(deep, t / 1.5)
        result[s] = (min(pure / 16, r_ / 16), r_, t)
    return N, m, result


def strength(anchor, e):
    p, r, t = anchor
    return p * (r / p) ** (e / 0.5) if e <= 0.5 else r * (t / r) ** ((e - 0.5) / 0.5)


def breath(e1, every):
    x = e1 - e1.mean()
    n = len(x)
    if x.std() < 1e-9:
        return None
    f = np.fft.rfft(x, 2 * n)
    ac = np.fft.irfft(f * np.conj(f))[:n]
    ac /= ac[0]
    zc = int(np.argmax(ac < 0))
    if zc == 0 or zc >= n // 2:
        return None
    return (zc + int(np.argmax(ac[zc:n // 2]))) * every


def periods(job):
    N, m, anchors = job
    cfg = {}
    for ie, e in enumerate(ES):
        for i, s in enumerate(SS):
            g = strength(anchors[s], e)
            al = (1 - s) * g
            be = C * al * al + s * g
            cfg[(ie, i)] = (al, be, dt_for(al, be))
    out = {}
    for dt in (0.5, 0.25, 0.125):
        keys = [k for k, c in cfg.items() if c[2] == dt]
        if not keys:
            continue
        units = min({0.5: 2.5e4 * (N / 16) ** 2 / m + 4000, 0.25: 1.5e4 * (N / 16) ** 2 / m + 3000, 0.125: 8000}[dt], 90000)
        E = simulate(N, m, [cfg[k] for k in keys], units, 5.0)
        ne = neff(E)
        for i, k in enumerate(keys):
            e1 = E[i, :, m - 1] / E[i].sum(-1)
            T = breath(e1, 5.0) if np.isfinite(e1).all() else None
            out[k] = (T, float(ne[i, -len(e1) // 4:].mean()))
    # In chaos there is no recurrence: the period is held from the last
    # recurrent position and may only shorten as energy rises.
    table = []
    for i in range(len(SS)):
        row, last = [], None
        for ie in range(len(ES)):
            T, n = out[(ie, i)]
            if T is None or (n > 0.5 and last is not None):
                T = last if T is None else min(T, last)
            row.append(T)
            last = T
        table.append(row)
    return N, m, table


if __name__ == "__main__":
    with Pool(6) as pool:
        found = pool.map(thresholds, [(N, m) for N in NS for m in MS])
        anchors = {(N, m): r for N, m, r in found}
        timed = pool.map(periods, [(N, m, anchors[(N, m)]) for N in NS for m in MS])
    data = {"C": C, "N": NS, "m": MS, "s": SS, "e": ES,
            "anchors": {f"{N},{m}": [anchors[(N, m)][s] for s in SS] for N in NS for m in MS},
            "periods": {f"{N},{m}": t for N, m, t in timed}}
    OUT.write_text(json.dumps(data, indent=1))
    print("wrote", OUT)
