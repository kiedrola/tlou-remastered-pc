"""Check that every NEEDED soname of every packaged ELF resolves by file name inside the
AppDir (simulating a host with nothing installed but the kernel): lib/ first, then each
ELF's RUNPATH/RPATH ($ORIGIN expanded), then the usual python/ layout. A name that only
exists on the build host (a symlink the package did not ship) is reported missing.

usage: needed_check.py <AppDir>
"""
import os
import re
import subprocess
import sys
from pathlib import Path


def dynamic_lines(path):
    env = dict(os.environ, LC_ALL="C")
    out = subprocess.run(["readelf", "-dW", str(path)], capture_output=True,
                         text=True, env=env).stdout
    return out.splitlines()


def needed_and_rpath(path):
    needed, rpath = [], []
    for line in dynamic_lines(path):
        m = re.search(r"\(NEEDED\).*?\[([^\]]+)\]", line)
        if m:
            needed.append(m.group(1))
        m = re.search(r"\((?:RUNPATH|RPATH)\).*?\[([^\]]*)\]", line)
        if m:
            rpath.append(m.group(1))
    return needed, rpath


def elfs(appdir):
    found = [appdir / "gow3_pc/out/gow3-probe", appdir / "python/bin/python3"]
    for pattern in ("gow3_pc/out/gpu/*.so*", "python/lib/*.so*",
                    "python/lib/python3.*/*.so*", "python/lib/python3.*/lib-dynload/*.so*",
                    "lib/*.so*"):
        found.extend(sorted(appdir.glob(pattern)))
    return [p for p in found if p.is_file() and p.stat().st_size > 0 and
            p.read_bytes()[:4] == b"\x7fELF"]


# The host's dynamic loader and glibc are intentionally not bundled (the loader stays
# the host's; a glibc loaded under a different loader would not load at all).
HOST_PROVIDED = {"ld-linux-x86-64.so.2", "libc.so.6", "libm.so.6", "libpthread.so.0",
                 "libdl.so.2", "librt.so.1", "libutil.so.1", "libresolv.so.2",
                 "libnsl.so.1", "libanl.so.1", "libcrypt.so.1"}


def main():
    appdir = Path(sys.argv[1]).resolve()
    lib = appdir / "lib"
    lib_names = {p.name for p in lib.iterdir()} if lib.is_dir() else set()
    missing, checked = [], 0
    for elf in elfs(appdir):
        needed, rpath = needed_and_rpath(elf)
        for name in needed:
            if name in HOST_PROVIDED or name in lib_names:
                continue
            ok = False
            for entry in rpath:
                if not entry:
                    continue
                if entry.startswith("$ORIGIN"):
                    base = elf.parent / entry.removeprefix("$ORIGIN/")
                else:
                    base = Path(entry)
                if (base / name).is_file():
                    ok = True
                    break
            if not ok and (elf.parent.parent / "lib" / name).is_file():
                ok = True   # python/bin/python3 -> python/lib
            if not ok:
                missing.append(f"{elf.relative_to(appdir)}: {name}")
        checked += len(needed)
    print(f"{checked} NEEDED entries over {len(elfs(appdir))} ELFs")
    if missing:
        print("NOT resolvable inside the AppDir (host-only names):")
        for line in sorted(set(missing)):
            print(f"  {line}")
        sys.exit(1)
    print("All NEEDED names resolve inside the AppDir: the package is self-contained")


if __name__ == "__main__":
    main()
