#!/usr/bin/env bash
# Installs the starters TLOU-Launcher (launcher/TLOU-Launcher) and TLOU-Game (TLOU-Game). They are already
# executable scripts, the Linux counterparts of the starters TLOU Launcher.exe and TLOU Game.exe, which
# build-starters.sh compiles from launcher/tlou-launcher.c and tlou-game.c on Windows (no windres here).
# Nothing is compiled: the C starters exist to dodge antivirus heuristics on unsigned Windows binaries
# (issues #88, #112, #148), which does not apply to a Linux script, and the interpreter is the Python a
# package ships (python/bin/python3) or the system's python3 (the repository). This makes the starters
# executable (a checkout made elsewhere can have lost the bit), checks their syntax, and reports what
# Python a repository checkout needs (a packaged copy bundles its own).
# usage: build-linux.sh
set -euo pipefail
here=$(cd -- "$(dirname -- "$0")" && pwd)
root=$(dirname -- "$here")

# The executable bit: git keeps it, a zip or a checkout made on Windows does not.
chmod 755 -- "$here/TLOU-Launcher" "$root/TLOU-Game"

# A broken script would fail in the player's face, not in the build: check the syntax.
for script in "$here/TLOU-Launcher" "$root/TLOU-Game"; do
    bash -n "$script"
done
echo "Starters: $here/TLOU-Launcher and $root/TLOU-Game (both executable)"

# The starters find the Python themselves (python/bin/python3 in a package, python3 in the PATH in the
# repository). A repository checkout needs one with Tkinter (the launcher, and run_game.py, which reads
# the launcher's own code, import it): a note, not a failure, as a packaged copy bundles its own Python.
if ! command -v python3 >/dev/null; then
    echo "Note: no python3 in the PATH; from the repository the starters need one (Arch: in the `python` package)."
elif ! python3 -c 'import tkinter' 2>/dev/null; then
    echo "Note: the PATH python3 has no Tkinter (Arch: sudo pacman -S tk; Debian: python3-tk; Fedora: python3-tkinter)."
fi
