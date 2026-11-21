#!/usr/bin/env python3
"""Play TLOU automatically to a gameplay scene and capture frames for animation-speed
measurement (19 Nov 2026, the 2x speed bug): boot, wait the menu, press cross (continue or
new game), wait the load, then dump frames at ~4/s while the game runs.

Usage: python3 tools/play_capture.py [--dir DIR] [--pad PAD_TOKENS] [--play-seconds N]
"""
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DUMP = Path(sys.argv[sys.argv.index("--dir") + 1] if "--dir" in sys.argv else "/tmp/tlou-play")
PLAY_SECONDS = int(sys.argv[sys.argv.index("--play-seconds") + 1] if "--play-seconds" in sys.argv else 60)

def main() -> None:
    if DUMP.exists():
        shutil.rmtree(DUMP)
    DUMP.mkdir(parents=True)
    pad_file = DUMP / "pad.txt"
    trigger = DUMP / "trigger"
    env = dict(os.environ)
    env.update({
        "GOW3_FRAME_STATS": "1",
        "GOW3_PAD_FILE": str(pad_file),
        "GOW3_FRAME_DUMP_TRIGGER": str(trigger),
        "GOW3_DUMP_DIR": str(DUMP),
    })
    log = open("/tmp/tlou-play-run.log", "w")
    proc = subprocess.Popen([sys.executable, "tools/run_game.py", "--seconds", "600"],
                            cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT)
    print(f"game pid {proc.pid}", flush=True)
    # wait for the menu (the boot log reaches the PlayGo prefetch, then ~15 s for the menu).
    boot_log = ROOT / "analysis/tlou/boot.log"
    start = time.time()
    menu_at = None
    while time.time() - start < 120:
        if menu_at is None and boot_log.exists():
            tail = boot_log.read_text(errors="ignore")[-40000:]
            if "PlayGo" in tail or "prefetch" in tail:
                time.sleep(20)
                menu_at = time.time()
                print("menu expected", flush=True)
                break
        time.sleep(0.5)
    if menu_at is None:
        print("no menu in 120 s, aborting")
        proc.kill()
        return
    # one menu frame: touch the trigger twice (the check window is 250 ms).
    for _ in range(4):
        trigger.touch()
        time.sleep(0.3)
    time.sleep(1.0)
    print("menu frames dumped; press sequence starts", flush=True)
    # enter the game: cross three times (title -> main menu with THE LAST OF US selected ->
    # the story screen -> CONTINUAR with a save).
    for i in range(3):
        pad_file.write_text("pad cross\n")
        time.sleep(3)
        print(f"cross #{i+1}", flush=True)
    pad_file.write_text("")  # release
    # wait for the load to finish: frames go from black (loading) to a bright scene.
    scene_since = None
    time.sleep(5)
    while time.time() - (menu_at) < 150:
        for _ in range(4):
            trigger.touch()
            time.sleep(0.3)
        time.sleep(0.6)
        files = sorted(DUMP.glob("display_*.raw"))
        if not files:
            continue
        from PIL import Image
        last = files[-1]
        m = re.search(r"_(\d+)x(\d+)", last.name)
        w, h = int(m.group(1)), int(m.group(2))
        data = last.read_bytes()[: w * h * 4]
        img = Image.frombytes("RGBA", (w, h), data).convert("L")
        # sample the middle band (the loading screen is dark, the scene is bright).
        band = list(img.crop((w // 4, h // 3, 3 * w // 4, 2 * h // 3)).getdata())
        mean = sum(band) / len(band)
        print(f"t+{time.time()-menu_at:5.1f}s mean {mean:5.1f} ({last.name})", flush=True)
        if mean > 25:
            if scene_since is None:
                scene_since = time.time()
            elif time.time() - scene_since > 3:
                print("scene stable for 3 s: capturing", flush=True)
                break
        else:
            scene_since = None
    # capture the play scene at ~4/s.
    before = sorted(DUMP.glob("display_*.raw"))
    m = re.search(r"_(\d+)x(\d+)", before[-1].name) if before else None
    cap_index = int(re.search(r"display_(\d+)", before[-1].name).group(1)) + 1 if before else 0
    (DUMP / "capture_start.txt").write_text(str(cap_index))
    print(f"capture starts at frame index {cap_index}", flush=True)
    t_end = time.time() + PLAY_SECONDS
    while time.time() < t_end:
        trigger.touch()
        time.sleep(0.25)
    pad_file.write_text("")
    proc.terminate()
    try:
        proc.wait(timeout=15)
    except subprocess.TimeoutExpired:
        proc.kill()
    n = len(list(DUMP.glob("display_*.raw")))
    print(f"done: {n} frames in {DUMP}", flush=True)

if __name__ == "__main__":
    main()
