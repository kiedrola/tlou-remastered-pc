"""The AppImage's "thick" part: bundle the host's shared libraries the package links.

Collects the ldd closure of every ELF in the AppDir (the runtime, the GPU library's
third-party .so files, the bundled Python, its tkinter and Pillow extensions) plus the
Mesa Vulkan drivers for AMD and Intel (from the host's vulkan-radeon/vulkan-intel
packages, with their ICD manifests) and Tcl/Tk's data (init.tcl and friends, from the
host's tcl/tk packages, into share/tcl: libtcl looks for them in the build machine's
path, which does not exist on another distro), and copies each library once into
AppDir/lib. AppRun puts AppDir/lib on LD_LIBRARY_PATH and points TCL_LIBRARY/TK_LIBRARY
at the bundled data, so the AppImage no longer needs the host's Python, SDL3, the
Vulkan loader, the codecs, the Mesa drivers or the Tcl/Tk packages; the host's dynamic
loader (ld-linux) stays the host's, and the Vulkan ICDs are found by the bundled loader
in the usual system places (the host's NVIDIA driver) and in the manifests AppRun passes
to gow3_pc/launcher/gow3_vulkan.py (the bundled Mesa ones).

usage: bundle_libs.py <AppDir>
"""
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

SYSTEM_ROOTS = ("/lib/", "/lib64/", "/usr/lib/", "/usr/local/lib")
# glibc's own libraries are never bundled: the dynamic loader stays the host's, and a
# glibc mixed with a different loader (a different host) would not load at all.
GLIBC_NAMES = {"libc.so.6", "libm.so.6", "libpthread.so.0", "libdl.so.2", "librt.so.1",
               "libutil.so.1", "libresolv.so.2", "libnsl.so.1", "libanl.so.1", "libcrypt.so.1"}
MESA_DRIVERS = (("vulkan-radeon", "libvulkan_radeon.so", "radeon_icd"),
                ("vulkan-intel", "libvulkan_intel.so", "intel_icd"))


def elfs(appdir):
    found = [appdir / "gow3_pc/out/gow3-probe",
             appdir / "python/bin/python3"]
    for pattern in ("gow3_pc/out/gpu/*.so*", "python/lib/*.so*",
                    "python/lib/python3.*/*.so*", "python/lib/python3.*/lib-dynload/*.so*"):
        found.extend(sorted(appdir.glob(pattern)))
    return [p for p in found if p.is_file() and p.stat().st_size > 0]


def ldd_targets(binary):
    """The shared libraries the dynamic loader resolves for `binary`: (soname, path|None).

    Run ldd in a clean environment: it honors LD_LIBRARY_PATH, and a build from inside a
    sandbox (the developer's own AppImage, for instance) would resolve a library from the
    sandbox's mount instead of the system's and drop it from the bundle (libXss.so.1 was
    lost to the Pi AppImage this way, and the launcher would die on a host without it).
    """
    out = subprocess.run(["env", "-i", shutil.which("ldd"), str(binary)],
                         capture_output=True, text=True).stdout
    targets = []
    for line in out.splitlines():
        match = re.match(r"\s+(\S+?)\s+=>\s+(\S+)\s+\(0x[0-9a-f]+\)", line)
        if match:
            targets.append((match.group(1), match.group(2)))
            continue
        match = re.match(r"\s+(\S+?)\s+=>\s+not found", line)
        if match:
            raise SystemExit(f"{binary} is missing {match.group(1)}: the package would not run")
    return targets


def collect(appdir):
    """The host libraries to bundle: the ldd closure of the AppDir's ELFs, minus what the
    AppDir already ships and the dynamic loader (the host's stays)."""
    library_dirs = {str(p.parent) for p in elfs(appdir)}
    queue, bundled, missing = list(elfs(appdir)), {}, []
    while queue:
        binary = queue.pop(0)
        for soname, target in ldd_targets(binary):
            if target is None or target.startswith(str(appdir)):
                continue
            if not target.startswith(SYSTEM_ROOTS):
                missing.append(f"{soname} -> {target} (outside the system's lib dirs)")
                continue
            if soname.startswith("ld-linux") or soname in GLIBC_NAMES:
                continue   # the host's dynamic loader and glibc stay the host's
            real = Path(target).resolve()
            if real in bundled:
                continue
            bundled[real] = real.name
            if str(real.parent) not in library_dirs:
                queue.append(real)
    return bundled, missing


