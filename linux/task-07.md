# Task 7 — Linux Player Package: `tools/package_linux.sh` (Phase 2)

> **Status: COMPLETED AND VERIFIED.** `tools/package_linux.sh` builds
> `dist/TLOU-Remastered-linux/` and `dist/TLOU-Remastered-linux.zip` — a folder that runs on an
> x86-64 Linux PC with a Vulkan GPU and the distro's libraries, mirroring
> `tools/package_windows.sh` (same layout, same `tools/package_check.py` gate, no game files,
> no MSYS2). Verified on Arch Linux by running the packaged runtime and the packaged launcher
> from a fresh extraction of the shipped zip: `--check-imports` with the real `CUSA00552`
> 01.00 + 01.11 update completes with `Imports: 0 missing`, exit 0, and the Tkinter launcher
> opens a real window and closes cleanly. The one thing this machine cannot verify — a real
> `--boot` — is blocked by the CPU (no MOVBE; see Risks), exactly as Task 5 predicted, and is
> reproduced identically from the unstripped repository runtime with a byte-identical guest
> image, proving the fault is not introduced by the package.

## The change

Three new files (nothing existing was changed except two platform-guard edits in the shared
launcher files, documented below):

1. **`tools/package_linux.sh`** — the Linux counterpart of `tools/package_windows.sh`.
   It verifies the build inputs (the runtime from Task 1, `iced_x86` in `gow3_pc/out/pydeps`,
   the bundling Python with `tkinter` and `PIL`), stages the package, strips the binaries,
   normalizes the runtime's `RUNPATH` with `patchelf` (the build leaves this machine's absolute
   `out/gpu` path in it; `package_check.py` would reject it), copies only the non-system
   shared libraries `ldd` reports (in practice just `libgow3gpu.so`; the distro's
   `libstdc++`/SDL3/Vulkan/loader/codecs are documented, not copied), compiles `iced_x86` to
   bytecode-only (its generated sources trip AV false positives), bundles the Python
   (interpreter, standard library, `libpython`, `PIL`) with `tools/copy_python_linux.py`,
   gathers the third-party licenses, and runs `tools/package_check.py`, which both rejects
   game files/machine paths and produces the zip.
2. **`tools/copy_python_linux.py`** — the POSIX counterpart of `tools/copy_python.py`:
   copies the running interpreter to `python/bin/python3`, its linked `libpython`, the
   standard library (tests and other non-runnable pieces skipped), `PIL`/Pillow from
   `site-packages`, and refuses to proceed without the `lib-dynload/_tkinter` extension
   (the launcher is Tkinter).
3. **`gow3_pc/packaging/linux/README-player.txt`** — the player-facing README (PT/EN) shipped
   as `README.txt`: no game files included, the distro packages needed (Arch line included),
   how to start the launcher and select the game folder, the 1.11 update convention
   (`<name>-patch` beside the game), the 30 FPS limit tip, where logs go, and the license.

Two shared files got the roadmap's platform guards (Windows behavior byte-identical):

- **`launcher/tlou_launcher.pyw`** — seven guards, all `sys.platform == "win32"` (or `!= "win32"`):
  `SetCurrentProcessExplicitAppUserModelID` (Windows taskbar identity), `game_running()`
  (`tasklist` vs `pgrep -f gow3-probe`), `recycle()` (Recycle Bin vs deleting the regenerable
  shader/pipeline caches), the install check (`gow3-probe.exe` vs `gow3-probe`), the
  game-launch `subprocess.Popen` (`creationflags=0x08000000` is Windows-only and POSIX
  refuses a nonzero value), and `open_log()` (`os.startfile` vs `xdg-open`).
- **`launcher/updater.py`** — the release asset and the "what a package must hold" list are
  now per-platform (`TLOU-Remastered-linux.zip`, `python/bin/python3`, `gow3-probe`), and
  `start_apply()` launches the new version's installer with the bundled `python3` instead of
  `pythonw.exe` plus Windows `DETACHED_PROCESS`/`CREATE_NEW_PROCESS_GROUP` flags.

