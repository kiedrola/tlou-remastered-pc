# Tarea 4 — Generación de host shaders y librería GPU en el camino POSIX (Fase 1)

> **Estado: COMPLETADA Y VERIFICADA.** La generación de cabeceras de host shaders
> (`glslang`, target `host_shaders`) y la compilación de la librería `gow3gpu` **no
> dependen de nada exclusivo de Windows en el camino POSIX** — demostrado con un build
> limpio completo fuera del árbol (266/266 pasos de ninja, salida 0) en Arch Linux usando
> solo herramientas de la distro, más una auditoría estática de cada fuente compilada en
> POSIX. **No fue necesario cambiar ningún código para esta tarea; el build de Windows
> queda intacto por definición.**

## Entorno de verificación (Arch Linux)

Misma máquina que las tareas 1-3: x86_64 (8 núcleos), GCC 16.2.1 (`cc`/`c++`), CMake
4.4.4, Ninja 1.13.2, pkgconf 3.0.7, **glslang 16.6.0** (`/usr/bin/glslang`, el paquete
`glslang` de la distro, *no* el de MSYS2), Vulkan 1.4.363 (NVIDIA GTX 1060 3GB), SDL3
3.4.18, FFmpeg 9.0.2.

## Verificación en vivo (la prueba decisiva)

1. **Configuración limpia fuera del árbol** (`cmake -S gpu -B /tmp/gow3-task04-verify -G
   Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo`), salida 0. La caché prueba que cada entrada
   resuelve a un componente de la distro: `CMAKE_C_COMPILER=/usr/bin/cc`,
   `CMAKE_CXX_COMPILER=/usr/bin/c++`, `GLSLANG_COMPILER=/usr/bin/glslang`,
   `Vulkan_LIBRARY=/usr/lib/libvulkan.so`, y los reemplazos por submódulos de
   `magic_enum`/`miniz` (igual que en Windows, CMakeLists:59-66).
2. **Build completo** (`ninja host_shaders gow3gpu`): **266/266 pasos, salida 0**, sin
   avisos de LTO y sin mensajes de reemplazo de VMA/submódulos. Todo lo que el camino
   POSIX compila — video core de shadPS4, recompilador de shaders, `shadps4/common/*`,
   todo `shim/`, imgui, FSR-Vulkan (`ffx-vulkan::portable`,
   `ffx-vulkan::fsr4-v07-vulkan`), sirit, miniz — se compiló y enlazó con cero
   elementos de Windows. Salida: `libgow3gpu.so`, 170 MB (RelWithDebInfo + LTO), mismo
   tamaño que la del árbol.
3. **Las 31 cabeceras generadas** (19 shaders individuales + 12 variantes `tiling` de
   BPP/micro-macro, exactamente la lista de `gpu/CMakeLists.txt:124-141`):
   - `diff -r` contra el `out/gpu/include/video_core/host_shaders/` del árbol:
     **iguales byte a byte (31/31)** — el build en el árbol de la tarea 1 usó el mismo
     mecanismo POSIX puro.
   - Las 31 llevan el mágico SPIR-V válido `0x07230203`; el formato es `#pragma once` +
     `#include <cstdint>` (insertado por `AddShaderHeaderInclude.cmake`) +
     `const uint32_t SYMBOL[]` de palabras SPIR-V compiladas. Sin contenido
     específico de plataforma.
   - Los `-D` se aplican: `tiling_micro_8` vs `tiling_micro_128` difieren
     (`BITS_PER_PIXEL`), micro vs macro difieren (`IS_MACRO_TILED`).
   - **La cadena de dependencias funciona en POSIX**: `touch fault_buffer_process.comp`
     + `ninja` regeneró su cabecera (el seguimiento fuente → cabecera viene del
     `DEPENDS` explícito de `add_host_shader`).
4. **El .so generado**: `ldd` → las 15 entradas NEEDED resueltas (libfmt.so.12,
   libZydis.so.4.1 + libZycore, **libvulkan.so.1**, libxxhash.so.0, libSDL3.so.0, las
   cinco libav*, libX11.so.6, el runtime C/C++); `nm -D` → **cero** símbolos dinámicos
   indefinidos exclusivos de Windows (sin `GetProcAddress`, `CreateFileW`,
   `GetModuleHandle*`, `__imp_*`...).
5. **build.ninja del build limpio**: cero referencias a `cygpath`/`windres`/`msys64`/
   `CLANG64`. La regla de shaders es exactamente: `cmake -E make_directory` +
   `/usr/bin/glslang --target-env vulkan1.3 -o <salida> --vn SYMBOL [-D...] <fuente>` +
   `cmake -DHEADER_FILE=<salida> -P AddShaderHeaderInclude.cmake`.
