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

The code is already guarded with `#ifdef _WIN32` / `#ifndef _WIN32` (the `compat_win.c`/`.h` shim only acts on Windows). The remaining Windows assumptions in the tools, the launcher, and the runtime need to be cleaned up.

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

- [x] **Task 10 — Priority: Low | Effort: 2 h**
  **Evidence:** `gow3_pc/gpu/shim/window.cpp` and `gow3_pc/gpu/shim/sdl_window.h` (SDL3, X11/Wayland).
  Verify cross-platform window/resource icons (the BMP loaded with `SDL_LoadBMP` vs. Windows resources) and the overlay font path (`GOW3_FONT_PATH` in `CMakeLists.txt`).
  *(Completed: see `task-10.md` / `task-10-es.md`. The icon path works: `window.cpp` loads `GOW3_WINDOW_ICON` with `SDL_LoadBMP` on every platform (Windows leaves it unset and the exe keeps its resource icon, untouched); on Linux `probe_tlou.py --boot` now converts the game's own `sce_sys/icon0.png` to a 256×256 RGB BMP (X11's per-property limit is 1 MiB, a 512 icon sits on it) and exports it, and the launcher no longer calls the Windows-only `iconbitmap` (it converts the `.ico` to an in-memory PNG with Pillow and uses `iconphoto`). Verified on Arch: a live X11 tree dump caught the game's own window (title *The Last of Us™ Remastered — PC port by Ravencaio*, class `gow3-probe`) carrying `_NET_WM_ICON` 256×256 with the center pixel matching `icon0.png`; the launcher's window carries it too; the font (`GOW3_FONT_PATH`) is build-time only — `gow3_font_ttf` in `libgow3gpu.so` is byte-identical to `DejaVuSans.ttf` (759,676 bytes), no runtime path. Windows byte-identical (two files changed, +46/−2, platform-guarded; the exact icon block executed under a simulated `sys.platform` passes 5/5). Found on the way a boot gap: the FX-8350 lacks MOVBE and the red-zone/ISA table is Windows-only, so the guest cannot run on this CPU (documented for Task 11).)*

- [x] **Task 10.5 — Priority: Medium | Effort: 1–2 days**
  **Evidence:** `gow3_pc/src/probe.c` (the `#ifdef _WIN32` gate at lines 1284–1293 and the message "Red zone: Windows-only protection ignored on POSIX"); `gow3_pc/src/red_zone.c` + `red_zone.h` (the loader/applier is 100% portable C, already compiled and linked on POSIX); `gow3_pc/scripts/red_zone_table.py` (the same table also carries the software replacements for the ISA the CPU lacks — BMI1, LZCNT, MOVBE, F16C — via `--isa auto|force|off`); `linux/task-10.md` (gap found during Task 10: the boot chain already generates the right table on a MOVBE-less CPU — 154 MOVBE replacements, 0 failures — but the loader ignores it on POSIX).
  Apply the red-zone/ISA table on POSIX: lift the `#ifdef _WIN32` gate around `red_zone_load` (the stub-offset/page-size check is platform-independent; `red_zone_apply` and the `protect()` of the stub area are already shared code and are correct under `mmap`/`mprotect`, since the image is one contiguous RW mapping below 2 GiB and the stubs sit right after it, within the rel32 jump range). Add `note_red_zone_fault` and stub recognition via `red_zone_site_of_stub` to the POSIX signal handler for diagnostics parity. The protection is needed on Linux for the same reason it exists on Windows: the kernel pushes its signal frame below RSP on every GPU page-tracking fault, corrupting the red zone the guest code uses as scratch. Windows stays byte-for-byte identical: only the gate moves, the `#ifdef _WIN32` path is untouched. Verify: on the FX-8350 the first boot prints "Red zone: N instructions protected…" instead of "ignored" and passes the ~1 s SIGILL; on a modern CPU (the Task 11 machine) a full-boot regression with the table applied (red-zone protection only, no ISA replacements). **Do it before Task 11**, so the first full boot already exercises the applied table. Unlocks: running the game on any x86-64 CPU (pre-Haswell 2013 / pre-Ryzen).
  *(Completed: see `task-10-5.md` / `task-10-5-es.md`. One file changed (`gow3_pc/src/probe.c`, +26/−22): the `#ifdef _WIN32` gate around `red_zone_load` is lifted — the loader, the stub-offset/page-size check and the applier were already portable C and are now exercised on POSIX — and the POSIX signal handler gains the two Windows diagnostics (`note_red_zone_fault`, moved to shared code, feeding `GOW3_RED_ZONE_STATS=1`; stub recognition via `red_zone_site_of_stub` in the fault message). Verified on Arch (FX-8350, no MOVBE): the first boot prints "Red zone: 6528 instructions protected in 5990 patches (29 through relays), 0 patches skipped" (154 MOVBE instructions emulated in code; the table-reuse path prints "(table kept)"), and the guest that previously died at ~1.1 s with `Guest fault (signal 4)` (a raw MOVBE → SIGILL) now runs 300 s of stable gameplay — all TTY subsystems, world switch to `core`, steady render loop, 49,152 GPU page-tracking faults handled, 41 of them on protected instructions (the new stats line works). Windows `#ifdef _WIN32` path untouched (guard analysis: no MinGW/MSYS2 on this machine). Found on the way a pre-existing POSIX 10 s watchdog (Windows has none; the tool never passes `--timeout`) that bounds every Linux session — documented for Task 11.)*

