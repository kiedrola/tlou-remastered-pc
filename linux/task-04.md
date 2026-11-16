# Task 4 — Host Shader Generation and GPU Library on the POSIX Path (Phase 1)

> **Status: COMPLETED AND VERIFIED.** The host shader header generation (`glslang`,
> `host_shaders` target) and the `gow3gpu` library compilation have **no Windows dependency
> in the POSIX path** — proven by a full clean out-of-tree build (266/266 ninja steps, exit 0)
> on Arch Linux using only distro tools, plus a static audit of every source compiled on
> POSIX. **No code changes were needed for this task; the Windows build is untouched by
> definition.**

## Verification environment (Arch Linux)

Same machine as Tasks 1-3: x86_64 (8 cores), GCC 16.2.1 (`cc`/`c++`), CMake 4.4.4, Ninja
1.13.2, pkgconf 3.0.7, **glslang 16.6.0** (`/usr/bin/glslang`, the distro's `glslang`
package, *not* the MSYS2 one), Vulkan 1.4.363 (NVIDIA GTX 1060 3GB), SDL3 3.4.18,
FFmpeg 9.0.2.

## Live verification (the decisive test)

1. **Fresh out-of-tree configure** (`cmake -S gpu -B /tmp/gow3-task04-verify -G Ninja
   -DCMAKE_BUILD_TYPE=RelWithDebInfo`), exit 0. The cache proves every input resolves to a
   distro component: `CMAKE_C_COMPILER=/usr/bin/cc`, `CMAKE_CXX_COMPILER=/usr/bin/c++`,
   `GLSLANG_COMPILER=/usr/bin/glslang`, `Vulkan_LIBRARY=/usr/lib/libvulkan.so`, and the
   `magic_enum`/`miniz` fallbacks to the submodules (same as Windows, CMakeLists:59-66).
2. **Full build** (`ninja host_shaders gow3gpu`): **266/266 steps, exit 0**, no LTO
   warnings and no VMA/submodule fallback messages. Everything the POSIX path compiles —
   shadPS4 video core, shader recompiler, `shadps4/common/*`, all of `shim/`, imgui,
   FSR-Vulkan (`ffx-vulkan::portable`, `ffx-vulkan::fsr4-v07-vulkan`), sirit, miniz —
   built and linked with zero Windows elements. Output: `libgow3gpu.so`, 170 MB
   (RelWithDebInfo + LTO), same size as the in-tree one.
3. **The 31 generated headers** (19 single shaders + 12 `tiling` BPP/micro-macro variants,
   exactly the list in `gpu/CMakeLists.txt:124-141`):
   - `diff -r` against the in-tree `out/gpu/include/video_core/host_shaders/`:
     **byte-for-byte identical (31/31)** — the Task 1 in-tree build used the same pure
     POSIX mechanism.
   - All 31 carry the valid SPIR-V magic `0x07230203`; the format is `#pragma once` +
     `#include <cstdint>` (inserted by `AddShaderHeaderInclude.cmake`) +
     `const uint32_t SYMBOL[]` of compiled SPIR-V words. No platform-specific content.
   - The `-D` defines are applied: `tiling_micro_8` vs `tiling_micro_128` differ
     (`BITS_PER_PIXEL`), micro vs macro differ (`IS_MACRO_TILED`).
   - **Dependency chain works on POSIX**: `touch fault_buffer_process.comp` + `ninja`
     regenerated its header (source → header tracking is via the explicit `DEPENDS` in
     `add_host_shader`).
4. **The generated .so**: `ldd` → all 15 NEEDED entries resolved (libfmt.so.12,
   libZydis.so.4.1 + libZycore, **libvulkan.so.1**, libxxhash.so.0, libSDL3.so.0, the five
   libav*, libX11.so.6, the C/C++ runtime); `nm -D` → **zero** Windows-only undefined
   dynamic symbols (no `GetProcAddress`, `CreateFileW`, `GetModuleHandle*`, `__imp_*`...).