## Verification environment (Arch Linux)

Same machine as Tasks 1–6: x86_64, Python 3.14.8 (system, with Tkinter 8.6.16 and Pillow
12.3.0), AMD FX-8350 (Piledriver — no MOVBE; see Risks), NVIDIA GTX 1060, `patchelf` 0.19
(installed for this task: `sudo pacman -S patchelf`). Runtime built by Task 1. Real game:
`CUSA00552-1.00` + `CUSA00552-patch-1.11` through the workspace symlinks (as in Task 5,
`TLOU_UPDATE` points at the update folder).

## The package

`dist/TLOU-Remastered-linux.zip`: 876 files, 54 MiB unpacked, 17 MiB zipped (level 9).

```
TLOU-Remastered-linux/
  gow3_pc/out/gow3-probe          runtime (stripped, RUNPATH $ORIGIN/gpu)
  gow3_pc/out/gpu/libgow3gpu.so   video core (stripped; the only non-system library shipped)
  gow3_pc/out/pydeps/iced_x86     red-zone/ISA tool, bytecode-only (no .py sources)
  gow3_pc/scripts/                the preparation scripts (prepare, link, patches, red zone)
  python/                         Python 3.14.8 + Tkinter + Pillow (RUNPATH $ORIGIN/../lib)
  launcher/                       tlou_launcher.pyw, updater.py
  tools/                          probe_tlou.py, apply_update.py   (no run_game.py: it drives
                                   the Windows tasklist flow and is not used by the Linux path)
  licenses/                       the third-party licenses + THIRD-PARTY.txt, which lists the
                                   distro shared libraries the runtime links and their owners
  README.txt  LICENSE.txt  VERSION
```

The distro libraries are documented, not copied: `THIRD-PARTY.txt` lists every shared object
`ldd` reports under `/lib`, `/lib64`, `/usr/lib`, `/usr/lib64` with its owning Arch package,
and `README.txt` gives the install line (`sudo pacman -S sdl3 ffmpeg fmt zydis xxhash libx11
vulkan-icd-loader tcl tk` plus a Vulkan driver). Bundling a glibc-linked library set would
leak this machine's paths and break across distros; the Windows package ships MSYS2 toolchain
DLLs instead because those are not a distro's there. Tcl/Tk stay a distro dependency (the
`tk` package), exactly like a normal system Python; the bundle ships only Python's own
`_tkinter` extension.

## Verification

1. **Gate + zip from the script itself.** `bash tools/package_linux.sh --no-build` runs
   `tools/package_check.py` on the staged folder — the same checker the Windows package uses,
   which rejects game files (`.psarc`/`.sprx`/.../`eboot.bin`/...), forbidden folders and any
   byte of this machine's paths — and only then writes the zip. Result: `876 files,
   54 MiB -> 17 MiB`. Re-running the checker on the shipped folder independently reproduces
   the identical file list.
2. **`--check-imports` from a fresh extraction, with the packaged Python.** Unzip the shipped
   zip to a clean folder and run the packaged interpreter:
   `GOW3_CPU_CHECK=0 TLOU_UPDATE=<.../CUSA00552-patch-1.11> ./python/bin/python3
   tools/probe_tlou.py --game <CUSA00552>` → the packaged runtime prepares the game, applies
   the update overlay (46 files) and all game patches, and reports `Imports: 0 missing`,
   `exit_code: 0` (1.6 s). This is the same verification level Tasks 5 and 6 established —
   check-imports never decodes or runs guest code, which is why `GOW3_CPU_CHECK=0` is safe.
3. **The launcher runs on the packaged Python.** `./python/bin/python3
   launcher/tlou_launcher.pyw --close-after 5` from the extracted folder opens the real
   Tkinter window (X11, via the distro's Tcl/Tk 8.6.16) and closes cleanly: 5.6 s wall, exit
   0, no traceback. The same window creation was verified under `env -i` (empty
   environment) so nothing outside the package is needed except the display.
4. **The bundled Python is relocatable.** `env -i ./python/bin/python3 -c 'import tkinter,
   PIL'` from the extracted folder: `tkinter` 8.6, `PIL` 12.3.0, standard library resolves
   from `python/lib/python3.14` (the interpreter's `RUNPATH` is `$ORIGIN/../lib`, patched by
   `patchelf`; a copy to a foreign prefix imports everything and creates a `Tk()` window).