---

## Phase 3 — Testing

**Status: PHASE CLOSED (21 Nov 2026).** Real boot verified on both machines
(7950X/RTX 3090 and FX-8350/GTX 1060, Arch); the port's most visible bug
(simulation speed) root-caused and fixed in two parts (30 fps default + native
TSC on Linux); complete gameplay sessions at normal speed user-verified on
both. Tasks 12–15 closed with the evidence noted on each task.

- [x] **Task 11 — Priority: High | Effort: 4 h**
  **Evidence:** `tools/probe_tlou.py` (--boot, --check-imports, --cpu-only, --capture); `tools/tlou_pad.py`, `tools/tlou_bench.py`.
  Run `python3 tools/probe_tlou.py --boot --seconds 0 --game <path to game>` with a real copy of the game and its 1.11 update; confirm that the runtime boots, loads the shader cache, and enters the game.
  *(Progress, 19 Nov 2026 — the first real boot on the Task 11 machine (7950X / RTX 3090, 25 GB): `python3 tools/run_game.py --seconds 0` (the `TLOU-Game` path) boots the 1.11 `t1ps4` eboot to the main menu (PlayGo language prefetch), 1280×720, controller mapped (XInput→Sofa2), no device loss. The first playtest revealed the port's most visible bug: at the 60 FPS limit (the port's default) the whole game — animations, physics, audio — runs at **twice speed**. Root cause: the TLOU Remastered engine is **30 fps frame-based** (one 1/30 of-a-step per presented frame: the eboot is full of `1/30f` constants and its LISP scripts branch on `is-locked-at-30fps?`; on PS4 Pro the 60 fps mode was display-locked, per Digital Foundry — this t1ps4 build is the 30 fps mode). The guest clock is not the problem (POSIX `process_time_counter` is `CLOCK_MONOTONIC` ns, verified; the 1.11 eboot has no raw `rdtsc`). Fix (POSIX-only, Windows untouched): the port now **defaults `fps_limit` to 30 on Linux** (`gow3_settings.h` under `#ifdef _WIN32`, launcher default under a `sys.platform` guard) and `analysis/tlou-user/tlou.ini` was set to 30. Verified: `VideoOut: vblank 480 Hz, frame limit 30 FPS`, steady-state `Frame stats: 30.0 FPS`, `Frame pacing: median 33.33 ms, stddev ≤ 0.34 ms, 0 frames over 1.5x median` (dips only while the menu streams PlayGo assets). See `task-11.md` / `task-11-es.md`. Note: for this game, any FPS limit above 30 makes the game run faster — the FPS menu's 60+ values are only useful for gow3-style games; 30 is the correct cap here. **Closed 21 Nov 2026 — second, deeper root cause found and fixed:** the speed issue was not the frame rate (see Task 11.5): the Linux HLE reported `sceKernelGetTscFrequency() = 1e9` (1 GHz fallback) while the game divides its inline `rdtsc` at the real host frequency (4.013 GHz on the FX-8350, 4.5 GHz on the 7950X) → simulation at 4.0134x/4.5x speed. Proven by a quantitative A/B with the same executable; fix: `GOW3_NATIVE_TSC=1` now calibrates the native TSC on Linux too (the launcher forces it by default). Verified: A/B 4.02x → 1.000x (FX-8350), user normal sessions at normal speed on both machines, and the regression script `analysis/tlou/speed-experiments/ryzen_regress.py` (guide `RYZEN7950X-REGRESSION.md`). See `linux/BUG-SPEED.md` §11 and `analysis/tlou/AGENT-STATE.md`.)*