5. **build.ninja of the fresh build**: zero references to `cygpath`/`windres`/`msys64`/
   `CLANG64`. The shader rule is exactly: `cmake -E make_directory` +
   `/usr/bin/glslang --target-env vulkan1.3 -o <out> --vn SYMBOL [-D...] <src>` +
   `cmake -DHEADER_FILE=<out> -P AddShaderHeaderInclude.cmake`.
6. **End-to-end**: `out/gow3-probe --vulkan-only` (in-tree library, which embeds the
   generated SPIR-V) → `command submission + 4096-byte readback PASS` on the real device.

## Static audit of the POSIX path

### Shader generation
- `add_host_shader` (`gpu/CMakeLists.txt:105-123`): the only external tool is the system
  `glslang` (found by `find_program(GLSLANG_COMPILER NAMES glslang glslangValidator
  REQUIRED)`, CMakeLists:94). `AddShaderHeaderInclude.cmake` is pure CMake `file(READ/
  REPLACE/WRITE)` — no platform code.
- The 11 GLSL sources are pure GLSL: their only `#include`s are the local
  `fsr/ffx_a.h`/`fsr/ffx_fsr1.h` (resolved relative to the shader file, plain C headers).
- The `host_shaders` target is a **hard dependency** of `gow3gpu`
  (`add_dependencies(gow3gpu host_shaders)`, CMakeLists:170), and `build.sh`'s POSIX
  branch builds exactly `ninja -C out/gpu gow3gpu` — so the shaders always regenerate
  before the library links, on every platform.

### GPU library sources (all of `shadps4/` + `shim/` compiled on POSIX)
Every Win32 API hit found by the scan is inside `#ifdef _WIN32` with a POSIX `#else`
fallback; nothing unguarded:

| File | Windows content | POSIX path |
|---|---|---|
| `shadps4/common/ntapi.cpp` | whole file in `#ifdef _WIN32` (`ntdll.dll` proc lookup) | empty TU; no shared caller of `Common::NtApi` |
| `shadps4/video_core/blob_pack.cpp` | `CreateFileW`/`MapViewOfFile`, `GetFileAttributesExW`, `GetSystemTimeAsFileTime`, `FindFirstFileExW`, `PrefetchVirtualMemory` (5 guarded blocks) | `stat`, `clock_gettime`, `open`/`mmap`/`munmap`, directory-iterator fallback |
| `shadps4/video_core/page_manager.cpp:99` | `GetModuleHandleW` base rebase in `GOW3_FAULT_STATS` | `#else` leaves `rip = 0` |
| `shim/gow3_profiler.cpp` | Win32 thread suspend/sampling (`SuspendThread`, `GetModuleHandleExA`, ...) | `#else` no-op sampler |
| `shadps4/video_core/renderdoc.cpp` | RenderDoc DLL interop | **not compiled at all**: excluded by the glob filter (`list(FILTER VIDEO_CORE EXCLUDE REGEX "renderdoc\\.cpp$"`, CMakeLists:150) |
| `shim/window.cpp` | `SDL_PROP_WINDOW_WIN32_HWND_POINTER` (2 guarded reads) | SDL3 X11/Wayland; `WindowSystemType` already enumerates `X11`/`Wayland`/`Headless` |

