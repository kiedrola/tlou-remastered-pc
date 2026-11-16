# Task 3 — Unit Tests on Linux (Phase 1)

> **Status: COMPLETED AND VERIFIED.** `gow3_pc/build.sh --test` compiles all ten unit tests
> with GCC's `-Wall -Wextra -Werror` and **every test passes** on Arch Linux x86-64:
> pad (threads/SDL), runtime (threads, memory, mutexes, resolver), reader-writer lock, file
> mods, semaphores, AppContent, MP3, video decoding, services and HTTP. The full command
> exits 0.

## Verification environment (Arch Linux)

Same machine as Tasks 1-2: x86_64, GCC 16.2.1 (`cc`), CMake 4.4.4, Ninja 1.13.2,
pkgconf 3.0.7, Vulkan 1.4.363 (NVIDIA GTX 1060 3GB), SDL3 3.4.18, FFmpeg 9.0.2.
The tests run headless: `pad-test` uses `SDL_VIDEODRIVER=dummy` and an SDL virtual
joystick, so no display and no physical controller are needed.

## Test results (all 10, from the `--test` branch, `build.sh:108-130`)

| Binary | Source | Area | Result |
|---|---|---|---|
| `out/pad-test` | `tests/test_pad.c` | pad ABI, staged left/right clicks, L1 chord, SDL touch coordinates, overlay capture (SDL3 gamepad + virtual touchpad) | `PASS: pad ABI, staged left/right clicks, L1 chord, SDL touch coordinates, overlay capture` |
| `out/runtime-test` | `tests/test_runtime.c` + all `src/runtime*.c` | callback lifecycle, guard ABI, mutex errors, shared direct memory, memory primitives, resolver scope | `PASS: callback lifecycle, guard ABI, mutex errors, shared direct memory, memory primitives, resolver scope` |
| `out/rwlock-rp-test` | `tests/test_rwlock_rp.c` | readers nest behind a queued writer (the winpthreads deadlock case); deadlocks turned into failures via `alarm()` | `reader-preferring rwlock tests passed` |
| `out/file-mods-test` | `tests/test_file_mods.c` | guest mod files: reads, stat, merged listing, readonly assets, writable saves | `... PASS` |
| `out/sema-test` | `tests/test_sema.c` + all `src/runtime*.c` | `libSceSema` contracts (create/attr/wait/cancel) | `PASS: semaphore contracts` |
| `out/content-test` | `tests/test_content.c` + `src/runtime_content.c` | AppContent lifecycle, profile and output boundaries | `PASS: AppContent lifecycle, profile and output boundaries` |
| `out/mp3-test` | `tests/test_mp3.c` + `src/runtime_mp3.c` + LibAtrac9 | MP3 through `libSceAjm`: header/gapless parsing, decoding jobs (`tests/data/sine.mp3`) | `mp3 test: ok` |
| `out/videodec-test` | `tests/test_videodec.c` | H.264 access units to NV12 pictures via FFmpeg (`tests/data/tiny.h264`) | `videodec test: ok` |
| `out/services-test` | `tests/test_services.c` | dialogs (progress bars run until closed), PlayGo, trophies | `services test: ok` |
| `out/http-test` | `tests/test_http.c` | `libSceHttp` requests, `GOW3_HTTP_FILES` file serving | `http test: ok` |

The test binaries are always recompiled by the `--test` branch (they are not ninja-cached),
so the flow is self-contained and idempotent. After the fix, the rebuilt `out/gow3-probe`
and `out/gow3-gpu-capabilities` still pass the Task 1 smoke tests
(`--vulkan-only`: `command submission + 4096-byte readback PASS`).

## Problems found and technical decisions

The `--test` branch is POSIX-only (the Windows branch of `build.sh` ends with `exit 0`
before reaching it) and, per the roadmap's known limitations, it had **never been run on
a real Linux machine**: the first run failed, and the second (after the first fix) failed
again. Three distinct defects, all in test link/compile, none in test logic — the asserts
themselves pass untouched.

### 1. `pad-test`: undefined `sincosf@@GLIBC_2.2.5` + missing test stubs
First symptom: `ld: ... referencia sin definir al símbolo «sincosf@@GLIBC_2.2.5»` plus
`libm.so.6: DSO missing from command line`.

- **`sincosf`:** `runtime_pad.c:139-141` calls `sinf(x)` and `cosf(x)` with the *same*
  argument; with `_GNU_SOURCE` and `-O2`, GCC proves this is a `sincosf` pair and emits a
  single `sincosf` call — a glibc extension that does not exist in the MSYS2 CRT, which is
  why Windows never hit it. `pad-test`'s link line had no `-lm` (the other math-using tests
  have it).
  **Fix:** `-lm` added to the `pad-test` line in `build.sh` (one token).
- **Missing stubs:** after that, the link revealed 14 more undefined symbols: the GPU
  library's controls/prompts/mouse API (`gow3gpu_controls_*`, `gow3gpu_prompts_*`,
  `gow3gpu_mouse_*`) and two runtime modules (`runtime_gamesettings_tick`,
  `runtime_mouselook_{set_stick_boost,aim_recent,camera_recent}`). `test_pad.c` follows the
  project's established unit-test pattern — it `#include`s `src/runtime_pad.c` as its own
  translation unit and stubs its dependencies (it already stubbed `runtime_lookup` and
  `gow3gpu_overlay_captures_input`; `test_services.c`/`test_videodec.c` do the same for
  theirs). The build line never linked the GPU library for `pad-test`, so those stubs were
  simply never written (the line had never run).
  **Fix:** 15 neutral stubs added to `test_pad.c` in the same one-line `(void)`-cast style
  as the existing ones (no mouse input, default sensitivity `1.0f`, no inversion, no camera
  hook, generation `0`). `gow3gpu_controls_vibration` needed no stub: its only caller is a
  static function dead-code-eliminated at `-O2` (verified by the link).