6. **De punta a punta**: `out/gow3-probe --vulkan-only` (librería del árbol, que incrusta
   el SPIR-V generado) → `command submission + 4096-byte readback PASS` en el dispositivo
   real.

## Auditoría estática del camino POSIX

### Generación de shaders
- `add_host_shader` (`gpu/CMakeLists.txt:105-123`): la única herramienta externa es el
  `glslang` del sistema (buscado con `find_program(GLSLANG_COMPILER NAMES glslang
  glslangValidator REQUIRED)`, CMakeLists:94). `AddShaderHeaderInclude.cmake` es puro
  CMake `file(READ/REPLACE/WRITE)` — sin código de plataforma.
- Los 11 orígenes GLSL son GLSL puro: sus únicos `#include` son los locales
  `fsr/ffx_a.h`/`fsr/ffx_fsr1.h` (resueltos relativos al fichero del shader, cabeceras C
  planas).
- El target `host_shaders` es una **dependencia dura** de `gow3gpu`
  (`add_dependencies(gow3gpu host_shaders)`, CMakeLists:170), y la rama POSIX de
  `build.sh` construye exactamente `ninja -C out/gpu gow3gpu` — así los shaders se
  regeneran siempre antes de que la librería enlaze, en todas las plataformas.

### Fuentes de la librería GPU (todo `shadps4/` + `shim/` compilado en POSIX)
Cada referencia a API de Win32 encontrada por la revisión está dentro de
`#ifdef _WIN32` con su `#else` para POSIX; nada desprotegido:

| Fichero | Contenido Windows | Camino POSIX |
|---|---|---|
| `shadps4/common/ntapi.cpp` | fichero entero en `#ifdef _WIN32` (búsqueda de procs en `ntdll.dll`) | unidad de traducción vacía; ningún llamador compartido de `Common::NtApi` |
| `shadps4/video_core/blob_pack.cpp` | `CreateFileW`/`MapViewOfFile`, `GetFileAttributesExW`, `GetSystemTimeAsFileTime`, `FindFirstFileExW`, `PrefetchVirtualMemory` (5 bloques protegidos) | `stat`, `clock_gettime`, `open`/`mmap`/`munmap`, iterador de directorios |
| `shadps4/video_core/page_manager.cpp:99` | rebase de base con `GetModuleHandleW` en `GOW3_FAULT_STATS` | el `#else` deja `rip = 0` |
| `shim/gow3_profiler.cpp` | suspensión/muestreo de hilos Win32 (`SuspendThread`, `GetModuleHandleExA`, ...) | muestreador inoperante en el `#else` |
| `shadps4/video_core/renderdoc.cpp` | interop con el DLL de RenderDoc | **no se compila en absoluto**: lo excluye el filtro del glob (`list(FILTER VIDEO_CORE EXCLUDE REGEX "renderdoc\\.cpp$"`, CMakeLists:150) |
| `shim/window.cpp` | `SDL_PROP_WINDOW_WIN32_HWND_POINTER` (2 lecturas protegidas) | SDL3 X11/Wayland; `WindowSystemType` ya enumera `X11`/`Wayland`/`Headless` |

