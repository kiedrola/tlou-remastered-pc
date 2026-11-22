"""Copies the running Linux Python into the package (python/): the interpreter, the libpython it
links against, the standard library without tests and tools, and Pillow. Players need no Python
installed. The Tcl/Tk runtime Tkinter uses (the `tcl` and `tk` packages) is the distribution's,
as in a system Python: its libraries come through the package's ldd closure, and its data is
documented in the package's README (the zip) or bundled by the AppImage's bundle_libs.py.

usage: copy_python_linux.py <destination>
"""
import re
import shutil
import subprocess
import sys
from pathlib import Path
import sysconfig

SKIP_LIB = {"site-packages", "test", "tests", "idlelib", "ensurepip", "turtledemo", "__pycache__",
            "lib2to3", "venv", "pydoc_data"}
SITE_PACKAGES = ("PIL",)


def main():
    dest = Path(sys.argv[1])
    dest.mkdir(parents=True, exist_ok=True)
    exe = Path(sys.executable).resolve()

    # The interpreter (python/bin/python3): the real binary, whatever symlinks point at it.
    (dest / "bin").mkdir(exist_ok=True)
    shutil.copy2(exe, dest / "bin" / "python3")

    # The standard library without the test trees and the tools a player never runs.
    stdlib = Path(sysconfig.get_paths()["stdlib"])

    def ignore_lib(directory, names):
        return {name for name in names if name in SKIP_LIB or name.endswith(".pyc")}

    std = dest / "lib" / stdlib.name
    shutil.copytree(stdlib, std, dirs_exist_ok=True, ignore=ignore_lib)
    site = std / "site-packages"
    site.mkdir(exist_ok=True)
    for name in SITE_PACKAGES:
        found = sorted((stdlib / "site-packages").glob(name + "*"))
        if not found:
            raise SystemExit(f"{name} is not installed (Arch: sudo pacman -S python-pillow)")
        for path in found:
            if path.is_dir():
                shutil.copytree(path, site / path.name, dirs_exist_ok=True,
                                ignore=shutil.ignore_patterns("__pycache__"))
            else:
                shutil.copy2(path, site / path.name)
    for path in (stdlib / "site-packages").glob("pillow*"):
        if path.is_dir():
            shutil.copytree(path, site / path.name, dirs_exist_ok=True,
                            ignore=shutil.ignore_patterns("__pycache__"))

    # The Tcl/Tk extension (the launcher is a Tkinter app): it must be in the copy. Its libraries
    # and data are the distribution's (the package's README says to install `tcl` and `tk`).
    if not list((std / "lib-dynload").glob("_tkinter*.so")):
        raise SystemExit("tkinter is not available in this Python: install the distro's tkinter "
                         "package (Debian: python3-tk, Fedora: python3-tkinter)")

    # libpython: the interpreter links against it (a Python built with --disable-shared has none).
    out = subprocess.run(["ldd", str(exe)], capture_output=True, text=True, check=True).stdout
    for line in out.splitlines():
        m = re.match(r"\s*libpython[^ ]*\.so[^ ]*\s*=>\s*(\S+)", line)
        if m:
            shutil.copy2(m.group(1), dest / "lib" / Path(m.group(1)).name)


if __name__ == "__main__":
    main()
