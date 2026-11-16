# Tarea 1 — Build base en Linux (Fase 1)

> **Estado: COMPLETADA Y VERIFICADA.** `gow3_pc/build.sh` genera `out/gow3-probe` y
> `out/gpu/libgow3gpu.so` en Arch Linux x86-64, también desde una carpeta `out/` vacía
> (reconstrucción limpia). El runtime arranca, crea un dispositivo Vulkan real y pasa el
> smoke test de envío de comandos + readback.

## Entorno de verificación (Arch Linux)

| Componente | Versión |
|---|---|
| Kernel / arquitectura | x86_64 (8 cores, 31 GiB RAM) |
| Compilador C del runtime (`CC`) | GCC 16.2.1 (`cc`) |
| Compilador C++ de la librería GPU | GCC 16.2.1 (CMake, `RelWithDebInfo`, LTO ON, PGO off) |
| CMake | 4.4.4 |
| Ninja | 1.13.2 |
| pkg-config | pkgconf 3.0.7 |
| Vulkan | instance 1.4.363; GPU: NVIDIA GeForce GTX 1060 3GB (driver 580.178.4.0, Vulkan 1.4.312) |
| SDL3 | 3.4.18 |
| FFmpeg (libav*) | 63.1.102 / 61.1.102 / 10.1.102 (+libavformat, libswresample) |

## Dependencias de Arch necesarias (verificadas, todas ya instaladas)

```
base-devel            # gcc/cc, make, binutils, binutils
cmake ninja pkgconf
vulkan-headers vulkan-icd-loader     # find_package(Vulkan): VulkanHeaders + VulkanLoader
sdl3                              # pkg-config sdl3 (ventana, gamepad, audio)
ffmpeg                            # pkg-config libavformat libavcodec libavutil libswscale libswresample
boost                             # find_package(Boost) (solo headers, BOOST_ASIO_STANDALONE)
fmt                               # find_package(fmt)
robin-map                         # find_package(tsl-robin-map)
xxhash                            # pkg-config libxxhash
vulkan-memory-allocator           # find_package(VulkanMemoryAllocator); hay fallback header-only
glslang                           # find_program(glslang): genera los host shaders -> headers
zydis                             # find_package(Zydis)
libx11                            # pkg-config x11 (solo no-WIN32)
```

Además, los submódulos (inicializados con `git submodule update --init --recursive`):
`gpu/third_party/fsr-vulkan` (con los parches de `gpu/patches/fsr-vulkan` aplicados por
`build.sh`, igual que en Windows), `imgui`, `magic_enum`, `miniz`, `xbyak` y
`third_party/LibAtrac9`. **`sirit` no es submódulo: está versionado directamente en el
repositorio** (`gpu/third_party/sirit`, con sus SPIRV-Headers en `externals/`), por lo que
ni `spirv-cross` ni `spirv-headers` son paquetes necesarios. `libxcb`/`wayland` no se usan
en el build (SDL3 los usa en tiempo de ejecución si se prefiere Wayland).

## Artefactos generados

```
gow3_pc/out/gow3-probe              1.7 MB   ejecutable del runtime (NEEDED: libgow3gpu.so,
                                               libvulkan, libSDL3, libav*, RUNPATH $ORIGIN/gpu)
gow3_pc/out/gpu/libgow3gpu.so      170 MB   librería GPU (shadPS4 video core, LTO)
gow3_pc/out/gow3-gpu-capabilities  17 KB    check de GPU para live_resolution=auto
```

`ldd out/gow3-probe`: todas las dependencias resueltas.

## Verificación (más allá de "se generaron")

1. `./out/gow3-probe` sin argumentos: imprime el usage y sale 0 (el CLI funciona).
2. `./out/gow3-gpu-capabilities`: detecta la GPU →
   `GPU scene scaling: NVIDIA GeForce GTX 1060 3GB supports live presets`.
3. `./out/gow3-probe --vulkan-only`:
   `Vulkan: NVIDIA GeForce GTX 1060 3GB; command submission + 4096-byte readback PASS`
   (crea el dispositivo real, envía comandos y lee de vuelta 4096 B).

## Problemas encontrados y decisiones técnicas

El núcleo era multiplataforma tal y como dice el roadmap, pero el primer build real en
Linux (con GCC, que es más exigente que el Clang de MSYS2) expuso 8 defectos. **Todos los
cambios son aditivos y con guard de plataforma; el código Windows no se tocó** (detalle
de compatibilidad al final).