- Sin literales `.exe`/`.dll`/`C:\`/`C:/`/backslash desprotegidos en ninguna de las
  fuentes compiladas (revisado `shadps4/` + `shim/`).
- Sin rutas absolutas duras en las fuentes. La única ruta absoluta en tiempo de
  compilación es `GOW3_FONT_PATH` (CMakeLists:178): `third_party/fonts/DejaVuSans.ttf`
  (759 KB, en el repositorio) se **incrusta en el binario** mediante un `.incbin` de
  ensamblador inline (`gow3_overlay.cpp:42-62`, `gow3_prompts.cpp:1867`) — `.rdata` en
  Windows, `.rodata` en POSIX, un mismo diseño para ambas. La ruta solo se necesita *en
  tiempo de compilación*; en tiempo de ejecución la fuente vive en los datos de solo
  lectura del .so, así que mover el paquete (tareas 7/16) no necesita ningún fichero de
  fuente. Verificado presente en el .so construido.
- `shim/gow3_win_compat.h` (fuerza-incluida en **ambas** plataformas, CMakeLists:187-190):
  el lado `_WIN32` declara `GetCurrentThreadId` (la tarea 1 lo mantuvo idéntico byte a
  byte); el lado POSIX lo mapea a `gettid()`; la cabecera queda libre de `<windows.h>`
  fuera de la guarda, así que sus macros nunca llegan a una unidad de traducción en
  Linux.
- **Loader de Vulkan**: `libgow3gpu.so` lleva `NEEDED libvulkan.so.1` en *ambas*
  plataformas — en POSIX llega a través de los targets de FFX-Vulkan, que enlazan
  `Vulkan::Vulkan` como PUBLIC (`fsr-vulkan/CMakeLists.txt:79,123,154,306`), exactamente
  como en Windows. Ninguna lib exclusiva de Windows (`ws2_32`/`bcrypt`/`ntdll`/`winmm`/
  `pdh`, CMakeLists:189) existe en la línea de enlace POSIX — la rama `else()` solo
  enlaza `PkgConfig::X11` más el conjunto compartido de dependencias.
- Los submódulos compilados en ambos árboles son neutros de plataforma: las fuentes de
  `fsr-vulkan` contienen **cero** guardas `WIN32` y ningún `windows.h`; `sirit`
  igualmente (y `SIRIT_USE_SYSTEM_SPIRV_HEADERS OFF` fuerza sus
  `externals/SPIRV-Headers` versionados).
- Rama POSIX de `build.sh`: las herramientas usadas son `cc`, `pkg-config`, `cmake`,
  `ninja`, `git` (submódulos/patches), `ar` y (transitivamente) `glslang` — todo
  paquetes de la distro. `cygpath`, `windres`, ficheros `.rc` y `msys2-env.sh` existen
  **solo** en la rama de Windows, que termina con `exit 0`.

## Problemas encontrados y decisiones técnicas

Ninguno que exigiera cambiar código: el primer build real en Linux (tarea 1) ya eliminó
los dos puntos genuinamente acoplados a Windows de esta zona (el shim de
`GetCurrentThreadId` y el cast `VAddr`/`std::min` de LLP64), y la auditoría no encontró
acoplamiento adicional en la generación de shaders ni en la compilación de la librería
GPU. Se registraron dos observaciones de diseño para las tareas de empaquetado.

## Garantía de compatibilidad con Windows (prueba)

| Cambio en esta tarea | Efecto en Windows |
|---|---|
| **Ninguno.** Solo un build desechable en `/tmp` (ya eliminado) e inspecciones de solo lectura. | La rama de Windows de `build.sh`, de `gpu/CMakeLists.txt` y de cada fuente auditada queda sin cambios byte a byte. |

## Riesgos / observaciones para las tareas siguientes

- **Tarea 7 / 16 (`package_linux.sh`):** el cierre NEEDED exacto de `libgow3gpu.so` son
  las 15 entradas listadas en §Verificación en vivo.4 (más el cierre transitivo de
  libav*/SDL3, que permanece en la distro). No se necesita fichero de fuente en tiempo de
  ejecución (está incrustada), y el RUNPATH `$ORIGIN/gpu` de `gow3-probe` es el
  mecanismo de reubicación previsto.
- **`GOW3_FONT_PATH` es solo de tiempo de compilación** (`.incbin`): una copia del árbol
  de fuentes se puede compilar en cualquier sitio; la ruta incrustada no existe en
  tiempo de ejecución y nunca se consulta entonces. No hace falta sobreescribir nada
  para empaquetar; la tarea 10 puede descartar esta preocupación.
- **Existen dos implementaciones de `add_host_shader`**: la de gow3 (activa,
  `gpu/CMakeLists.txt:105-123`) y la de shadPS4 upstream en
  `shadps4/video_core/host_shaders/CMakeLists.txt` (**muerta** — nunca se
  `add_subdirectory`). La versión upstream añade el seguimiento del preprocesador de
  glslang con `--depfile`/`DEPFILE`; la de gow3 se apoya en el `DEPENDS <src>.comp`
  explícito y funciona hoy porque los únicos includes de los shaders son los dos locales
  `fsr/ffx_*.h`. Si un shader futuro añade un include de un fichero nuevo, esa
  dependencia no se seguiría — conviene entonces usar la variante upstream o añadir el
  include a `DEPENDS`.
- **Los 170 MB del .so** (RelWithDebInfo + LTO) son esperables (el build de Windows es
  comparable); una variante de release sin símbolos ayudaría al empaquetado (tarea 16)
  pero no cambia nada funcional.
- **`--allow-shlib-undefined`** (CMakeLists:176) es lo que permite que la librería
  SHARED lleve sus 51 símbolos `vk*` indefinidos hasta que el loader se enlace vía
  FFX-Vulkan en POSIX y en el ejecutable en Windows; ya está dentro de la guarda
  `if (NOT WIN32)`, así que el camino de la librería estática de Windows queda intacto.
- **Tarea 10 (iconos/fuentes):** la fuente incrustada del overlay se confirma funcional en
  POSIX (esta tarea); solo queda por resolver la cuestión del icono de la ventana.
