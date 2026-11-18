# Native Linux Port Roadmap — The Last of Us Remastered

> Project: [`ravencaio/tlou-remastered-pc`](https://github.com/ravencaio/tlou-remastered-pc) (`linux-support` branch).
> Unofficial port. Under GPL v2 license. **No game proprietary content is included or distributed** (only the runtime, launcher, and tools are provided; the user must supply their own `CUSA00552` copy).

> ⚠️ **Non-negotiable constraint: Windows must not break.** Many people play on Windows, so **every change for Linux must be additive and guarded by a platform guard** (`sys.platform == "win32"` in Python, `if (WIN32)`/`#ifdef _WIN32` in C/C++), so that the Windows branch stays byte-for-byte identical. Before assuming a change is safe, prove it: compare the exact path/binary/PATH/behavior on Windows before and after. See [Windows Compatibility Guarantee](#windows-compatibility-guarantee).

## Windows Compatibility Guarantee

**Rule:** no change for Linux may alter the behavior, binary, or paths of the Windows build. To apply a change, you must explicitly prove that on Windows the result is identical to before.

**Why Windows is already protected at the core:**

- `gow3_pc/build.sh`: the Windows branch (`case $(uname -s) in MINGW*|MSYS*`) ends with `exit 0` inside the `case`, so the POSIX code from line 50+ **never** runs on Windows. The separation is complete.
- `gow3_pc/gpu/CMakeLists.txt`: everything Windows-related is inside `if (WIN32) ... endif()`; the GPU library is `STATIC` on Windows and `SHARED` on POSIX (`GOW3GPU_KIND`).
- `gow3_pc/src/compat_win.c` / `compat_win.h`: only compiled and active with `#ifdef _WIN32`. On POSIX the runtime uses native calls (pthread, `pread`/`pwrite`, `sem_*`).
- `gow3_pc/src/probe.c`: the stack-entry boot variants (`enter_on_stack`) are in `#ifndef _WIN32` (POSIX x86-64 assembly); the Windows branch is not touched.
- Windowing via SDL3 (`gow3_pc/gpu/shim/window.cpp` / `sdl_window.h`): X11 or Wayland on POSIX, Win32 on Windows, with no shared modifiable code.

**Future modifications (mandatory):** before touching any file, confirm that the file already separates Windows/POSIX with an existing guard; if not, add a new one **without moving or rewriting** the Windows branch. Prefer `#ifndef _WIN32` / `else()` over rewriting the `WIN32` branch.

---

## Executive Summary

The port core is **already multi-platform**: the C runtime and GPU library (shadPS4 video core + shader recompiler) already compile natively on POSIX. The `gow3_pc/build.sh` script already has a non-Windows branch that produces `out/gow3-probe` and `out/gpu/libgow3gpu.so`, the code uses pthread/POSIX mutexes natively, and windowing depends on SDL3 (X11/Wayland). The only remaining platform couplings are: (1) two Windows assumptions in `tools/probe_tlou.py` (binary name and MSYS2 PATH), (2) the launcher, which today is a Windows GUI app (`.pyw` + `.exe` starters via `windres`), and (3) packaging, which only generates a Windows zip. The `linux-support` branch's job is to close those three points, add a native launcher runner and a Linux packaging script, and verify boot and full gameplay on a real Linux machine.

---

## Phase 1 — Base Build on Linux

The goal is to confirm that the POSIX branch of `build.sh` produces a functional runtime on a real Linux machine (not just on NixOS).

- [x] **Task 1 — Priority: High | Effort: 4 h**
  **Evidence:** `gow3_pc/build.sh`, lines 7–48 (`MINGW*|MSYS*` branch) vs. POSIX branch (line 50+); `gow3_pc/gpu/CMakeLists.txt` (non-Windows path, `GOW3GPU_KIND SHARED`, `pkg_check_modules(X11)` when `NOT WIN32`, `--allow-shlib-undefined`).
  Run `gow3_pc/build.sh` on a Linux distro with `pkg-config` (vulkan, sdl3, libavcodec/libavutil/libswscale), cmake, and ninja; verify that `out/gow3-probe` and `out/gpu/libgow3gpu.so` are generated.
  *(Completed: see `task-01.md` / `task-01-es.md`.)*

- [x] **Task 2 — Priority: High | Effort: 4 h**
  **Evidence:** `gow3_pc/shell.nix` (build environment) and the comment in `build.sh` lines 59–64 (automatic reactivation of `nix-shell`).
  Document and verify the list of required system dependencies (`vulkan-loader`, `vulkan-headers`, `sdl3`, `ffmpeg`, `boost`, `fmt`, `robin-map`, `xxhash`, `vulkan-memory-allocator`, `glslang`, `spirv-cross`, `xbyak`, `zydis`, `libx11`, `libxcb`, `wayland`). Compare with the MSYS2 packages in `gow3_pc/packaging/windows/README.md`.
  *(Completed: see `task-02.md` / `task-02-es.md`. Documented in `gow3_pc/packaging/linux/README.md`; `spirv-cross`/`xbyak`/`libxcb`/`wayland` are not build dependencies, and `base-devel`/`cmake`/`ninja`/`pkgconf`/`git` were missing from the list.)*

- [x] **Task 3 — Priority: Medium | Effort: 3 h**
  **Evidence:** `gow3_pc/build.sh` lines 108–130 (`--test` branch, compiles and runs unit tests on POSIX).
  Run `gow3_pc/build.sh --test` and confirm that all C/C++ tests pass on Linux (threads, memory, mutexes, MP3, video decoding, services, content).
  *(Completed: see `task-03.md` / `task-03-es.md`. All 10 tests pass; the `--test` branch had never run on Linux — three link/compile defects fixed (missing `-lm` for GCC's `sincosf`, test stubs for `pad-test`, `probe.c`-only symbols in `runtime-test`/`sema-test`, missing `<unistd.h>` in `http-test`).)*

- [x] **Task 4 — Priority: Medium | Effort: 3 h**
  **Evidence:** `gow3_pc/gpu/CMakeLists.txt` (host shader generation via `glslang`, `host_shaders`).
  Verify that host shader header generation and the GPU library compilation do not depend on anything Windows-related in the POSIX path.
  *(Completed: see `task-04.md` / `task-04-es.md`. Clean out-of-tree build of `host_shaders` + `gow3gpu` (266/266 ninja steps) on Arch with distro tools only; the 31 generated headers are byte-for-byte identical to the in-tree ones; every Win32 reference in the compiled sources is guarded with `#ifdef _WIN32` with a POSIX fallback; no code changes were needed.)*


---

## Phase 2 — Platform Abstraction (Remove Windows Couplings)

The code is already guarded with `#ifdef _WIN32` / `#ifndef _WIN32` (the `compat_win.c`/`.h` shim only acts on Windows). The remaining Windows assumptions in the tools and launcher need to be cleaned up.

- [x] **Task 5 — Priority: High | Effort: 3 h**
  **Evidence:** `tools/probe_tlou.py`, line 462 (`built = ROOT / "gow3_pc/out/gow3-probe.exe"`) and line 664 (usage in `command`).
  In `game_executable()`, choose the binary according to the OS: `gow3-probe.exe` on Windows, `gow3-probe` on POSIX.
  *(Completed: see `task-05.md` / `task-05-es.md`. One-line `sys.platform == "win32"` guard in `game_executable()`; verified live end-to-end on Arch with a real `CUSA00552` 01.00 + 01.11 update — the tool launches the native `gow3-probe`, which completes `--check-imports` with `Imports: 0 missing`.)*

- [x] **Task 6 — Priority: High | Effort: 2 h**
  **Evidence:** `tools/probe_tlou.py`, lines 552–553 (`msys = Path(os.environ.get("MSYS2_ROOT", "C:/msys64"))` and `env["PATH"] = str(msys / "clang64/bin") + ...`).
  Guard the MSYS2 PATH injection to Windows (`sys.platform == "win32"`): on Linux `C:/msys64` does not exist and the toolchain lookup would break.
  *(Completed: see `task-06.md` / `task-06-es.md`. `sys.platform == "win32"` guard around the PATH injection (the Windows runtime links against the MSYS2 CLANG64 DLLs; the native Linux one has no such dependency). Verified on Arch by executing the exact statements of the old and new file (byte-identical `PATH` on simulated Windows in three `MSYS2_ROOT`/`PATH` cases; `PATH` no longer polluted on Linux), by capturing the environment the tool builds live (untouched on Linux, real `main()` run), and by a full `--check-imports` run with the real `CUSA00552` 01.00 + 01.11 update (`Imports: 0 missing`, exit 0).)*

- [x] **Task 7 — Priority: Medium | Effort: 3 h**
  **Evidence:** `tools/package_windows.sh` (Windows only, uses `cygpath`, `ldd`/DLLs from CLANG64, Windows Python).
  Add a Linux equivalent `tools/package_linux.sh` that packages the runtime, `libgow3gpu.so`, dependent `.so` files via `ldd`, the launcher, scripts, and a bundled Python, without game files (`tools/package_check.py` already rejects build files/paths).
  *(Completed: see `task-07.md` / `task-07-es.md`. `tools/package_linux.sh` + `tools/copy_python_linux.py` + `gow3_pc/packaging/linux/README-player.txt`; the shared launcher files got only `sys.platform` guards (Windows byte-identical). `dist/TLOU-Remastered-linux.zip` (876 files, 17 MiB, runtime RUNPATH normalized with `patchelf`, distro libraries documented not copied, `iced_x86` bytecode-only) passes `tools/package_check.py`; verified on Arch from a fresh extraction of the shipped zip with the bundled Python: live `--check-imports` with the real `CUSA00552` 01.00 + 01.11 update (`Imports: 0 missing`, exit 0) and the Tkinter launcher opens a real window and closes cleanly.)*

- [x] **Task 8 — Priority: Medium | Effort: 3 h**
  **Evidence:** `launcher/build-starters.sh` (Windows only: `windres`, `-Wl,--subsystem,windows`, `-nostdlib`).
  Create the launcher's Linux entry point: a native runner (lightweight binary or executable script) that replaces `TLOU Game.exe`, and adapt `build-starters.sh` or create a `build-linux.sh` that does not use `windres`.
  *(Completed: see `task-08.md` / `task-08-es.md`. `launcher/TLOU-Launcher` + `TLOU-Game` (executable bash scripts, the Linux counterparts of `TLOU Launcher.exe` / `TLOU Game.exe`; the C starters exist to dodge antivirus heuristics on unsigned Windows binaries, which does not apply to a Linux script) + `launcher/build-linux.sh` (nothing compiled, no `windres`: executable bit + `bash -n`, called by `gow3_pc/build.sh`'s POSIX branch). Windows paths byte-identical; shared files got `sys.platform` / platform guards only (`tools/run_game.py` `game_running()`, `tools/apply_update.py` `wait_for_exit()` + post-update restart, `tools/package_linux.sh` + `tools/package_check.py` now ship and scan the starters and `run_game.py`, `launcher/updater.py` Linux `REQUIRED`, player README). Verified on Arch: the full `TLOU-Game` chain with the real `CUSA00552` 01.00 + 01.11 (starter → `run_game.py` → `probe_tlou.py --boot` → native runtime, the `tlou.ini` settings applied — resolution, language, patches — and the only failure SDL's "no available video device"), every error path (game already running, no Python, Python without Tkinter, launcher not found), the no-terminal path (output to `analysis/tlou/*-start.log`, exit code kept), `wait_for_exit` on the real topology, and a fresh extraction of the 879-file / 17.2 MiB zip running `./TLOU-Game` on the bundled Python, and the launcher window through the starters under Xvfb, `sudo pacman -S xorg-server-xvfb`).*

- [x] **Task 9 — Priority: Medium | Effort: 2 h**
  **Evidence:** `launcher/tlou_launcher.pyw` (Windows Tkinter app) and `tools/copy_python.py`.
  Ensure the Tkinter launcher runs on Linux Python (the `.pyw` must be passed with `python3`; verify the Pillow/Tkinter imports in the Linux environment).
  *(Completed: see `task-09.md` / `task-09-es.md`. The launcher runs on the Linux Python (`.pyw` via `python3`, `tkinter`/Pillow present in system and bundled Python) and now remaps keys correctly on Linux: `key_from_event` had no Linux path (pure Windows `MapVirtualKeyW`), and the new one replicates SDL3's own mapping — the game resolves `key_map` with `SDL_GetScancodeFromName`, so the launcher writes SDL scancode names built from SDL3 3.4.20's `xfree86_scancode_table2` (physical position, X11 key code − 8 / Linux input code, the table both SDL3 X11 and Wayland drivers use) with the keysym override for non-character keys and SDL's eleven fixed scancodes — four tables generated from SDL3's own source and embedded as static literals (layout-independent: a Spanish/AZERTY/US board writes the same bind for the same physical key, matching Windows). The Controller tab loads the distro's `libSDL3.so.0` (the package ships no SDL3; the runtime loads the same distro library); `open_log` survives a missing `xdg-open`. Windows byte-identical (one file changed: `launcher/tlou_launcher.pyw`, +166/−13, platform-guarded). Verified on Arch (Spanish layout): unit harness on the live X11 keycodes `0 failures` with every name resolving through the real `SDL_GetScancodeFromName`, and a two-display Xvfb end-to-end injecting real XTEST key events into a live Tk window and a live SDL3 window: 51/51 launcher==SDL3 name, `BAD: 0`.)*

- [ ] **Task 10 — Priority: Low | Effort: 2 h**
  **Evidence:** `gow3_pc/gpu/shim/window.cpp` and `gow3_pc/gpu/shim/sdl_window.h` (SDL3, X11/Wayland).
  Verify cross-platform window/resource icons (the BMP loaded with `SDL_LoadBMP` vs. Windows resources) and the overlay font path (`GOW3_FONT_PATH` in `CMakeLists.txt`).

---

## Phase 3 — Testing

- [ ] **Task 11 — Priority: High | Effort: 4 h**
  **Evidence:** `tools/probe_tlou.py` (--boot, --check-imports, --cpu-only, --capture); `tools/tlou_pad.py`, `tools/tlou_bench.py`.
  Run `python3 tools/probe_tlou.py --boot --seconds 0 --game <path to game>` with a real copy of the game and its 1.11 update; confirm that the runtime boots, loads the shader cache, and enters the game.

- [ ] **Task 12 — Priority: High | Effort: 6 h**
  **Evidence:** `README.md` *Status* section ("playable from start to finish" rule) and `gow3_pc/README.md` (diagnostic variables such as `GOW3_FRAME_STATS`, `GOW3_GPU_CHECKPOINTS`).
  Verify at least one complete session with no device loss, recording controller input (`GOW3_PAD_RECORD`) so failures can be reproduced.

- [ ] **Task 13 — Priority: Medium | Effort: 4 h**
  **Evidence:** `tools/tlou_replay.py`, `tools/tlou_compare_shots.py`.
  Reproduce a recorded run on Linux to confirm that the replay loop (the basis for device-loss debugging) works with the native binary.

- [ ] **Task 14 — Priority: Medium | Effort: 3 h**
  **Evidence:** `gow3_pc/tests/` (multi-platform unit tests).
  Add, if they do not already exist, platform-specific tests (binary path, POSIX file path resolution) and run them in the `build.sh --test` flow.

- [ ] **Task 15 — Priority: Medium | Effort: 3 h**
  **Evidence:** `README.md` *Requirements* section (AVX/SSE4.2/POPCNT; Vulkan 1.3; 4 GiB VRAM).
  Register performance and stability by GPU type (NVIDIA pre-Turing, AMD, Intel) on Linux, in particular the points already documented for Windows (cloth solver `Cs_ClothSimulate512`, barriers in `if`, `GOW3_RTZ`).

---

## Phase 4 — Linux Packaging / Distribution

- [ ] **Task 16 — Priority: High | Effort: 4 h**
  **Evidence:** `tools/package_windows.sh` and `tools/package_check.py`; `gow3_pc/packaging/windows/`.
  Define the Linux package structure (runtime + `libgow3gpu.so` + launcher + game runner + scripts + Python), with a clear entry point (`./TLOU` or similar) and no MSYS2 dependencies.

- [ ] **Task 17 — Priority: Medium | Effort: 3 h**
  **Evidence:** `README.md` *Playing* section (launch flow) and `tools/package_windows.sh` (bundled Python copy).
  Document in a `linux/README.md` the installation and boot instructions for Linux (dependencies, common `GOW3_*`/`TLOU_*` environment variables, location of `analysis/tlou-user`).

- [ ] **Task 18 — Priority: Low | Effort: 2 h**
  **Evidence:** `.github/` (possible CI workflows) and `tools/release-notes/`.
  Add, if appropriate, a CI/build workflow for Linux and release notes for the Linux package.

---

## Known Limitations and Assumptions

- **No real Linux build/run has been verified yet.** Everything above is inferred from the code; the `linux-support` branch is identical to `main` (no new commits). The estimates assume an environment with the system dependencies already available. *(Updated: Task 1 verified the build, Task 2 the system dependencies, Task 3 the unit tests (`build.sh --test`, all 10 passing), Task 4 the host shader generation and the GPU library compilation (clean out-of-tree build, no Windows dependency in the POSIX path, no code changes), Task 5 the tool launching the native binary (live `--check-imports` with a real `CUSA00552` 01.00 + 01.11 update, `Imports: 0 missing`), and Task 6 the Windows-only guard on the MSYS2 PATH injection (byte-identical `PATH` on simulated Windows; `PATH` no longer polluted on Linux), and Task 7 the Linux player package (`tools/package_linux.sh`, 876 files, 17 MiB; live `--check-imports` from a fresh extraction with the bundled Python, `Imports: 0 missing`; the Tkinter launcher opens a real window), and Task 8 the launcher's Linux entry points (`TLOU-Launcher` / `TLOU-Game` scripts + `build-linux.sh`; the full `TLOU-Game` chain with the real game up to the display, and the packaged starters from a fresh extraction) — all on Arch Linux, see `task-01.md` / `task-02.md` / `task-03.md` / `task-04.md` / `task-05.md` / `task-06.md` / `task-07.md` / `task-08.md`.)*
- **The game is not in the repository.** A legitimate `CUSA00552` copy + 1.11 update is required (see `README.md`, *Why*/*Playing* sections). Without it there is no way to run the runtime.
- **x86-64 only, by design.** The port targets Windows and Linux on x86-64; the `enter_on_stack` inline assembly in `probe.c` (entering the guest stack) has no ARM64 variant and none is planned.
- **The launcher's entry points are per-platform.** Windows: `build-starters.sh` compiles the C starters (`TLOU Launcher.exe`, `TLOU Game.exe`) with `windres`; Linux: the executable scripts `TLOU-Launcher` and `TLOU-Game` (task 8) installed by `launcher/build-linux.sh` (no compiler, no `windres`).
- **The shader cache (~250 MB, ~12,000 files) must be on an SSD.** Junctions/`mklink /J` are mentioned on Windows; on Linux this would be equivalent to a symlink or bind mount (`README.md`, *Playing* section).
- **Third-party dependencies via submodule.** FSR-Vulkan, imgui, xbyak, miniz, magic_enum, LibAtrac9 (`.gitmodules`). FSR-Vulkan requires applying the patches in `gow3_pc/patches/fsr-vulkan` (done by `build.sh`).
- **PGO/LTO with custom profile.** `pgo/tlou.profdata` is not in the repository; without it, the build is a normal one (build.sh, `build.sh` and `CMakeLists.txt` comments).

---

## Plan B

- **If the POSIX branch of `build.sh` does not compile on the target distro:** use `gow3_pc/shell.nix` (`nix-shell`) as a reproducible environment, or build the missing dependencies from source (miniz, magic_enum, xbyak are already submodules; FSR-Vulkan is patched automatically).
- **If the GUI launcher (Tkinter) is not viable on the target:** prioritize a lightweight command-line/terminal launcher or runner that reads `tlou.ini` and launches the runtime with the same environment variables (`tools/probe_tlou.py` already centralizes that boot); the graphical launcher is left as a secondary feature.
- **If window/font resource loading fails on some distro:** allow overriding paths via environment variables (`GOW3_FONT_PATH`, `GOW3_GPU_USER_DIR`) and fall back to the overlay's embedded font if the system one is not found.
- **If the game is only stable at low resolution/VRAM:** reuse the existing `GOW3_LOW_VRAM` logic and the resolution patch in `probe_tlou.py` to set a safe default Linux profile.

---

## Final Verification Checklist (game runs on Linux)

- [x] `gow3_pc/build.sh` generates `out/gow3-probe` and `out/gpu/libgow3gpu.so` without errors on the target Linux.
- [x] `gow3_pc/build.sh --test` runs all unit tests on Linux.
- [ ] `python3 tools/probe_tlou.py --boot` boots the game with a real copy of `CUSA00552` + 1.11 update.
- [ ] The game enters the main menu and a full session can be played with no Vulkan device loss.
- [ ] The window is created via SDL3 (X11 or Wayland) and the game image is displayed.
- [ ] The gamepad (DualShock 4 / Xbox) and keyboard/mouse receive input; it can be recorded and reproduced (`GOW3_PAD_RECORD` / `tlou_replay.py`).
- [ ] The Linux launcher/runner starts and applies resolution, frame cap, and language from `tlou.ini`.
- [ ] The Linux package (`package_linux.sh`) generates an executable folder/binary without game files and without depending on MSYS2.
- [ ] The documentation (`linux/README.md` and `README.md`) indicates installation and boot for Linux.
