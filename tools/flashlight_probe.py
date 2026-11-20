"""Reads the flashlight's flicker state from the running game (Windows; diagnostics).

Start the game with Teste-Lanterna-Rastro.cmd (breakpoint 0x697b83 logs r13, the flashlight, and
rbx, the flicker record in use, to analysis/tlou/boot.log), turn the flashlight on and run this.
Fields (eboot code 0x696f98-0x697bd5; the cheat in probe_tlou.py CHEATS):
  +0x7d4 countdown to the next flicker (less the frame time [[+0xb0]+0x18] each frame)
  +0x7d8 sequence (low: group, high: step), +0x7e0 idle record, +0x7ed forced flicker
  [rbx] the record's time: 0 or less never flickers
  image+0x15f5167: a global flag that holds flickers off while set
--watch: print every half second. --force: also set the countdown to -1 once (a flicker now)."""
import ctypes
import ctypes.wintypes as w
import re
import struct
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
IMAGE = 0xB400000
k32 = ctypes.WinDLL("kernel32", use_last_error=True)
k32.OpenProcess.restype = w.HANDLE
k32.ReadProcessMemory.argtypes = [w.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t,
                                  ctypes.POINTER(ctypes.c_size_t)]
k32.WriteProcessMemory.argtypes = k32.ReadProcessMemory.argtypes


def game_pid():
    out = subprocess.run(["tasklist", "/FI", "IMAGENAME eq gow3-probe.exe", "/FO", "CSV", "/NH"],
                         capture_output=True, text=True, errors="replace").stdout
    found = re.search(r'"gow3-probe\.exe","(\d+)"', out)
    return int(found.group(1)) if found else None


def registers():
    text = (ROOT / "analysis/tlou/boot.log").read_text(errors="replace")
    lines = [l for l in text.splitlines() if "rip guest 0x697b83" in l]
    if not lines:
        return None
    regs = dict(re.findall(r"\b(r\w\w|r\d+) (0x[0-9a-f]+|0)\b", lines[-1]))
    return {k: int(v, 16) for k, v in regs.items()}


def main():
    pid = game_pid()
    if not pid:
        sys.exit("The game (gow3-probe.exe) is not running")
    regs = registers()
    if not regs:
        sys.exit("No 'rip guest 0x697b83' line in analysis/tlou/boot.log: start with Teste-Lanterna-Rastro.cmd")
    handle = k32.OpenProcess(0x0010 | 0x0020 | 0x0008, False, pid)   # VM read, write, operation
    if not handle:
        sys.exit(f"OpenProcess failed: {ctypes.get_last_error()}")

    def read(address, size):
        buffer = ctypes.create_string_buffer(size)
        got = ctypes.c_size_t()
        if not k32.ReadProcessMemory(handle, address, buffer, size, ctypes.byref(got)) or got.value != size:
            raise OSError(f"cannot read {address:#x}")
        return buffer.raw

    r13, rbx = regs["r13"], regs["rbx"]
    if "--force" in sys.argv:
        data = struct.pack("<f", -1.0)
        k32.WriteProcessMemory(handle, r13 + 0x7D4, data, 4, None)
        print("Countdown set to -1: the flashlight should flicker now")
    # --seconds N: stop watching after N seconds.
    seconds = float(sys.argv[sys.argv.index("--seconds") + 1]) if "--seconds" in sys.argv else None
    end = time.monotonic() + seconds if seconds else None
    while True:
        try:
            countdown, = struct.unpack("<f", read(r13 + 0x7D4, 4))
            group, step = struct.unpack("<ii", read(r13 + 0x7D8, 8))
            idle, = struct.unpack("<i", read(r13 + 0x7E0, 4))
            forced = read(r13 + 0x7ED, 1)[0]
            record = struct.unpack("<8f", read(rbx, 32))
            clock, = struct.unpack("<Q", read(r13 + 0xB0, 8))
            frame, = struct.unpack("<f", read(clock + 0x18, 4)) if clock else (float("nan"),)
            flag = read(IMAGE + 0x15F5167, 1)[0]
            print(f"countdown {countdown:10.3f}  frame {frame:.4f}  sequence {group},{step}  idle record {idle}  "
                  f"forced {forced}  global flag {flag}  record {', '.join(f'{v:g}' for v in record)}")
        except OSError as error:   # the flashlight moved or the game closed
            print(error)
        if "--watch" not in sys.argv or (end and time.monotonic() > end):
            break
        time.sleep(0.5)


if __name__ == "__main__":
    main()
