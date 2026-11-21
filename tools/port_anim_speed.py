#!/usr/bin/env python3
"""Measure the animation periods (press-any-button pulse, curtain sway) in a port frame dump
(the output_*.raw files: the actual image shown on screen, 1920x1080), in real seconds
(timestamped by the file mtime, so frame drops do not bias the period).

Usage: port_anim_speed.py <dump_dir> [sample_note]
Compare with tools/ref_anim_speed.py (the reference video: pulse 2.50 s, curtain 8.9 s).
A port period half of the reference means the animation runs at twice speed.
PIL only (no numpy).
"""
import math
import sys
from datetime import datetime
from pathlib import Path
from PIL import Image

W, H = 1920, 1080
TEXT_BOX = (590, 915, 860, 975)      # "PULSA CUALQUIER BOTON" (1920x1080 screen)
CURTAIN_ROWS = range(120, 960, 24)   # curtain left edge (dark wall | white curtain)
CURTAIN_X0, CURTAIN_X1, CURTAIN_THRESH = 560, 1250, 90
SCENE_BOX = (1080, 120, 1620, 840)   # the bright window


def scene_up(px):
    s = c = 0
    for y in range(SCENE_BOX[1], SCENE_BOX[3], 24):
        for x in range(SCENE_BOX[0], SCENE_BOX[2], 24):
            s += px[x, y]
            c += 1
    return s / c > 90


def text_signal(px):
    s = c = 0
    for y in range(TEXT_BOX[1], TEXT_BOX[3], 2):
        for x in range(TEXT_BOX[0], TEXT_BOX[2], 2):
            s += px[x, y]
            c += 1
    return s / c


def curtain_edge(px):
    edges = []
    for y in CURTAIN_ROWS:
        for x in range(CURTAIN_X0, CURTAIN_X1):
            if px[x, y] > CURTAIN_THRESH:
                edges.append(x)
                break
    if not edges:
        return None
    edges.sort()
    return edges[len(edges) // 2]


def resample(times, values, dt=0.05):
    """Linear resample onto a uniform grid (skips gaps > 2 s)."""
    t0 = times[0]
    t1 = times[-1]
    grid, sig = [], []
    j = 0
    t = t0
    while t <= t1:
        while j < len(times) - 2 and times[j + 1] < t:
            if times[j + 1] - times[j] > 2.0:
                break
            j += 1
        if times[j + 1] - times[j] > 2.0:
            grid.append(None)
            sig.append(None)
        elif j + 1 < len(times) and t <= times[j + 1]:
            a = (t - times[j]) / (times[j + 1] - times[j])
            grid.append(t)
            sig.append(values[j] + a * (values[j + 1] - values[j]))
        else:
            grid.append(None)
            sig.append(values[-1])
        t += dt
    out_t, out_v = [], []
    for tt, vv in zip(grid, sig):
        if vv is not None:
            out_t.append(tt)
            out_v.append(vv)
    # drop the longest internal gap by trimming to the biggest contiguous run.
    best = run = []
    for tt, vv in zip(out_t, out_v):
        if run and tt - run[-1][0] > 2.0:
            if len(run) > len(best):
                best = run
            run = []
        run.append((tt, vv))
    if len(run) > len(best):
        best = run
    return [t for t, _ in best], [v for _, v in best]


def power(sig, dt, period_s):
    m = sum(sig) / len(sig)
    s = [v - m for v in sig]
    w = 2 * math.pi / (period_s / dt)
    re = sum(s[k] * math.cos(w * k) for k in range(0, len(s), 2))
    im = sum(s[k] * math.sin(w * k) for k in range(0, len(s), 2))
    return re * re + im * im


def best_periods(sig, dt, p0, p1, step, top=6):
    res = []
    p = p0
    while p <= p1:
        res.append((power(sig, dt, p), p))
        p += step
    res.sort(reverse=True)
    return res[:top]


def main():
    dump = Path(sys.argv[1])
    files = sorted(dump.glob("output_*.raw"))
    times, pulses, edges = [], [], []
    scene_from = None
    for f in files:
        if f.stat().st_size < W * H * 4:
            continue
        img = Image.frombytes("RGBA", (W, H), f.read_bytes()).convert("L")
        px = img.load()
        if scene_from is None and not scene_up(px):
            continue
        if scene_from is None:
            scene_from = f.name
        t = datetime.fromtimestamp(f.stat().st_mtime).timestamp()
        times.append(t)
        pulses.append(text_signal(px))
        edges.append(curtain_edge(px))
    if len(times) < 20:
        print(f"{dump}: not enough scene frames ({len(times)})")
        return
    t0 = times[0]
    span = times[-1] - t0
    print(f"{dump}: scene from {scene_from}, {len(times)} frames over {span:.0f} s "
          f"({len(times) / span:.2f} dumps/s)")
    dt = 0.05
    # pulse: keep the frames where the text is up or down (all scene frames).
    pt, pv = resample(times, pulses, dt)
    et, ev = resample(times, [e if e is not None else 0 for e in edges], dt)
    print(f"pulse: {len(pv)} samples, range {min(pv):.1f}..{max(pv):.1f}")
    for pw, p in best_periods(pv, dt, 0.8, 5.0, 0.01, 4):
        print(f"  pulse period {p:6.2f} s : power {pw:14.0f}")
    evalid = [e for e in ev]
    print(f"curtain edge: range {min(evalid):.0f}..{max(evalid):.0f} px")
    for pw, p in best_periods(evalid, dt, 3.0, 20.0, 0.05, 4):
        print(f"  curtain period {p:6.2f} s : power {pw:14.0f}")


if __name__ == "__main__":
    main()