### 1. `GetCurrentThreadId()` sin definir en POSIX (librería GPU)
`liverpool.cpp`, `vk_scheduler.cpp`, `vk_draw_pipe.h` y `gow3_profiler.cpp` llaman a
`GetCurrentThreadId()`. En Windows existe el shim `gpu/shim/gow3_win_compat.h`
(force-incluido solo en `if (WIN32)`) que lo declara y define `gettid()` sobre él.
En POSIX ocurre al revés: `gettid()` es nativo (glibc ≥ 2.30) y `GetCurrentThreadId` no
existe.
**Fix:** `gow3_win_compat.h` gana un `#else` con
`static inline unsigned long GetCurrentThreadId(void) { return (unsigned long)gettid(); }`
y `CMakeLists.txt` fuerza el include también en la rama `else()` (POSIX). Los sitios de
llamada conservan la ortografía Windows en ambas plataformas (patrón ya establecido:
`gow3_threads.h` y `gow3_write_log.cpp` usan `gettid()` en POSIX).

### 2. `std::min` con tipos distintos: LLP64 vs LP64 (`vk_rasterizer.cpp:6211`)
El UDL `1_MB` (types.h) devuelve `unsigned long long`. En Windows (LLP64)
`VAddr = uintptr_t = unsigned long long` (mismo tipo); en glibc (LP64) es
`unsigned long` → `std::min` no tiene overload y el build falla.
**Fix:** `std::min(VAddr(window + 1_MB), end)`. En Windows el cast es a su propio tipo
(no-op): mismo overload, mismo codegen. Es **el único cambio en código compartido**
(vea la sección de compatibilidad).

### 3-4. Diagnósticos de GCC que Clang no emite (o no como error) en el runtime C
El runtime se compila con `-Wall -Wextra -Werror`; GCC 16 prueba cosas que Clang/MSYS2 no:

- `runtime_file.c:143` y `:302-303` — `-Wformat-truncation`: GCC prueba que el peor caso
  (root de 511 B + `/` + nombre de 1-2 KiB) excede `host[1100]`/`layer[1100]`.
  `snprintf` ya trunca de forma segura y así se comportaba en Windows (Clang no lo marca).
  **Fix:** `#pragma GCC diagnostic push/ignored "-Wformat-truncation"/pop` alrededor de
  las 2 líneas. El pragma no genera código y Clang lo ignora sin avisar (verificado con
  ambos compiladores), así que el binario Windows queda idéntico.
- `runtime_hud.c:156` — `-Warray-bounds`: el escaneo de `runtime_hud_quad` **cruza a
  propósito** 1024 B por encima del local `anchor` para encontrar los registros guardados
  por el stub en el frame del llamador (documentado en el comentario de la función).
  Agrandar el buffer rompería la búsqueda (el patrón queda fuera del rango).
  **Fix:** mismo idiom de pragma (`-Warray-bounds`) alrededor del bucle.
- `runtime_kernel.c:50` — `-Wunused-variable`: `native_process_start` solo se escribe y
  lee dentro de bloques `#ifdef _WIN32` (diagnóstico `GOW3_NATIVE_TSC`).
  **Fix:** guard `#ifdef _WIN32 / #else` en la declaración; en POSIX lleva
  `__attribute__((unused))`. En Windows la declaración queda en su sitio y forma
  originales.
- `probe.c:1080` — `-Wunused-but-set-variable`: `--red-zone` se parsea en código
  compartido, pero la red zone es Windows-only (carga y aplicación dentro de `#ifdef
  _WIN32`; `red_zone.h` lo dice explícitamente).
  **Fix:** rama `#else` que acepta la opción (mismo `--help`/CLI en ambos SO) e imprime
  `Red zone: Windows-only protection ignored on POSIX` en lugar de ignorarla en silencio.

### 5. Includes faltantes en POSIX (`runtime_services.c`)
`_exit` y `mkdir` se usaban sin declaración en la rama POSIX (en Windows venían de
`<stdlib.h>`/`<direct.h>` vía los includes Win32). Cascada: con `_exit` declarado
implícitamente GCC asumía que retornaba y marcaba "control reaches end" en `load_exec`.
**Fix:** la rama `#else` añade `<sys/stat.h>`, `<sys/types.h>` y `<unistd.h>`.