def tcl_data(appdir):
    """Tcl/Tk's data (init.tcl, tk.tcl and the rest, from the host's `tcl` and `tk`
    packages), into share/tcl. Tkinter starts with the Tcl script library, which libtcl
    looks for in the path baked in at build time (this machine's /usr/lib/tcl8.6): on a
    distro where that path does not exist (Fedora/Bazzite: /usr/lib64, or none at all)
    the launcher dies with "Can't find a usable init.tcl". The libraries (libtcl/libtk)
    already come through the ldd closure; AppRun points TCL_LIBRARY/TK_LIBRARY at this.
    """
    dest = appdir / "share" / "tcl"
    for package, marker in (("tcl", "init.tcl"), ("tk", "tk.tcl")):
        listing = subprocess.run(["pacman", "-Ql", package], capture_output=True, text=True)
        files = [line.rsplit(" ", 1)[1] for line in listing.stdout.splitlines()
                 if line.split() and line.split()[0] == package]
        source = next((Path(f).parent for f in files if f.rsplit("/", 1)[-1] == marker), None)
        if source is None:
            raise SystemExit(f"Install the {package} package (sudo pacman -S {package}) for "
                             f"the AppImage's bundled Tcl/Tk data")
        # The data's own name (tcl8.6, tk8.6); some distros nest it (tcl8.6.16/library).
        name = source.name if source.name not in ("library", "share") else source.parent.name
        # The symlinks inside are other packages' convenience links (graphviz -> /usr/lib/...),
        # dangling here: the package's own files are complete without them.
        shutil.copytree(source, dest / name, dirs_exist_ok=True,
                        ignore=lambda d, names: {n for n in names if (Path(d) / n).is_symlink()})


def mesa(appdir, queue, bundled):
    """The AMD/Intel Vulkan drivers and their ICD manifests, from the host's packages."""
    icd_dir = appdir / "share/vulkan/icd.d"
    icd_dir.mkdir(parents=True, exist_ok=True)
    for package, library, manifest_prefix in MESA_DRIVERS:
        listing = subprocess.run(["pacman", "-Ql", package], capture_output=True, text=True)
        files = [line.split()[-1] for line in listing.stdout.splitlines()
                 if len(line.split()) == 2]
        library_path = next((f for f in files if f.endswith("/" + library)), None)
        # The manifest's name varies across distros (radeon_icd.json, radeon_icd.x86_64.json...).
        manifest_path = next((f for f in files
                              if f.rsplit("/", 1)[-1].startswith(manifest_prefix)), None)
        if not library_path or not manifest_path:
            raise SystemExit(f"Install the {package} package (sudo pacman -S {package}) "
                             f"for the AppImage's bundled AMD/Intel Vulkan driver")
        shutil.copy2(manifest_path, icd_dir / Path(manifest_path).name)
        # The driver itself and its dependencies (libdrm, libgbm, ...) go through the
        # ldd closure like everything else.
        queue.append(Path(library_path))


def elf_soname(path):
    """The library's DT_SONAME, or None: the name the dynamic loader asks for."""
    out = subprocess.run(["readelf", "-dW", str(path)], capture_output=True, text=True,
                         env=dict(os.environ, LC_ALL="C")).stdout
    match = re.search(r"\(SONAME\).*?\[([^\]]+)\]", out)
    return match.group(1) if match else None


def main():
    appdir = Path(sys.argv[1]).resolve()
    lib = appdir / "lib"
    lib.mkdir(exist_ok=True)
    bundled, missing = collect(appdir)
    queue = list(bundled)
    tcl_data(appdir)
    mesa(appdir, queue, bundled)
    while queue:
        path = queue.pop(0)
        if path in bundled:
            continue
        bundled[path] = path.name
        for soname, target in ldd_targets(path):
            if target is None or target.startswith(str(appdir)):
                continue
            if not target.startswith(SYSTEM_ROOTS) or soname in GLIBC_NAMES \
                    or soname.startswith("ld-linux"):
                continue
            queue.append(Path(target).resolve())
    for path in sorted(bundled):
        if path.name.startswith("ld-linux") or path.name in GLIBC_NAMES:
            continue
        existing = lib / path.name
        if existing.exists() and existing.resolve() != path:
            raise SystemExit(f"Two different files named {path.name}: {existing} and {path}")
        if not existing.exists():
            shutil.copy2(path, existing)
    # The NEEDED entries of the packaged ELFs name libraries by SONAME, which on the
    # build host is usually a symlink to the real (longer) file name: libZydis.so.4.1 ->
    # libZydis.so.4.1.1.0. Shipping only the real file works on a host that has its own
    # copy of the soname (the build machine's system link hides it) and dies on a host
    # without it (Bazzite has no zydis: gow3-probe failed to load). Create the soname
    # links so every NEEDED name resolves inside lib/, ahead of whatever the host has.
    links = []
    for path in sorted(bundled):
        if path.name.startswith("ld-linux") or path.name in GLIBC_NAMES:
            continue
        soname = elf_soname(lib / path.name)
        if soname and soname != path.name and not (lib / soname).exists():
            (lib / soname).symlink_to(path.name)
            links.append(f"{soname} -> {path.name}")
    copied = sorted(p.name for p in lib.iterdir())
    print(f"{len(copied)} libraries in {lib} ({sum(p.stat().st_size for p in lib.iterdir()) >> 20} MiB)")
    for name in copied:
        print(f"  {name}")
    print(f"{len(links)} soname links in {lib}")
    for line in links:
        print(f"  {line}")
    if missing:
        print("WARNING, not bundled (outside the system's lib dirs):", file=sys.stderr)
        for line in missing:
            print(f"  {line}", file=sys.stderr)


if __name__ == "__main__":
    main()
