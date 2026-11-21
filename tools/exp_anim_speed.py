#!/usr/bin/env python3
"""Measure the in-game animation speed from the displayed frames, without a human watching.

The main menu shows a window with swaying curtains (user, 19 Nov 2026: the animation runs at
twice speed on Linux). The menu background is otherwise mostly static, so the curtains are a
moving clock built into the game. This:

  1. starts the game through tools/run_game.py (the player's own path),
  2. waits until the menu is up (the PlayGo prefetch line in the boot log),
  3. touches the frame-dump trigger every SAMPLE seconds (the presenter saves the displayed
     frame each time the file appears, gow3_pc/gpu/shadps4/video_core/renderer_vulkan/
     vk_presenter.cpp), for CAPTURE seconds,
  4. reports the motion period of the moving pixels (the curtain): a period half of what the
     game has at the correct speed means the animation clock runs twice as fast.

Usage: exp_anim_speed.py [--scale N] [--seconds N]
  --scale    GOW3_TIME_SCALE (the guest's clocks run N times slower; 1 is the normal run)
  --seconds  game lifetime
The dumps go to /tmp/tlou-anim-<scale>. PIL only (no numpy): frames are downsampled to
160x90 before the per-frame difference.
"""
import argparse
import os
import re
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SAMPLE = 0.10  # s between frame dumps
CAPTURE = 24.0  # s of motion recorded (240 frames at 100 ms)
WIDTH, HEIGHT = 160, 90


def main():
    global SAMPLE
    parser = argparse.ArgumentParser()
    parser.add_argument("--scale", type=float, default=1.0)
    parser.add_argument("--seconds", type=int, default=int(CAPTURE) + 120)
    parser.add_argument("--analyze-only", action="store_true",
                        help="analyse an existing dump without starting the game")
    parser.add_argument("--dir", type=Path, default=None,
                        help="explicit dump directory (with analyze-only)")
    parser.add_argument("--sample", type=float, default=SAMPLE,
                        help="seconds between dumped frames (with analyze-only)")
    args = parser.parse_args()
    SAMPLE = args.sample
    if args.analyze_only:
        dump = args.dir or Path(f"/tmp/tlou-anim-{args.scale:g}")
        frames = sorted(dump.glob("output*.raw"))
        analyze(frames, f"{args.scale:g}")
        return 0
    tag = f"{args.scale:g}"
    dump = args.dir or Path(f"/tmp/tlou-anim-{tag}")
    if dump.exists():
        for child in dump.iterdir():
            child.unlink()
    else:
        dump.mkdir(parents=True)
    trigger = dump / "trigger"
    env = dict(os.environ)
    env["GOW3_FRAME_DUMP_TRIGGER"] = str(trigger)
    env["GOW3_DUMP_DIR"] = str(dump)
    if args.scale != 1.0:
        env["GOW3_TIME_SCALE"] = f"{args.scale:g}"
    env["GOW3_FRAME_STATS"] = "1"
    boot_log = ROOT / "analysis" / "tlou" / "boot.log"
    if boot_log.exists():
        boot_log.unlink()
    process = subprocess.Popen([sys.executable, "tools/run_game.py", "--seconds", str(args.seconds)],
                               cwd=ROOT, env=env)
    # Wait for the menu (PlayGo prefetch, after the game's first frames are drawn), then sample.
    menu_up = False
    start_wait = time.monotonic()
    while time.monotonic() - start_wait < 120:
        if boot_log.exists() and b"PlayGo prefetch" in boot_log.read_bytes():
            menu_up = True
            break
        if process.poll() is not None:
            print("the game exited before the menu came up")
            return 1
        time.sleep(0.5)
    if not menu_up:
        process.kill()
        print("no menu after 120 s")
        return 1
    t0 = time.monotonic()
    print(f"menu up after {time.monotonic() - start_wait:.1f} s; sampling every {SAMPLE}s "
          f"for {CAPTURE:.0f} s", flush=True)
    while time.monotonic() - t0 < CAPTURE:
        trigger.touch()
        time.sleep(SAMPLE)
    process.terminate()
    try:
        process.wait(timeout=10)
    except subprocess.TimeoutExpired:
        process.kill()
    frames = sorted(dump.glob("output*.raw"))
    print(f"frames captured: {len(frames)}", flush=True)
    if not frames:
        return 1
    analyze(frames, tag)
    return 0


def grayscale(raw):
    """The dumped frame (RGBA, full resolution) downsampled to a 160x90 grayscale image."""
    from PIL import Image
    w, h = int(re.search(r"_(\d+)x(\d+)", raw.name).group(1)), int(re.search(
        r"_(\d+)x(\d+)", raw.name).group(2))
    image = Image.frombytes("RGBA", (w, h), raw.read_bytes()[:w * h * 4]).convert("L")
    return image.resize((WIDTH, HEIGHT), Image.BILINEAR)


def analyze(frames, tag):
    from PIL import ImageChops
    images = [grayscale(raw) for raw in frames]
    # The motion per interval: how many pixels moved above the noise, and where (x centroid).
    motion, centroid = [], []
    for i in range(1, len(images)):
        diff = ImageChops.difference(images[i - 1], images[i])
        moved = diff.point(lambda p: 255 if p > 18 else 0)
        hist = moved.histogram()
        count = hist[255]
        if count:
            pixels = list(moved.getdata())
            cx = sum(x for x, p in enumerate(pixels) if p) / count
            motion.append(count)
            centroid.append(cx)
        else:
            motion.append(0)
            centroid.append(-1)
    moving = [i for i, m in enumerate(motion) if m > 0]
    if not moving:
        print("no motion found (is the curtain there?)")
        return
    print(f"scale={tag} frames={len(images)} sampled every {SAMPLE}s")
    # Period of the motion signal: autocorrelation peak, in samples.
    signal = motion[moving[0]:moving[-1] + 1]
    c = [v for v in signal if v > 0]
    s = [v - sum(signal) / len(signal) for v in signal]
    n = len(s)
    energy0 = sum(v * v for v in s)
    autocorr = [sum(s[k] * s[k + lag] for k in range(n - lag)) for lag in range(1, n // 2)]
    peak = max(range(len(autocorr)), key=lambda i: autocorr[i]) + 1
    print(f"motion: {sum(1 for v in signal if v > 0)}/{n} intervals moving, "
          f"mean {sum(v for v in signal if v > 0) / max(1, sum(1 for v in signal if v > 0)):.0f} px")
    if energy0 and autocorr[peak] > 0.3 * energy0 and autocorr[peak] > 0:
        print(f"motion period: ~{peak * SAMPLE:.2f} s (autocorrelation peak at {peak} samples)")
    else:
        print("motion period: no clear peak (weak autocorrelation)")
    # The signal, downsampled 10x, so a human can see the wave shape in the terminal.
    for start in range(0, n, 10):
        chunk = signal[start:start + 10]
        top = max(chunk) if chunk else 1
        print(f"t={start * SAMPLE:5.1f}s " + "".join(
            "#" * max(1, int(10 * v / top)) if v > 0 else "." for v in chunk))


if __name__ == "__main__":
    sys.exit(main())
