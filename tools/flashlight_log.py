"""Records the flashlight's flicker countdown during normal play (Windows; diagnostics).

Started by Teste-Lanterna-Normal.cmd with the game: GOW3_EXEC_WATCH_FILE points at
analysis/tlou/flashlight-watch.txt. First it watches 0x697b83 (the per-frame countdown: r13 is a
flashlight; Joel's and other characters' each run it) for a few seconds to collect them, then
watches instead
  0x6964a0  the countdown starts over from the first step (event 0xa84cd62c)
  0x6965b4  a scripted flicker (event 0x4269ddbc)
  0x697bd5  the countdown ran out: the flashlight flickers on its own
and writes, every second, one row per flashlight to analysis/tlou/flashlight-log.csv
  run, seconds, flashlight, countdown, group, step, event (start-over / scripted / flicker)
and prints a line per event. It keeps going across game runs (the launcher starts the game more
than once) until its window is closed. A run where the countdown keeps starting over before it
reaches zero is why the flashlight never flickers in play. (tools/flashlight_probe.py: the fields.)"""
import ctypes
import re
import struct
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import flashlight_probe as probe  # noqa: E402

ROOT = probe.ROOT
WATCH = ROOT / "analysis/tlou/flashlight-watch.txt"
LOG = ROOT / "analysis/tlou/boot.log"
CSV = ROOT / "analysis/tlou/flashlight-log.csv"
EVENTS = {"0x6964a0": "start-over", "0x6965b4": "scripted", "0x697bd5": "flicker"}
FIND, FOLLOW = "697b83", "6964a0,6965b4,697bd5"
COLLECT = 5  # seconds of 0x697b83 hits before only the events are watched


def watch_lines(offset):
    """New "Watch:" lines of boot.log after `offset`: ([(rip, {register: value})], new offset)."""
    try:
        with LOG.open("rb") as f:
            if f.seek(0, 2) < offset:  # a new run rewrote the log
                offset = 0
            f.seek(offset)
            data = f.read()
    except OSError:
        return [], offset
    end = data.rfind(b"\n") + 1
    found = []
    for line in data[:end].decode("utf-8", "replace").splitlines():
        rip = re.search(r"rip guest (0x[0-9a-f]+)", line)
        if line.startswith("Watch:") and rip:
            regs = {k: int(v, 16) for k, v in re.findall(r"\b(r\w\w|r\d+) (0x[0-9a-f]+|0)\b", line)}
            found.append((rip.group(1), regs))
    return found, offset + end


def flashlight_of(rip, regs):
    return regs.get("r13") if rip in ("0x697b83", "0x697bd5") else regs.get("rbx")


def record(out, run, pid):
    """One game run: returns when the game process is gone."""
    WATCH.write_text(FIND)
    handle = probe.k32.OpenProcess(0x0010, False, pid)
    offset, start, found_at = 0, time.monotonic(), None
    lights = {}  # flashlight object -> first second it was seen
    print(f"Run {run}: game found, turn the flashlight on in the game")
    while probe.game_pid() == pid:
        lines, offset = watch_lines(offset)
        now = time.monotonic() - start
        events = {}
        for rip, regs in lines:
            light = flashlight_of(rip, regs)
            if light is None:
                continue
            if light not in lights:
                lights[light] = now
                print(f"{now:6.0f} s  flashlight {light:#x} found ({len(lights)} so far)")
                if found_at is None:
                    found_at = now
            if rip in EVENTS:
                events.setdefault(light, []).append(EVENTS[rip])
        if found_at is not None and now - found_at >= COLLECT and WATCH.read_text() == FIND:
            WATCH.write_text(FOLLOW)
            print(f"{now:6.0f} s  recording {len(lights)} flashlights (keep playing; close this window to stop)")
        for light in lights:
            data = ctypes.create_string_buffer(12)
            got = ctypes.c_size_t()
            if not (probe.k32.ReadProcessMemory(handle, light + 0x7D4, data, 12, ctypes.byref(got))
                    and got.value == 12):
                continue
            countdown, group, step = struct.unpack("<fii", data.raw)
            happened = events.get(light, [])
            out.write(f"{run},{now:.0f},{light:#x},{countdown:.1f},{group},{step},{'+'.join(happened)}\n")
            for event in happened:
                print(f"{now:6.0f} s  {event:10}  flashlight {light:#x}  countdown {countdown:7.1f} s  step {group},{step}")
        out.flush()
        time.sleep(1)
    probe.k32.CloseHandle(handle)
    print(f"Run {run}: game closed ({len(lights)} flashlights seen). Waiting for the next run...")


def main():
    WATCH.write_text(FIND)
    run = 0
    with CSV.open("w", encoding="utf-8") as out:
        out.write("run,seconds,flashlight,countdown,group,step,event\n")
        print(f"Recording to {CSV}. Waiting for the game...")
        while True:
            pid = probe.game_pid()
            if pid:
                run += 1
                record(out, run, pid)
            time.sleep(1)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass
