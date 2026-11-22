#!/usr/bin/env bash
# Builds dist/TLOU-Remastered-x86_64.AppImage: the Linux package (tools/package_linux.sh)
# wrapped as a "thick" AppImage:
#   - the package itself (runtime, libgow3gpu.so, the bundled Python with Tkinter and
#     Pillow, the launcher, the scripts),
#   - every system shared library it links (bundle_libs.py copies them into lib/; AppRun
#     puts lib/ on LD_LIBRARY_PATH, so nothing from the host's Python, SDL3, Vulkan loader
#     or codec packages is needed),
#   - Tcl/Tk's data (init.tcl, tk.tcl, into share/tcl; AppRun points TCL_LIBRARY/TK_LIBRARY
#     at it: libtcl's fallback path only exists on the build machine, and on Fedora/Bazzite
#     the launcher died with "Can't find a usable init.tcl"),
#   - the Mesa Vulkan drivers for AMD and Intel GPUs (radeon/intel ICDs, used when the
#     host has no NVIDIA driver),
#   - AppRun (the entry: it exports GOW3_DATA_DIR, starts the launcher through
#     gow3_pc/launcher/gow3_vulkan.py, which selects the Vulkan driver; --play starts
#     the game directly), the .desktop and the icon.
# The game never ships (the player selects their own copy in the launcher, task 16).
# Needs: the package in dist/TLOU-Remastered-linux (bash tools/package_linux.sh first, or
# run this script without --no-package), linuxdeploy (Arch: linuxdeploy), the tcl and tk
# packages for the bundled Tcl/Tk data, and the vulkan-radeon and vulkan-intel packages
# for the bundled Mesa drivers.
# TLOU_APPIMAGE_ICON_GAME points the icon generator at the player's game folder (default:
# the launcher's saved game_folder, then /roms-ps4/.../CUSA00552).
set -euo pipefail
cd -- "$(dirname -- "$0")/../../.."
here=$PWD/gow3_pc/packaging/appimage
name=TLOU-Remastered-x86_64
package=dist/TLOU-Remastered-linux

command -v linuxdeploy >/dev/null || { echo 'Need linuxdeploy (Arch: sudo pacman -S linuxdeploy)' >&2; exit 1; }
if [[ ${1:-} != --no-package ]]; then
    bash tools/package_linux.sh --no-build
fi
[[ -f $package/TLOU-Launcher && -f $package/gow3_pc/out/gow3-probe && -f $package/python/bin/python3 ]] || {
    echo 'The package is not complete: bash tools/package_linux.sh' >&2; exit 1; }

appdir=dist/appimage/$name.AppDir
rm -rf -- dist/appimage
mkdir -p "$appdir"
# The package is the AppDir, flat (its scripts resolve everything relative to the top).
cp -a "$package"/. "$appdir"/
# AppRun (the entry: GOW3_DATA_DIR, the bundled libraries' path, the driver selection)
# and the desktop entry (the menu name and the AppImage's own name).
install -m 755 "$here/AppRun" "$appdir/AppRun"
mkdir -p "$appdir/usr/share/applications" "$appdir/usr/share/icons/hicolor/256x256/apps"
cp "$here/TLOU-Remastered-x86_64.desktop" "$appdir/usr/share/applications/"
python3 "$here/make_icon.py" "$appdir/tlou-remastered.png"
cp "$appdir/tlou-remastered.png" "$appdir/usr/share/icons/hicolor/256x256/apps/tlou-remastered.png"
python3 "$here/bundle_libs.py" "$appdir"
# Every NEEDED name must resolve inside the AppDir, or the package would depend on a
# host-only symlink (Bazzite has no zydis: gow3-probe died on libZydis.so.4.1 there).
python3 tools/needed_check.py "$appdir"
# The package check (no game file, no path of this machine) over the AppDir payload, without
# the zip (it ships as an AppImage, not a zip).
python3 tools/package_check.py "$appdir" -

# Arch's linuxdeploy is the plugin-based 1-alpha: its appimage output plugin writes the file
# after the desktop's Name (The_Last_of_Us_Remastered-x86_64.AppImage) in the current dir.
linuxdeploy --appdir "$appdir" -o appimage
mv ./The_Last_of_Us_Remastered-x86_64.AppImage "dist/$name.AppImage"
rm -rf -- "$appdir"
ls -lh "dist/$name.AppImage"
echo "Run: dist/$name.AppImage        (the launcher)"
echo "     dist/$name.AppImage --play (the game, with the launcher's saved settings)"
