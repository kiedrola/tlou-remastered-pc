# The Last of Us Remastered — native Linux port (player and developer notes)

A native x86-64 Linux port of *The Last of Us Remastered* (PS4, `CUSA00552`, update 1.11).
**The game files are not included and never ship inside the package**: you point the launcher
at your own decrypted copy (the folder with `eboot.bin`; the update is looked for beside it,
in the same folder named with a `-patch` suffix, e.g. `CUSA00552` and `CUSA00552-patch`).

## Requirements

- 64-bit (x86-64) Linux with a Vulkan graphics card.
- A Vulkan driver:
  - **NVIDIA**: your system's driver is required (proprietary or NVK) — it must match your
    kernel module, so it can't be bundled. Tested: GTX 1060 3GB (low-VRAM mode kicks in by
    itself on 3 GiB or less), RTX 3090.
  - **AMD**: every GPU with Vulkan support in Mesa — Polaris (RX 400/500), Vega, all RDNA
    (RX 5000/6000/7000/9000), CDNA (Instinct) and the PS5's GPU (RDNA2). Verified running on
    a PS5 (ps5-bazzite-deck) with the bundled Mesa.
  - **Intel**: Haswell (4th generation) and newer, including current iGPUs and Arc.
  - Older than that (pre-Polaris AMD, pre-Haswell Intel): no Vulkan driver exists in any Mesa
    (not even the system's) — unsupported.
- The **AppImage** is fully self-contained (embedded Python, runtime libraries and the Mesa
  26.3.1 Vulkan drivers for AMD/Intel): nothing to install.
- The **zip package** needs your distribution's libraries: SDL3, the ffmpeg libraries
  (libavcodec/libavformat/libavutil/libswscale), libfmt, libzydis, libxxhash, libX11, the
  Vulkan ICD loader, Tcl/Tk and a Vulkan driver (Mesa's or NVIDIA's). On Arch:
  `sudo pacman -S sdl3 ffmpeg fmt zydis xxhash libx11 vulkan-icd-loader tcl tk` plus
  `mesa-vulkan-drivers` (AMD/Intel).
- An SSD helps: the first start compiles the shaders into a cache (~250 MB); later starts are
  faster while the cache warms up.

## Packages

### AppImage (recommended)

```
chmod +x TLOU-Remastered-x86_64.AppImage
./TLOU-Remastered-x86_64.AppImage            # the launcher
./TLOU-Remastered-x86_64.AppImage --play     # the game, with the launcher's saved settings
```

Player data (settings, saves, logs, caches) lives in `~/.local/share/tlou-remastered`
(see "Player data" below); the AppImage itself is read-only and can be deleted after use —
your saves and settings survive it.

### Zip

Extract to a folder with plenty of free space, then (if your extractor dropped the
executable bit): `chmod +x TLOU-Launcher TLOU-Game`. Start `./TLOU-Launcher`, or `./TLOU-Game`
directly. Player data lives in `analysis/` inside the extracted folder — keep that folder
when you update by hand.

## First start

1. In the launcher, choose your **game files** folder (the one with `eboot.bin`).
2. Click **PLAY**. The first start takes longer (shader compilation); stutters may appear
   until the cache is warm.
3. If the frame rate swings, set **FPS = 30** (launcher → Video, or in-game **PC SETTINGS →
   FRAME RATE LIMIT**, which changes live). The game's engine is 30 fps frame-based on the
   PS4 base (the port emulates the base PS4; see `linux/roadmap-es.md` for the real 60 fps
   mode, an engine task) — 30 is the default on Linux and the mode's correct pacing.

## Player data

Root folder: `GOW3_DATA_DIR` when set; otherwise `analysis/` inside an extracted package, or
`~/.local/share/tlou-remastered` inside an AppImage (follows `XDG_DATA_HOME`).

```
<root>/
  tlou/        run logs (boot.log — the main diagnostic), boot preparation, pad recordings
  tlou-user/   tlou.ini (the settings), launcher.json, saves, the shader cache
```

`GOW3_DATA_DIR=/some/path` relocates everything (a shared data folder, tests, a read-only
home).

## Common environment variables

Player-facing (the launcher already sets what it needs; these matter mostly for running
`TLOU-Game` / the runner directly):

| Variable | Meaning |
|---|---|
| `TLOU_GAME=<folder>` | The game folder (with `eboot.bin`), when not choosing it in the launcher. |
| `TLOU_UPDATE=<folder>` (or `0`) | The update folder; by default `<game folder>-patch` or `-update` beside it. |
| `TLOU_RESOLUTION=<W>x<H>` | The render resolution (also launcher → Video → Resolution). |
| `TLOU_CHEATS=<name>,...` | Cheats (default: flashlight; `none`: off). |
| `TLOU_FIXES=none` | Leave the built-in fixes out. |
| `TLOU_RECORD_PAD=0` | Don't record controller input (recordings go to `<root>/tlou`). |
| `GOW3_DATA_DIR=<folder>` | The player data's root (above). |
| `GOW3_CONFIG=<file>` | The settings file (default: `<root>/tlou-user/tlou.ini`). |
| `GOW3_LOW_VRAM=auto\|1\|0` | Low-VRAM mode; `auto` (default): on for discrete cards with ≤ 3 GiB. |
| `GOW3_GUEST_ARGS="<args>"` | Extra arguments for the game's `main` (the engine's debug options, e.g. `forcefps 30`). |

