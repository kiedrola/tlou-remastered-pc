#!/usr/bin/env python3
"""Measure the animation periods of the reference video of the main menu (the window with
the swaying curtain and the pulsing "PRESS ANY BUTTON" text), in seconds.

The port's frame dumps (tools/exp_anim_speed.py, tools/menu_pulse.py) give the same signals
in presented-frame units; comparing the periods here tells us the speed factor of the port:
period(port) == period(ref) is normal speed; twice as small means the animations run 2x.

Usage: ref_anim_speed.py <frames_dir> <fps>
  frames_dir: frames named f_%04d.png (ffmpeg -vf fps=<fps>) of the menu screen.
The scene window is auto-detected (the video starts dark: logo/loading fade-in).
PIL only (no numpy).
"""
import sys
from pathlib import Path
from PIL import Image

# The menu screen at 640x360 (the video's native size).
TEXT_BOX = (80, 280, 190, 300)          # "PRESS ANY BUTTON"
EDGE_ROWS = range(40, 320, 8)           # curtain left edge (dark wall | white curtain)
EDGE_X0, EDGE_X1, EDGE_THRESH = 150, 330, 90
SCENE_TEST_BOX = (360, 40, 540, 280)    # the bright window: >90 means the scene is up


def scene_up(img):
    px = img.load()
    s = c = 0
    for y in range(SCENE_TEST_BOX[1], SCENE_TEST_BOX[3], 8):
        for x in range(SCENE_TEST_BOX[0], SCENE_TEST_BOX[2], 8):
            s += px[x, y]
            c += 1
    return s / c > 90


def edge_signal(img):
    """Median x of the curtain's left edge (first bright pixel of each row)."""
    px = img.load()
    edges = []
    for y in EDGE_ROWS:
        for x in range(EDGE_X0, EDGE_X1):
            if px[x, y] > EDGE_THRESH:
                edges.append(x)
                break
    if not edges:
        return None
    edges.sort()
    return edges[len(edges) // 2]


def text_signal(img):
    box = img.crop(TEXT_BOX)
    data = list(box.getdata())
    return sum(data) / len(data)


def autocorr_period(signal, fps):
    s = [v - sum(signal) / len(signal) for v in signal]
    n = len(s)
    energy0 = sum(v * v for v in s)
    if not energy0 or n < 8:
        return None, 0.0
    best_lag, best_val = 1, -1.0
    for lag in range(2, n // 3):
        val = sum(s[k] * s[k + lag] for k in range(n - lag))
        if val > best_val:
            best_lag, best_val = lag, val
    return best_lag / fps, best_val / energy0


def main():
    frames_dir = Path(sys.argv[1])
    fps = float(sys.argv[2])
    files = sorted(p for p in frames_dir.glob("f_*.png"))
    print(f"frames: {len(files)} @ {fps} fps = {len(files) / fps:.0f} s")
    start = 0
    for i, f in enumerate(files):
        if scene_up(Image.open(f).convert("L")):
            start = i
            break
    files = files[start:]
    print(f"scene from frame {start} (t={start / fps:.0f} s), "
          f"{len(files) / fps:.0f} s of scene")
    edges, pulse = [], []
    for f in files:
        img = Image.open(f).convert("L")
        w, h = img.size
        if (w, h) != (640, 360):
            img = img.resize((640, 360))
        edges.append(edge_signal(img))
        pulse.append(text_signal(img))
    valid = [e for e in edges if e is not None]
    print(f"curtain edge: {len(valid)}/{len(edges)} frames, "
          f"range {min(valid)}..{max(valid)} px, "
          f"stdev {sum((v - sum(valid) / len(valid)) ** 2 for v in valid) ** 0.5 / len(valid) ** 0.5:.2f}")
    period, corr = autocorr_period(valid, fps)
    if period:
        print(f"curtain edge period: {period:.2f} s (corr {corr:.2f})")
    else:
        print(f"curtain edge period: no peak (corr {corr:.2f})")
    lo, hi = min(pulse), max(pulse)
    print(f"text pulse: range {lo:.1f}..{hi:.1f}")
    period, corr = autocorr_period(pulse, fps)
    if period:
        print(f"press-any-button pulse period: {period:.2f} s (corr {corr:.2f})")
    else:
        print(f"press-any-button pulse period: no peak (corr {corr:.2f})")


if __name__ == "__main__":
    main()