5. **No build-machine residue.** `patchelf --print-rpath`: `gow3-probe` → `$ORIGIN/gpu`,
   `python/bin/python3` → `$ORIGIN/../lib`. `package_check.py` scans every launcher/tool/
   `gow3_pc` file for this machine's paths (both slash forms and the repository path) and
   finds none. Both binaries are `strip --strip-debug`'d; `gpu/` holds only
   `libgow3gpu.so`.
6. **`iced_x86` ships as bytecode only.** 0 `.py` files, 40 `.pyc` under
   `gow3_pc/out/pydeps/iced_x86` (the generated sources are the known AV false-positive
   trigger), and the packaged runtime's red-zone/ISA stage still runs with it: building the
   table reports `154 instructions replaced (movbe_load 20, movbe_store 134)`.

## Technical decisions

1. **`patchelf --set-rpath` for the RUNPATH.** Task 1's build links the runtime with
   `-Wl,-rpath,'$ORIGIN/gpu' -Wl,-rpath,"$PWD/out/gpu"` (the absolute entry so a fresh build
   runs in place). A shipped package must not carry `$PWD`, and `package_check.py` rejects
   it, so the script replaces it with the lone `$ORIGIN/gpu` (`--set-rpath`, which also
   works where only `RPATH`/`RUNPATH` differ). `patchelf` is therefore a build dependency
   (documented in the script's error message: `sudo pacman -S patchelf`).
2. **System libraries documented, not copied.** The copy loop takes whatever `ldd` reports
   that is not under `/lib`, `/lib64`, `/usr/lib`, `/usr/lib64` or `/usr/local/lib` (a missing
   library fails the build loudly). On a normal Arch system that is exactly one file:
   `libgow3gpu.so`. The rest of `ldd`'s output goes to `THIRD-PARTY.txt` with the owning
   `pacman -Qo` package. This keeps the package small (17 MiB vs 233 MiB when the loop
   once copied the system libraries) and honest about what the player must install.
3. **Bytecode-only `iced_x86`.** Its generated `Code.py` (a 2 MB instruction table) is flagged
   by several AV vendors as a false positive; the package compiles it with `compileall -b`
   and deletes the `.py` sources and `.pyi` stubs, so only the `.pyc` and the native
   `_iced_x86_py` extension ship.
4. **`run_game.py` is not staged.** It wraps the Windows `tasklist`/`taskkill` flow and the
   Windows `gow3-probe.exe`; the Linux launch path is `probe_tlou.py` (guarded in Tasks
   5–6) plus the launcher, so the package stages only what the Linux path runs.
5. **Launcher guards follow the established pattern.** Every edit is the roadmap-mandated
   `sys.platform == "win32"` guard (or its negation); the pre-existing `try/except
   AttributeError` around the `MapVirtualKeyW` calls already made the key-name helpers
   Linux-safe and was left untouched. The POSIX `recycle()` deletes only the player's
   regenerable shader/pipeline caches (there is no Recycle Bin), and the POSIX
   `game_running()` matches `gow3-probe` through `pgrep -f`.

## Windows compatibility guarantee (proof)

| Change | Effect on Windows |
|---|---|
| `tools/package_linux.sh`, `tools/copy_python_linux.py`, `gow3_pc/packaging/linux/README-player.txt` | New files; nothing Windows reads or executes. `tools/package_windows.sh`, `tools/copy_python.py` and `tools/package_check.py` are untouched. |
| `launcher/tlou_launcher.pyw` (7 guards) | Every guard's `win32` branch contains the original statements textually unchanged (in `game_running()` the two original statements are only re-indented under the guard, as in Task 6); on any Windows Python (`sys.platform == "win32"`) the code runs the same calls, in the same order, with the same arguments — `creationflags=0x08000000` included. The non-`win32` branches are dead code on Windows. |
| `launcher/updater.py` (2 blocks) | On Windows `ASSET`/`REQUIRED` take the exact former constant values (same strings, same tuple) and `start_apply()` uses the same `pythonw.exe` and the same `DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP` flags. |

Net: the Windows package (`package_windows.sh`), the Windows runtime, and the Windows
launcher behavior are byte-for-byte what they were before this task.

## Risks / observations for the following tasks

- **A real `--boot` cannot be verified on this machine — by the CPU, not by the package.**
  With `GOW3_CPU_CHECK=0` the packaged runtime (and the repository's unstripped one) gets
  through the whole boot — Vulkan, PlayGo (64 chunks), all the way to the `boot1` scene
  transition — and then faults: `Guest fault (signal 4) at guest offset 0xcafaa3`,
  deterministically, before the first frame. The same command run from the repository
  (original binary, original `RUNPATH`, repository user directory) produces the identical
  fault at the identical guest offset from a byte-identical guest image
  (`boot-linked.bin` SHA-256 match), so the package changes nothing here. The cause is the
  host CPU: the FX-8350 (Piledriver) lacks MOVBE (the runtime's own CPU check names it:
  `STOP: this CPU lacks instruction sets the game's code uses: MOVBE`) and the 1.11 guest
  code reaches instructions this core cannot decode. The red-zone/ISA table is not the
  trigger: the fault is identical with the table disabled (`GOW3_RED_ZONE_REWRITE=0
  TLOU_ISA_REWRITE=off`), and it emulates only BMI1/LZCNT/MOVBE/F16C. Task 11 (real boot)
  needs a MOVBE-capable, current-generation CPU (Intel Haswell or newer, any AMD Ryzen) or
  a POSIX ISA-emulation path in the loader; the packaging itself is not the blocker.
- **`tools/package_check.py`'s `PRIVATE` list is per-builder.** It hardcodes this machine's
  user name and repository path (the Windows script has the same property). A second builder
  must add their own before the checker can prove their zip clean; the check's game-file and
  forbidden-folder rules are machine-independent.
- **`apply_update.py` (staged, Linux path) was not exercised end-to-end.** It is shipped and
  its imports resolve on the packaged Python, but the full self-update cycle (download the
  new release zip, stage it, relaunch the installer with `--wait-pid`) needs a second
  published release to test against; Task 18 (release process) should run it once.
- **Tcl/Tk stay a distro dependency.** The bundled `_tkinter` extension needs the distro's
  `libtcl`/`libtk` and their data; `README.txt` lists them. If a player's distro splits them
  differently (Debian: `python3-tk` pulls them in; here `tcl tk`), the launcher's
  `copy_python_linux.py` refusal message names the Debian/Fedora equivalents.
- **Verification artifacts are gitignored.** The runs wrote to `analysis/tlou` /
  `analysis/tlou-user` (covered by `/analysis/*`); `git status` shows only this task's files:
  `tools/package_linux.sh`, `tools/copy_python_linux.py`,
  `gow3_pc/packaging/linux/README-player.txt`, `launcher/tlou_launcher.pyw` and
  `launcher/updater.py` modified (plus the pre-existing `fsr-vulkan` submodule `-dirty` from
  Task 1's patches, and an untracked `.idea/` IDE folder that predates this task).