Many other `GOW3_*` variables are development/diagnostic switches; the defaults and their
meaning are in the comments of `tools/probe_tlou.py`.

## Troubleshooting

- **The main log** is `<root>/tlou/boot.log` (launcher → "Open log"). Its first lines name
  the driver chosen and the frame limit in effect (`VideoOut: vblank ... frame limit N FPS`).
- **The Vulkan line at start**: on an NVIDIA machine, "no accessible NVIDIA ICD found" means
  the host's NVIDIA driver is missing or its Vulkan manifest isn't in the usual places. On an
  AMD/Intel machine, "using the bundled Mesa drivers" (AppImage) is the normal path, not an
  error.
- **`Can't find a usable init.tcl`**: the AppImage bundles Tcl/Tk's data (`share/tcl`) and
  sets `TCL_LIBRARY`/`TK_LIBRARY`; if you see this from the AppImage, report it (it was fixed
  2026-11-22). From the zip, install the distro's `tcl`/`tk` packages.
- **Shared-library errors from the game** (e.g. `libZydis.so.4.1: cannot open shared object
  file`): from the AppImage that shouldn't happen (the package carries versioned sonames;
  `package_check.py` verifies it); from the zip, install the missing distro package.
- **It stutters a lot in one area**: the first visits compile pipelines; the cache
  (`<root>/tlou-user`) makes later visits faster. `FPS = 30` evens the motion out.
- **Little VRAM**: `GOW3_LOW_VRAM=auto` handles it; force it with `GOW3_LOW_VRAM=1`.

## Development (this repository)

- Build: `gow3_pc/build.sh` (produces `gow3_pc/out/gow3-probe` and `out/gpu/libgow3gpu.so`);
  `gow3_pc/build.sh --test` runs the unit tests.
- Run against a game copy: `TLOU_GAME=<folder> python3 tools/probe_tlou.py --boot`
  (or the launcher).
- Package: `tools/package_linux.sh` (the zip) and
  `gow3_pc/packaging/appimage/build_appimage.sh` (the AppImage, which also re-runs the zip
  and the self-containment check `gow3_pc/packaging/appimage/needed_check.py`).
- Design notes, tasks and the verification history live next to this file (`roadmap-es.md`,
  `task-NN-es.md`); the frame-rate/speed investigation is in `BUG-SPEED.md`.