- No unguarded `.exe`/`.dll`/`C:\`/`C:/`/backslash literals anywhere in the compiled
  sources (grepped `shadps4/` + `shim/`).
- No hardcoded absolute paths in sources. The only compile-time absolute path is
  `GOW3_FONT_PATH` (CMakeLists:178): `third_party/fonts/DejaVuSans.ttf` (759 KB, in the
  repo) is **embedded into the binary** via an inline-assembly `.incbin`
  (`gow3_overlay.cpp:42-62`, `gow3_prompts.cpp:1867`) — `.rdata` on Windows, `.rodata` on
  POSIX, one design for both. The path is only needed *at build time*; at runtime the font
  lives in the .so's read-only data, so relocating the package (Tasks 7/16) needs no font
  file. Verified present in the built .so.
- `shim/gow3_win_compat.h` (force-included on **both** platforms, CMakeLists:187-190):
  the `_WIN32` side declares `GetCurrentThreadId` (kept byte-identical by Task 1); the
  POSIX side maps it to `gettid()`; the header stays free of `<windows.h>` outside the
  guard, so its macros never reach a TU on Linux.
- **Vulkan loader**: `libgow3gpu.so` carries `NEEDED libvulkan.so.1` on *both* platforms —
  on POSIX it arrives through the FFX-Vulkan targets, which link `Vulkan::Vulkan` PUBLIC
  (`fsr-vulkan/CMakeLists.txt:79,123,154,306`), exactly as on Windows. No `ws2_32`/
  `bcrypt`/`ntdll`/`winmm`/`pdh` (Windows-only libs, CMakeLists:189) exist in the POSIX
  link line — the `else()` branch links only `PkgConfig::X11` plus the shared dependency
  set.
- Submodules compiled in both trees are platform-neutral: `fsr-vulkan` sources contain
  **zero** `WIN32` guards and no `windows.h`; `sirit` likewise (and
  `SIRIT_USE_SYSTEM_SPIRV_HEADERS OFF` forces its vendored `externals/SPIRV-Headers`).
- `build.sh` POSIX branch: tools used are `cc`, `pkg-config`, `cmake`, `ninja`, `git`
  (submodules/patches), `ar`, and (transitively) `glslang` — all distro packages.
  `cygpath`, `windres`, `.rc` files and `msys2-env.sh` exist **only** in the Windows
  branch, which ends with `exit 0`.

## Problems found and technical decisions

None requiring code changes: the first real Linux build (Task 1) already removed the two
genuinely Windows-coupled points in this area (the `GetCurrentThreadId` shim and the
`VAddr`/`std::min` LLP64 cast), and the audit found no further coupling in shader
generation or GPU compilation. Two design observations were recorded below for the
packaging tasks.

## Windows compatibility guarantee (proof)

| Change in this task | Effect on Windows |
|---|---|
| **None.** Only a throwaway build in `/tmp` (since removed) and read-only inspections. | The Windows branch of `build.sh`, of `gpu/CMakeLists.txt` and of every audited source is byte-for-byte unchanged. |

## Risks / observations for the following tasks

- **Task 7 / 16 (`package_linux.sh`):** the exact NEEDED closure of `libgow3gpu.so` is the
  15 entries listed in §Live verification.4 (plus the transitive closure of libav*/SDL3,
  which stay in the distro). No font file is needed at runtime (embedded), and the
  `$ORIGIN/gpu` RUNPATH on `gow3-probe` is the intended relocation mechanism.
- **`GOW3_FONT_PATH` is build-time only** (`.incbin`): a copy of the source tree can be
  built anywhere; the baked path does not exist at runtime and is never consulted then.
  No override is required for packaging; Task 10 can drop this concern.
- **Two `add_host_shader` implementations exist**: gow3's (active,
  `gpu/CMakeLists.txt:105-123`) and the shadPS4 upstream one in
  `shadps4/video_core/host_shaders/CMakeLists.txt` (**dead** — never `add_subdirectory`'d).
  The upstream version adds `--depfile`/`DEPFILE` glslang preprocessor tracking; gow3's
  relies on the explicit `DEPENDS <src>.comp` and works today because the shaders' only
  includes are the two local `fsr/ffx_*.h`. If a future shader adds an include of a new
  file, that dependency would not be tracked — prefer the upstream variant or add the
  include to `DEPENDS` when that happens.
- **The 170 MB `.so`** (RelWithDebInfo + LTO) is expected (the Windows build is comparable);
  a release-stripped variant would help packaging (Task 16) but changes nothing functional.
- **`--allow-shlib-undefined`** (CMakeLists:176) is what lets the SHARED library carry the
  51 undefined `vk*` symbols until the loader is linked in via FFX-Vulkan on POSIX and in
  the executable on Windows; it is already inside the `if (NOT WIN32)` guard, so the
  Windows static-library path is untouched.
- **Task 10 (icons/fonts):** the embedded overlay font is confirmed working on POSIX (this
  task); only the window icon question remains there.
