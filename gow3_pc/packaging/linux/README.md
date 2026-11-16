# Building on Linux

The runtime builds with the distro's GCC, CMake and Ninja. Verified on **Arch Linux x86-64**
(full dependency audit in [`linux/task-02.md`](../../../linux/task-02.md)); other distros work
with the equivalent packages.

On Arch:

```bash
sudo pacman -S --needed base-devel cmake ninja pkgconf git \
  vulkan-headers vulkan-icd-loader vulkan-memory-allocator glslang \
  sdl3 ffmpeg boost fmt robin-map xxhash zydis libx11
```

`magic_enum`, `miniz`, `xbyak` and `imgui` are not system packages: they are submodules under
`gpu/third_party/`; FSR-Vulkan gets this project's patches (`gpu/patches/fsr-vulkan`) applied by
`build.sh`, as on Windows. `sirit` (the SPIR-V assembler) is versioned in the repository with
its own SPIRV-Headers, so no `spirv-*` package is needed.

Then, in the repository: `gow3_pc/build.sh` (see the [main README](../../../README.md)).
It produces `gow3_pc/out/gow3-probe` and `gow3_pc/out/gpu/libgow3gpu.so`.

## What each package provides

| Build need | Arch package | Consumed as |
|---|---|---|
| C compiler (runtime + tests) | `base-devel` (`cc`/`gcc`) | `build.sh` (`CC`, falls back to `cc`/`gcc`) |
| Build system | `cmake`, `ninja`, `pkgconf` | CMake for `gpu/`, `pkg-config` for the rest |
| Submodules + FSR-Vulkan patches | `git` | `git submodule update`, `git apply` (no `patch` needed on POSIX) |
| Vulkan loader + headers | `vulkan-icd-loader`, `vulkan-headers` | `find_package(Vulkan)`, `pkg-config vulkan` |
| GPU memory allocator (headers) | `vulkan-memory-allocator` | `find_package(VulkanMemoryAllocator)` (CONFIG package) |
| Host shader compiler | `glslang` | `find_program(glslang)` → `host_shaders` headers |
| Windowing / gamepad / audio | `sdl3` | `pkg-config sdl3` |
| Video/audio codecs | `ffmpeg` | `pkg-config libavformat libavcodec libavutil libswscale libswresample` |
| Asio (standalone, headers) | `boost` | `find_package(Boost)` |
| Formatting | `fmt` | `find_package(fmt)` |
| Flat hash map | `robin-map` | `find_package(tsl-robin-map)` |
| Hashes | `xxhash` | `pkg-config libxxhash` |
| x86 disassembler | `zydis` | `find_package(Zydis)` |
| X11 windowing glue | `libx11` | `pkg-config x11` (non-WIN32 only) |

Not required by the build (in `shell.nix` for completeness): `libxcb`, `wayland` and
`xorgproto` (SDL3's Wayland path, runtime only), `make`, `python3` (for `tools/` and the
launcher, not `build.sh`) and `spirv-cross` (only the optional asset tools in
`tools/fsr4_optimize.sh` / `tools/fetch_fsr4_assets.sh` use it).

`build.sh` checks its own prerequisites (`pkg-config` with `vulkan` and `sdl3`, `cmake`,
`ninja`) and re-enters `nix-shell shell.nix` automatically when `nix-shell` is available;
with the packages above that branch stays cold.

At runtime `gow3-probe` links `libgow3gpu.so` (next to the binary, resolved through
`$ORIGIN/gpu`), `libvulkan.so.1`, `libSDL3.so.0`, the `libav*` libraries, `libfmt`,
`libZydis`, `libxxhash` and `libX11` — plus, of course, a working Vulkan driver
(e.g. `mesa-vulkan-drivers` or the NVIDIA proprietary driver).
