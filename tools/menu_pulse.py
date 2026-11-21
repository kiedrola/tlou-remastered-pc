#!/usr/bin/env python3
"""Menu timing analysis: 'PULSA' text pulse period + left-curtain edge trace.

Usage: menu_pulse.py <dump_dir> <sample_s>
"""
import re
import sys
from pathlib import Path
from PIL import Image


def load_l(raw):
    w, h = int(re.search(r"_(\d+)x(\d+)", raw.name).group(1)), int(re.search(r"_(\d+)x(\d+)", raw.name).group(2))
    return Image.frombytes("RGBA", (w, h), raw.read_bytes()[:w * h * 4]).convert("L")


def autocorr_period(signal, sample):
    s = [v - sum(signal) / len(signal) for v in signal]
    n = len(s)
    energy0 = sum(v * v for v in s)
    if not energy0:
        return None, 0.0
    best_lag, best_val = 1, -1.0
    for lag in range(2, n // 2):
        val = sum(s[k] * s[k + lag] for k in range(n - lag))
        if val > best_val:
            best_lag, best_val = lag, val
    if best_val < 0.15 * energy0:
        return None, best_val / energy0
    return best_lag * sample, best_val / energy0


def main():
    dump = Path(sys.argv[1])
    sample = float(sys.argv[2])
    frames = sorted(dump.glob("output*.raw"))
    pulse, curtain = [], []
    for raw in frames:
        # skip truncated dumps (a kill in the middle of the write).
        m = re.search(r"_(\d+)x(\d+)", raw.name)
        if raw.stat().st_size < int(m.group(1)) * int(m.group(2)) * 4:
            continue
        img = load_l(raw)
        px = img.load()
        text = img.crop((250, 845, 660, 895))
        tpx = list(text.getdata())
        pulse.append(sum(tpx) / len(tpx))
        row_edges = []
        for y in range(100, 600, 4):
            ex = None
            for x in range(550, 950):
                if px[x, y] > 35:
                    ex = x
                    break
            row_edges.append(ex if ex is not None else 550)
        curtain.append(sorted(row_edges)[len(row_edges) // 2])
    menu_from = None
    for i in range(3, len(pulse)):
        if all(v > 38 for v in pulse[i - 3:i + 1]):
            menu_from = i
            break
    if menu_from is None:
        print(f"{dump}: menu not found")
        return
    p = pulse[menu_from:]
    c = curtain[menu_from:]
    period, corr = autocorr_period(p, sample)
    print(f"{dump}: menu from t={menu_from * sample:.1f}s, {len(p)} frames")
    if period:
        print(f"  PULSA pulse period: {period:.2f}s (corr {corr:.2f})")
    else:
        print(f"  PULSA pulse period: no peak (max corr {corr:.2f})")
    lo, hi = min(c), max(c)
    out = []
    for i in range(0, len(c), 2):
        pos = 0 if hi == lo else (c[i] - lo) * 40 // (hi - lo)
        out.append(" " * pos + "*")
    for start in range(0, len(out), 40):
        row = out[start:start + 40]
        t0 = (menu_from + start * 2) * sample
        t1 = (menu_from + min(start + 40, len(out)) * 2) * sample
        print(f"  curtain t={t0:5.1f}-{t1:5.1f}s (x {lo}..{hi}):")
        print("   " + "".join(r.ljust(1) for r in row))


if __name__ == "__main__":
    main()