### 6. Símbolos sin definir en el enlace final (POSIX)
`libgow3gpu.so` (SHARED en POSIX, `--allow-shlib-undefined` en el CMake) referencia 3
símbolos que en Windows resolvían de otra forma:

- `__ImageBase` — lo define el **CRT de Windows** (ucrtbase); el código GPU
  (`buffer_cache.cpp`, `vk_scheduler.cpp`) lo declara `extern "C" char __ImageBase;` y usa
  `&__ImageBase` como ancla dentro de la imagen del loader. En Linux nadie lo define.
  **Fix:** `probe.c` lo define en un bloque `#ifndef _WIN32` (en Windows el CRT ya lo
  aporta; definirlo ahí chocaría).
- `runtime_exec_watch_retarget` / `runtime_exec_watch_set_reads` — definidos en `probe.c`
  dentro del gran bloque `#ifdef _WIN32` (usan debug registers + handlers de excepción
  Win32); el GPU solo los llama desde diagnósticos gated por `GOW3_SRT_WATCH`,
  `GOW3_TRACE_WATCH_NODE` y `GOW3_RESULT_WATCH` (características de depuración de
  Windows). **Fix:** definiciones no-op en `#ifndef _WIN32` que avisan una sola vez si
  alguna vez se invocan.

## Garantía de compatibilidad con Windows (demostración)

Por cada cambio, lo que ocurre en la rama Windows (MSYS2 Clang, LLP64):

| Archivo | Cambio | Efecto en Windows |
|---|---|---|
| `gpu/shim/gow3_win_compat.h` | Nuevo `#else` POSIX + comentario actualizado | El bloque `#ifdef _WIN32` es textualmente idéntico; el `#else` no se compila |
| `gpu/CMakeLists.txt` | 2 líneas en la rama `else()` | La rama `if (WIN32)` no se toca; el force-include Windows ya estaba |
| `gpu/.../vk_rasterizer.cpp` | `std::min(VAddr(window + 1_MB), end)` | `VAddr(…)` = cast a `unsigned long long`, su propio tipo: no-op, mismo overload y codegen |
| `src/runtime_services.c` | 3 includes en `#else` | No se compilan en Windows |
| `src/runtime_kernel.c` | Declaración bajo `#ifdef _WIN32` + `#else` con `unused` | En Windows: texto y posición de la declaración invariantes (BSS idéntico) |
| `src/probe.c` | Bloque `#ifndef _WIN32` + rama `#else` de red-zone | No se compilan en Windows |
| `src/runtime_file.c`, `src/runtime_hud.c` | Pares de `#pragma GCC diagnostic` | Los pragmas no generan código; Clang los ignora sin warnings (verificado compilando un TU de prueba con `clang -Werror` y `gcc -Werror`) |

El único cambio en código compartido es el cast de `vk_rasterizer.cpp`, que es
conversión a tipo idéntico en LLP64 (no se puede demostrar byte a byte sin máquina
Windows; el razonamiento anterior es la prueba). Nota: el submódulo `fsr-vulkan` aparece
`-dirty` en `git status` porque `build.sh` aplica los parches del proyecto a su working
tree: comportamiento preexistente e idéntico en Windows.

Advertencias de CMake 4.x benignas (sin efecto en el build): `CMP0167` (FindBoost
deprecated), deprecaciones de `miniz` (`<3.10`, `CMP0077`) y `sirit` (`CMP0218`).

## Riesgos / observaciones para las siguientes tareas

- **PGO off** (no hay `pgo/tlou.profdata` en el repo): el build es normal; en Windows el
  perfil da ~10% extra. LTO sí está ON.
- El smoke test `--vulkan-only` pasa en una GTX 1060 (pre-Turing, VRAM 3 GB): la
  verificación de juego real (Tarea 12/13) es donde importará la GPU.
- Tarea 3 (`build.sh --test`): los binarios de test se compilan con el mismo
  `-Werror` de GCC; si algún test compila código compartido que aún no se tocó, podría
  aparecer algún diagnóstico nuevo de la misma familia (format-truncation/array-bounds).
- Tarea 6/7 (`tools/probe_tlou.py`): el nombre del binario ya es `gow3-probe` en POSIX
  (sin `.exe`); pendiente el guard de `sys.platform == "win32"` del PATH de MSYS2.
