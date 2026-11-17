#!/usr/bin/env bash
# Builds dist/TLOU-Remastered-linux/ and dist/TLOU-Remastered-linux.zip: a folder that runs on an
# x86-64 Linux PC with a Vulkan GPU and the distro's libraries installed (they are listed in
# licenses/THIRD-PARTY.txt; the Arch package names are in gow3_pc/packaging/linux/README.md).
# Python, with Tkinter and Pillow, is bundled, so no Python install is needed.
#   gow3_pc/out/gow3-probe   the runtime; gow3_pc/out/gpu/libgow3gpu.so the video core
#   python/                  a copy of the Python that built it (tools/copy_python_linux.py)
#   launcher/ tools/         the launcher and the start script (tools/probe_tlou.py)
#   gow3_pc/scripts/         the preparation scripts; gow3_pc/out/pydeps/iced_x86 the red-zone tool
# The distro's shared libraries (libstdc++, SDL3, the Vulkan loader, the libav* codecs, ...) are
# not copied: a Linux package ships what the repository builds and what is not a package of the
# distro, and documents the rest (the Windows package ships the MSYS2 toolchain DLLs instead).
# No game file ships, the launcher's icon included (launcher/assets is the cover art): each player
# selects their own copy of the game in the launcher, which reads its art from there.
# In the repository. --no-build uses the runtime already in gow3_pc/out.
# PYTHON is the Python 3.10+ that gets bundled (default: `python3` on PATH); it must have tkinter
# (Arch: in the `python` package; Debian: python3-tk) and Pillow (Arch: python-pillow).
set -euo pipefail
cd -- "$(dirname -- "$0")/.."
name=TLOU-Remastered-linux

# The Python that gets bundled (looked up first: the interpreter is what the package runs on).
python=${PYTHON:-python3}
"$python" -c 'import sys; sys.exit(sys.version_info < (3, 10))' || {
    echo "The bundled Python must be 3.10+, this is $("$python" -V 2>&1)" >&2; exit 1; }
"$python" -c 'import tkinter, PIL' || {
    echo "The bundled Python needs tkinter and Pillow (Arch: sudo pacman -S python-pillow; Debian: python3-tk python3-pil)" >&2; exit 1; }

if [[ ${1:-} != --no-build ]]; then
    bash gow3_pc/build.sh
fi
[[ -f gow3_pc/out/gow3-probe ]] || { echo 'Build the runtime first: gow3_pc/build.sh' >&2; exit 1; }
[[ -f gow3_pc/out/pydeps/iced_x86/__init__.py ]] || {
    echo 'Need iced-x86: python3 -m pip install --no-cache-dir --target gow3_pc/out/pydeps iced-x86' >&2; exit 1; }
command -v patchelf >/dev/null || {
    echo 'Need patchelf, to drop the build-machine path from gow3-probe (Arch: sudo pacman -S patchelf)' >&2; exit 1; }

stage=dist/stage/$name
rm -rf -- dist/stage
mkdir -p "$stage/gow3_pc/out/gpu" "$stage/gow3_pc/out/pydeps" "$stage/gow3_pc/scripts" "$stage/tools" "$stage/launcher"