### 2. `runtime-test` / `sema-test`: three `probe.c`-only symbols
Linking the shared `libgow3gpu.so` requires *all* of its undefined `runtime_*` symbols in
the executable (unlike the Windows static library, which only pulls in referenced objects).
Three of them are defined only in `probe.c`'s `#ifndef _WIN32` block (Task 1's fix):
`__ImageBase` (loader-image anchor), `runtime_exec_watch_retarget` and
`runtime_exec_watch_set_reads` (no-ops, the real ones use Windows debug registers). Tests
deliberately do not link `probe.c` — they already stub `runtime_restart` for exactly this
reason.
**Fix:** the same three symbols stubbed in `test_runtime.c` and `test_sema.c` next to the
existing `runtime_restart` stub (a `char __ImageBase;` and two no-op functions). This is the
minimal change; moving the definitions out of `probe.c` into a runtime module would have
touched shared code for no gain.
Note: `mp3-test` was suspected of the same issue but does **not** link the GPU library in
`build.sh` (and `test_mp3.c`/`runtime_mp3.c`/`runtime_ajm.c` reference no `gow3gpu_*`
symbol): it links only LibAtrac9 + FFmpeg, matching its Windows CMake target
(`gpu/CMakeLists.txt:253-255` links `mp3-test` with `atrac9 PkgConfig::FFMPEG`, no
`gow3gpu`). No change needed.

### 3. `http-test`: implicit declaration of `rmdir`
`-Werror -Wimplicit-function-declaration`: on POSIX `rmdir` is declared in `<unistd.h>`; on
Windows it arrives through the Win32 header chain. (The test's `mkdir`/`remove` were already
covered: `mkdir` via `<sys/stat.h>` pulled in by the included `runtime_http.c`, `remove` via
`<stdio.h>`.)
**Fix:** `#ifndef _WIN32 #include <unistd.h> #endif` in `tests/test_http.c` — the project's
standard idiom for missing POSIX includes (Task 1 did the same in `runtime_services.c`).

## Windows compatibility guarantee (proof)

| File | Change | Effect on Windows |
|---|---|---|
| `gow3_pc/build.sh` | `-lm` on the `pad-test` line (`build.sh:109`) | The line is inside the `--test` block, which only exists after the Windows branch; that branch ends with `exit 0` inside the `case`, so it never reaches this line. Windows path byte-for-byte identical. |
| `gow3_pc/tests/test_pad.c` | 15 dependency stubs | The file is compiled **only** by the POSIX `--test` branch; no Windows build references it (the Windows CMake test targets use `test_pad_buffer.c`/`test_pad_map.c`, verified by grep over `CMakeLists.txt`/`build.sh`/`*.nix`/`*.py`). No Windows binary or path changes. |
| `gow3_pc/tests/test_runtime.c`, `tests/test_sema.c` | 3 probe-only stubs each | Same: compiled only by the POSIX `--test` branch; `test_runtime.c`/`test_sema.c` appear in no Windows CMake target. |
| `gow3_pc/tests/test_http.c` | `#include <unistd.h>` under `#ifndef _WIN32` | The include does not compile on Windows; even if the file were ever built there, `rmdir` remains declared by the Win32 headers as before. |

No test *logic* changed: every assert, expected value and data file is untouched; the tests
that failed before failed at **compile/link**, not at their checks.

## Risks / observations for the following tasks

- **The C++ CMake test targets are out of scope and unverified on Linux.** `gpu/CMakeLists.txt`
  defines ~30 `EXCLUDE_FROM_ALL` test executables (pipeline-cache, motion, fiber, ...), built
  manually on Windows via `ninja -C out/gpu <target>`. The ones that `target_link_libraries(...
  gow3gpu)` will hit the same "shared library needs every `runtime_*` symbol" link problem on
  POSIX that this task fixed for the C tests (they currently link no runtime objects). Task 14
  ("add platform-specific tests and run them in the `build.sh --test` flow") should decide the
  policy: keep the `--test` branch as the canonical C-test runner and extend it, or add a CMake
  `--test`-equivalent for the C++ targets.
- **The `__ImageBase` anchor is per-binary.** In the real probe it lives in `probe.c` (the
  loader image); in the tests it is a per-test-binary `char`. Nothing in the tested paths
  depends on its position, so the stub is safe; noted in case a future test exercises GPU
  image code that offsets from `&__ImageBase`.
- **GCC's `sincosf` optimization is a portability minefield for shared code.** Any new
  `sinf(x); cosf(x)` pair in a `-O2 -Werror` TU that links without `-lm` will fail the same
  way. All existing math link lines in the `--test` branch now carry `-lm`.
- **Headless testing works.** `pad-test` proved that SDL3's dummy driver + virtual joystick
  is sufficient for controller tests — Tasks 11-13 (boot/replay) will still need a real
  display/Vulkan device, but unit-level work needs no GUI.
