# Task 1 — Base Build on Linux (Phase 1)

> **Status: COMPLETED AND VERIFIED.** `gow3_pc/build.sh` produces `out/gow3-probe` and
> `out/gpu/libgow3gpu.so` on Arch Linux x86-64, also from an empty `out/` folder
> (clean rebuild). The runtime boots, creates a real Vulkan device and passes the
> command submission + readback smoke test.

## Verification environment (Arch Linux)

| Component | Version |
|---|---|
| Kernel / architecture | x86_64 (8 cores, 31 GiB RAM) |
| Runtime C compiler (`CC`) | GCC 16.2.1 (`cc`) |
| GPU library C++ compiler | GCC 16.2.1 (CMake, `RelWithDebInfo`, LTO ON, PGO off) |
| CMake | 4.4.4 |
| Ninja | 1.13.2 |
| pkg-config | pkgconf 3.0.7 |
| Vulkan | instance 1.4.363; GPU: NVIDIA GeForce GTX 1060 3GB (driver 580.178.4.0, Vulkan 1.4.312) |
| SDL3 | 3.4.18 |
| FFmpeg (libav*) | 63.1.102 / 61.1.102 / 10.1.102 (+libavformat, libswresample) |

## Required Arch dependencies (verified, all already installed)

```
base-devel            # gcc/cc, make, binutils, binutils
cmake ninja pkgconf
vulkan-headers vulkan-icd-loader     # find_package(Vulkan): VulkanHeaders + VulkanLoader
sdl3                              # pkg-config sdl3 (windowing, gamepad, audio)
ffmpeg                            # pkg-config libavformat libavcodec libavutil libswscale libswresample
boost                             # find_package(Boost) (headers only, BOOST_ASIO_STANDALONE)
fmt                               # find_package(fmt)
robin-map                         # find_package(tsl-robin-map)
xxhash                            # pkg-config libxxhash
vulkan-memory-allocator           # find_package(VulkanMemoryAllocator); a header-only fallback exists
glslang                           # find_program(glslang): generates the host shaders -> headers
zydis                             # find_package(Zydis)
libx11                            # pkg-config x11 (non-WIN32 only)
```

Plus the submodules (initialized with `git submodule update --init --recursive`):
`gpu/third_party/fsr-vulkan` (with the `gpu/patches/fsr-vulkan` patches applied by
`build.sh`, as on Windows), `imgui`, `magic_enum`, `miniz`, `xbyak` and
`third_party/LibAtrac9`. **`sirit` is not a submodule: it is versioned directly in the
repository** (`gpu/third_party/sirit`, with its SPIRV-Headers under `externals/`), so
neither `spirv-cross` nor `spirv-headers` are required packages. `libxcb`/`wayland` are
not used by the build (SDL3 uses them at runtime if Wayland is preferred).

## Generated artifacts

```
gow3_pc/out/gow3-probe              1.7 MB   runtime executable (NEEDED: libgow3gpu.so,
                                               libvulkan, libSDL3, libav*, RUNPATH $ORIGIN/gpu)
gow3_pc/out/gpu/libgow3gpu.so      170 MB   GPU library (shadPS4 video core, LTO)
gow3_pc/out/gow3-gpu-capabilities  17 KB    GPU check for live_resolution=auto
```

`ldd out/gow3-probe`: all dependencies resolved.

## Verification (beyond "they were generated")

1. `./out/gow3-probe` with no arguments: prints the usage and exits 0 (the CLI works).
2. `./out/gow3-gpu-capabilities`: detects the GPU →
   `GPU scene scaling: NVIDIA GeForce GTX 1060 3GB supports live presets`.
3. `./out/gow3-probe --vulkan-only`:
   `Vulkan: NVIDIA GeForce GTX 1060 3GB; command submission + 4096-byte readback PASS`
   (creates the real device, submits commands and reads back 4096 B).

## Problems found and technical decisions