# The runtime without debug information (the build keeps it for the diagnostics; the packaged
# runtime is stripped like the Windows one).
strip --strip-debug -o "$stage/gow3_pc/out/gow3-probe" gow3_pc/out/gow3-probe
chmod 755 -- "$stage/gow3_pc/out/gow3-probe"
# The build's RUNPATH also names gow3_pc/out/gpu on this machine (convenience when running from
# the source tree); the package resolves libgow3gpu.so through $ORIGIN/gpu alone, and no path of
# this machine may ship (tools/package_check.py rejects it).
patchelf --set-rpath '$ORIGIN/gpu' "$stage/gow3_pc/out/gow3-probe"   # (replaces the existing RUNPATH)
# Every shared library the runtime loads that is not a system one: it ships in gpu/, the folder
# the runtime's $ORIGIN/gpu RUNPATH resolves to. System libraries are the distro's and are
# documented (licenses/THIRD-PARTY.txt), not copied.
ldd gow3_pc/out/gow3-probe gow3_pc/out/gpu/libgow3gpu.so | awk '/=>/ {print $3}' | sort -u | while read -r lib; do
    [[ $lib == "not found" ]] && {
        echo "The runtime is missing a library (see gow3_pc/packaging/linux/README.md)" >&2; exit 1; }
    case $lib in
        /lib/*|/lib64/*|/usr/lib/*|/usr/lib64/*|/usr/local/lib*) continue ;;  # the distro's
    esac
    cp "$lib" "$stage/gow3_pc/out/gpu/"
done
[[ -f $stage/gow3_pc/out/gpu/libgow3gpu.so ]] || cp gow3_pc/out/gpu/libgow3gpu.so "$stage/gow3_pc/out/gpu/"
strip --strip-debug "$stage/gow3_pc/out/gpu/libgow3gpu.so"
cp -r gow3_pc/out/pydeps/iced_x86 gow3_pc/out/pydeps/iced_x86-*.dist-info "$stage/gow3_pc/out/pydeps/"
rm -rf -- "$stage/gow3_pc/out/pydeps/iced_x86/__pycache__"   # (paths of this machine)
# iced_x86's modules ship as bytecode only. Its generated Code.py (only constants and docstrings)
# is flagged as JS/TrojanDropper.ModiLoader by ESET, Google and Ikarus and as Script.Trojan.Agent
# by GData on VirusTotal, and with it the whole package (#88, #112, #148). The Python that ships
# is a copy of this one, so the bytecode matches it. The type stubs are not needed to run.
"$python" -I -m compileall -q -b -o 2 "$stage/gow3_pc/out/pydeps/iced_x86"
find "$stage/gow3_pc/out/pydeps/iced_x86" -name '*.py' -delete
rm -f -- "$stage/gow3_pc/out/pydeps/iced_x86/_iced_x86_py.pyi"
cp gow3_pc/scripts/*.py "$stage/gow3_pc/scripts/"
cp tools/probe_tlou.py tools/apply_update.py tools/run_game.py "$stage/tools/"
# (run_game.py: the TLOU-Game starter's script, as TLOU Game.exe's on Windows)
cp launcher/tlou_launcher.pyw launcher/updater.py "$stage/launcher/"
# The starters: the package's entry points (TLOU-Launcher opens the launcher, TLOU-Game starts the game
# directly with the settings the launcher saved), beside python/ and gow3_pc/ at the top.
cp launcher/TLOU-Launcher TLOU-Game "$stage/"
chmod 755 -- "$stage/TLOU-Launcher" "$stage/TLOU-Game"
# The package's version: the launcher's updates (launcher/updater.py) compare it with the releases.
cp tools/VERSION "$stage/VERSION"
cp gow3_pc/packaging/linux/README-player.txt "$stage/README.txt"
cp gow3_pc/LICENSE "$stage/LICENSE.txt"

# Licenses of the sources built into the runtime, and the list of the system libraries it links.
licenses=$stage/licenses
mkdir -p "$licenses"
for dir in gow3_pc/gpu/third_party/* gow3_pc/third_party/*; do
    for file in "$dir"/LICENSE* "$dir"/COPYING* "$dir"/COPYRIGHT*; do
        [[ -f $file ]] || continue
        mkdir -p "$licenses/$(basename -- "$dir")"
        cp "$file" "$licenses/$(basename -- "$dir")/"
    done
done
{
    echo "Shared libraries the runtime links that are not shipped in this package: they come from"
    echo "the distribution (how to install them, per distro: README.txt)."
    ldd gow3_pc/out/gow3-probe gow3_pc/out/gpu/libgow3gpu.so | awk '/=>/ {print $3}' | sort -u | while read -r lib; do
        case $lib in
            /lib/*|/lib64/*|/usr/lib/*|/usr/lib64/*|/usr/local/lib*) ;;  # the distro's (listed)
            *) continue ;;
        esac
        owner=
        if command -v pacman >/dev/null 2>&1; then
            owner=$(LC_ALL=C pacman -Qo "$lib" 2>/dev/null | awk '{print $(NF-1)}' | head -1 || true)
        fi
        printf '%s%s\n' "$lib" "${owner:+  ($owner)}"
    done
} > "$licenses/THIRD-PARTY.txt"

"$python" tools/copy_python_linux.py "$stage/python"
# The interpreter has no RPATH: without this, on a machine with no system Python it would not
# find the libpython it links against (the one that ships with it, in python/lib).
patchelf --set-rpath '$ORIGIN/../lib' "$stage/python/bin/python3"

"$python" tools/package_check.py "$stage" "dist/$name.zip"
rm -rf -- "dist/$name"
mv "$stage" "dist/$name"
rm -rf -- dist/stage
ls -la "dist/$name.zip"
