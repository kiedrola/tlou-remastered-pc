#!/usr/bin/env python3
"""Capture the main menu of the current build and measure its animation speed.

Boots the game (tools/run_game.py) with GOW3_FRAME_STATS=1 (so boot.log records the
real FPS over time), waits for the menu (PlayGo prefetch in boot.log), then touches the
GOW3_FRAME_DUMP_TRIGGER every 250 ms (the presenter's own dump rate limit) for a while,
and measures the press-any-button pulse and curtain periods in the on-screen frames
(output_*.raw, timestamped by mtime) with tools/port_anim_speed.py.

Reference (PS4 30 fps title screen, tools/ref_anim_speed.py): pulse 2.50 s, curtain 8.9 s.

Usage: anim_capture.py [--seconds N] [--cap N]
  --seconds N  run_game.py total lifetime (default 420)
  --cap N      capture window in seconds (default 90)
"""
import argparse
import re
import shutil
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
GAME = "/roms-ps4/Roms/PS4/The Last of Us Remastered/CUSA00552-1.00"
DUMP = Path("/tmp/tlou-cap")
TRIGGER = Path("/tmp/tlou-frame-dump")
CAPLOG = DUMP / "capture.log"


def wait_menu(timeout=240):
    """Wait until boot.log shows the menu reached (PlayGo prefetch)."""
    deadline = time.time() + timeout
    seen = None
    while time.time() < deadline:
        try:
            text = (ROOT / "analysis/tlou/boot.log").read_text(errors="replace")[-60000:]
        except OSError:
            text = ""
        m = re.search(r"(\d{2}:\d{2}:\d{2}\.\d+).*PlayGo prefetch", text)
        if m:
            if seen is None:
                seen = time.time()
            if time.time() - seen > 15:  # let it settle
                return True
        time.sleep(2)
    return False


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--seconds", type=int, default=420)
    ap.add_argument("--cap", type=int, default=90)
    args = ap.parse_args()

    if TRIGGER.exists():
        TRIGGER.unlink()
    if DUMP.exists():
        shutil.rmtree(DUMP)
    DUMP.mkdir(parents=True)
    (ROOT / "analysis/tlou/boot.log").write_text("")
    log = CAPLOG.open("w")

    def say(msg):
        line = f"{datetime.now().isoformat()} {msg}"
        print(line, flush=True)
        log.write(line + "\n")
        log.flush()

    cmd = [sys.executable, str(ROOT / "tools/run_game.py"), "--seconds", str(args.seconds)]
    env = {
        "GOW3_FRAME_STATS": "1",
        "GOW3_FRAME_DUMP_TRIGGER": str(TRIGGER),
        "GOW3_DUMP_DIR": str(DUMP),
        "GOW3_PAD_FILE": str(ROOT / "analysis/tlou/pad-record-capture.txt"),
    }
    say("booting (run_game.py --seconds %d)" % args.seconds)
    proc = subprocess.Popen(cmd, cwd=ROOT, env={**__import__("os").environ, **env})
    try:
        if not wait_menu():
            say("menu never reached; killing")
            return 2
        say("menu reached, settling 10 s")
        time.sleep(10)
        say(f"capturing {args.cap} s (touch every 250 ms)")
        deadline = time.time() + args.cap
        while time.time() < deadline:
            TRIGGER.touch()
            # Keep the dump dir small: the guest-buffer files are corrupt dumps anyway.
            for f in DUMP.glob("display_*.raw"):
                try:
                    f.unlink()
                except OSError:
                    pass
            time.sleep(0.25)
        say("capture done")
        time.sleep(5)  # let the last dumps finish
    finally:
        proc.send_signal(subprocess.SIGINT)
        try:
            proc.wait(timeout=60)
        except subprocess.TimeoutExpired:
            proc.kill()
    n = len(list(DUMP.glob("output_*.raw")))
    say(f"{n} output frames captured")
    return 0


if __name__ == "__main__":
    sys.exit(main())