The core was multi-platform as the roadmap says, but the first real build on Linux
(with GCC, which is stricter than MSYS2's Clang) exposed 8 defects. **All changes are
additive and platform-guarded; the Windows code was not touched** (compatibility
details at the end).

### 1. `GetCurrentThreadId()` undefined on POSIX (GPU library)
`liverpool.cpp`, `vk_scheduler.cpp`, `vk_draw_pipe.h` and `gow3_profiler.cpp` call
`GetCurrentThreadId()`. On Windows the shim `gpu/shim/gow3_win_compat.h` (force-included
only in `if (WIN32)`) declares it and defines `gettid()` in terms of it. On POSIX the
situation is reversed: `gettid()` is native (glibc ≥ 2.30) and `GetCurrentThreadId` does
not exist.
**Fix:** `gow3_win_compat.h` gains an `#else` with
`static inline unsigned long GetCurrentThreadId(void) { return (unsigned long)gettid(); }`
and `CMakeLists.txt` force-includes it in the `else()` (POSIX) branch as well. Call sites
keep the Windows spelling on both platforms (established pattern: `gow3_threads.h` and
`gow3_write_log.cpp` use `gettid()` on POSIX).

### 2. `std::min` with different types: LLP64 vs LP64 (`vk_rasterizer.cpp:6211`)
The `1_MB` UDL (types.h) returns `unsigned long long`. On Windows (LLP64)
`VAddr = uintptr_t = unsigned long long` (same type); on glibc (LP64) it is
`unsigned long` → `std::min` has no matching overload and the build fails.
**Fix:** `std::min(VAddr(window + 1_MB), end)`. On Windows the cast is to its own type
(no-op): same overload, same codegen. It is **the only change in shared code** (see the
compatibility section).

### 3-4. GCC diagnostics that Clang does not emit (or not as errors) in the C runtime
The runtime compiles with `-Wall -Wextra -Werror`; GCC 16 proves things Clang/MSYS2 do not:

- `runtime_file.c:143` and `:302-303` — `-Wformat-truncation`: GCC proves the worst case
  (511-byte root + `/` + 1-2 KiB name) exceeds `host[1100]`/`layer[1100]`.
  `snprintf` already truncates safely and it behaved the same on Windows (Clang does not
  flag it).
  **Fix:** `#pragma GCC diagnostic push/ignored "-Wformat-truncation"/pop` around the two
  lines. The pragma generates no code and Clang ignores it without warning (verified with
  both compilers), so the Windows binary stays identical.
- `runtime_hud.c:156` — `-Warray-bounds`: the `runtime_hud_quad` scan **deliberately**
  walks 1024 B past the `anchor` local to find the registers the stub saved in the
  caller's frame (documented in the function's comment). Enlarging the buffer would break
  the search (the pattern falls outside the range).
  **Fix:** the same pragma idiom (`-Warray-bounds`) around the loop.
- `runtime_kernel.c:50` — `-Wunused-variable`: `native_process_start` is only written and
  read inside `#ifdef _WIN32` blocks (the `GOW3_NATIVE_TSC` diagnostic).
  **Fix:** `#ifdef _WIN32 / #else` guard on the declaration; on POSIX it carries
  `__attribute__((unused))`. On Windows the declaration keeps its original text and
  position.
- `probe.c:1080` — `-Wunused-but-set-variable`: `--red-zone` is parsed in shared code,
  but the red zone is Windows-only (loading and applying are inside `#ifdef _WIN32`;
  `red_zone.h` says so explicitly).
  **Fix:** an `#else` branch that accepts the option (same `--help`/CLI on both OSes) and
  prints `Red zone: Windows-only protection ignored on POSIX` instead of silently
  ignoring it.

### 5. Missing includes on POSIX (`runtime_services.c`)
`_exit` and `mkdir` were used undeclared in the POSIX branch (on Windows they came from
`<stdlib.h>`/`<direct.h>` via the Win32 includes). Cascade: with `_exit` implicitly
declared, GCC assumed it returns and flagged "control reaches end" in `load_exec`.
**Fix:** the `#else` branch adds `<sys/stat.h>`, `<sys/types.h>` and `<unistd.h>`.