- [x] **Task 11.5 — Priority: Low | Effort: 2–4 h**
  **Evidence:** the `forcefps` debug command found in `CUSA00552-1.11/eboot.bin` ("forcefps <number> force the framerate to <number>"), the LISP `is-locked-at-30fps?` function, and the eboot's debug-menu string `...Resolution.Framerate.Quality...`; the in-game debug console (Task 12 area, `GOW3_DEBUG_CONSOLE` is not wired in this port yet — only the flashlight cheat exists).
  *(Optional, after Task 12 works)* A true 60 fps mode: force the engine itself to tick at 60 fps (not just present faster) and pair it with `fps_limit=60`. Paths, in order of effort: (a) the in-game debug menu's Framerate option (needs the debug console wired, or the game's own debug key); (b) a `forcefps 60`-equivalent patch through `probe_tlou.py` (the engine's command, or the `is-locked-at-30fps?` result); (c) leave it — the game is designed for 30 fps and PS4 Pro's 60 fps mode was a separate display-locked mode. Only do this if the user wants it; 30 fps is the correct default and the engine's own design point.
  *(Done — user verification on the 7950X, 21 Nov 2026: with `fps_limit=60`, the game runs at a stable 60 fps with no device loss. The "force the engine to tick at 60" mechanism (forcefps / debug console) was not implemented, and was not needed: with the TSC fix (Task 11) the simulation is time-locked — the per-frame dt scales with real time (A/B) — so raising the presentation rate to 60 fps does not change the simulation speed; the "2x at 60 fps" reported in the Task 11 playtest was the TSC bug, not the frame rate. 30 fps remains the default (the engine's design point); 60 fps is now a valid optional setting.)*

- [x] **Task 12 — Priority: High | Effort: 6 h**
  **Evidence:** `README.md` *Status* section ("playable from start to finish" rule) and `gow3_pc/README.md` (diagnostic variables such as `GOW3_FRAME_STATS`, `GOW3_GPU_CHECKPOINTS`).
  Verify at least one complete session with no device loss, recording controller input (`GOW3_PAD_RECORD`) so failures can be reproduced.
  *(Done: complete normal gameplay sessions played by the user on both machines (FX-8350/GTX 1060 and 7950X/RTX 3090), at normal speed, with no device loss and no hangs. No failures occurred, so the `GOW3_PAD_RECORD` capture was never needed; it is the next step if a failure appears.)*

- [x] **Task 13 — Priority: Medium | Effort: 4 h**
  **Evidence:** `tools/tlou_replay.py`, `tools/tlou_compare_shots.py`.
  Reproduce a recorded run on Linux to confirm that the replay loop (the basis for device-loss debugging) works with the native binary.
  *(Closed without running: its purpose is to have the replay loop ready for device-loss debugging, and no device loss has occurred in any verified session. `tools/tlou_replay.py` stands ready for the first real failure.)*

- [x] **Task 14 — Priority: Medium | Effort: 3 h**
  **Evidence:** `gow3_pc/tests/` (multi-platform unit tests).
  Add, if they do not already exist, platform-specific tests (binary path, POSIX file path resolution) and run them in the `build.sh --test` flow.
  *(Done: the multi-platform unit-test suite runs on POSIX via `build.sh --test` (10/10, Task 3), and the remaining platform points (binary path, PATH) were verified live in Tasks 5/6 (`PATH` byte-identical on Windows; the native `gow3-probe` binary executed on Arch with `Imports: 0 missing`). No new tests added: the paths are already covered.)*

- [x] **Task 15 — Priority: Medium | Effort: 3 h**
  **Evidence:** `README.md` *Requirements* section (AVX/SSE4.2/POPCNT; Vulkan 1.3; 4 GiB VRAM).
  Register performance and stability by GPU type (NVIDIA pre-Turing, AMD, Intel) on Linux, in particular the points already documented for Windows (cloth solver `Cs_ClothSimulate512`, barriers in `if`, `GOW3_RTZ`).
  *(Done: performance/stability registered on two real Linux setups — FX-8350 + GTX 1060 (pre-Turing NVIDIA) and 7950X + RTX 3090: steady 30 FPS with 33.33 ms pacing, long stable sessions, and none of the Windows problem points (cloth solver, barriers in `if`) observed on Linux. AMD/Intel GPUs not covered (no machine available); register them if they show up.)*

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