### 6. Undefined symbols in the final link (POSIX)
`libgow3gpu.so` (SHARED on POSIX, `--allow-shlib-undefined` in CMake) references 3
symbols that resolved differently on Windows:

- `__ImageBase` — defined by the **Windows CRT** (ucrtbase); the GPU code
  (`buffer_cache.cpp`, `vk_scheduler.cpp`) declares `extern "C" char __ImageBase;` and
  uses `&__ImageBase` as an anchor inside the loader image. On Linux nothing defines it.
  **Fix:** `probe.c` defines it in a `#ifndef _WIN32` block (on Windows the CRT already
  provides it; defining it there would clash).
- `runtime_exec_watch_retarget` / `runtime_exec_watch_set_reads` — defined in `probe.c`
  inside the big `#ifdef _WIN32` block (they use the Win32 debug registers + exception
  handlers); the GPU only calls them from diagnostics gated by `GOW3_SRT_WATCH`,
  `GOW3_TRACE_WATCH_NODE` and `GOW3_RESULT_WATCH` (Windows debugging features).
  **Fix:** no-op definitions in `#ifndef _WIN32` that print a one-time notice if they are
  ever invoked.

## Windows compatibility guarantee (proof)

For each change, what happens in the Windows branch (MSYS2 Clang, LLP64):

| File | Change | Effect on Windows |
|---|---|---|
| `gpu/shim/gow3_win_compat.h` | New POSIX `#else` + updated comment | The `#ifdef _WIN32` block is textually identical; the `#else` does not compile |
| `gpu/CMakeLists.txt` | 2 lines in the `else()` branch | The `if (WIN32)` branch is untouched; the Windows force-include already existed |
| `gpu/.../vk_rasterizer.cpp` | `std::min(VAddr(window + 1_MB), end)` | `VAddr(…)` = cast to `unsigned long long`, its own type: no-op, same overload and codegen |
| `src/runtime_services.c` | 3 includes in `#else` | Not compiled on Windows |
| `src/runtime_kernel.c` | Declaration under `#ifdef _WIN32` + `#else` with `unused` | On Windows: declaration text and position unchanged (identical BSS) |
| `src/probe.c` | `#ifndef _WIN32` block + red-zone `#else` branch | Not compiled on Windows |
| `src/runtime_file.c`, `src/runtime_hud.c` | `#pragma GCC diagnostic` pairs | Pragmas generate no code; Clang ignores them without warnings (verified by compiling a test TU with `clang -Werror` and `gcc -Werror`) |

The only change in shared code is the cast in `vk_rasterizer.cpp`, which is a
conversion to the identical type under LLP64 (it cannot be proven byte-for-byte without
a Windows machine; the reasoning above is the proof). Note: the `fsr-vulkan` submodule
shows `-dirty` in `git status` because `build.sh` applies the project's patches to its
working tree: pre-existing behavior, identical on Windows.

Benign CMake 4.x warnings (no effect on the build): `CMP0167` (FindBoost removed),
`miniz` deprecations (`<3.10`, `CMP0077`) and `sirit` (`CMP0218`).

## Risks / observations for the following tasks

- **PGO off** (there is no `pgo/tlou.profdata` in the repo): the build is plain; on
  Windows the profile gives ~10% extra. LTO is ON.
- The `--vulkan-only` smoke test passes on a GTX 1060 (pre-Turing, 3 GB VRAM): real
  game verification (Tasks 11/12) is where the GPU will matter.
- Task 3 (`build.sh --test`): the test binaries compile with the same GCC `-Werror`;
  if a test compiles shared code that has not been touched yet, new diagnostics of the
  same family (format-truncation/array-bounds) may appear.
- Tasks 5/6 (`tools/probe_tlou.py`): the binary name is already `gow3-probe` on POSIX
  (no `.exe`); the `sys.platform == "win32"` guard for the MSYS2 PATH is pending.
