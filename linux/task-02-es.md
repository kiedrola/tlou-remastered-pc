# Tarea 2 — Dependencias de sistema, documentadas y verificadas (Fase 1)

> **Estado: COMPLETADA Y VERIFICADA.** Toda la dependencias de sistema del build de Linux
> está documentada, verificada contra `build.sh` / `gpu/CMakeLists.txt`, confirmada en
> vivo en una máquina Arch Linux x86-64 y comparada con la lista de paquetes MSYS2 de
> `gow3_pc/packaging/windows/README.md`. El resultado es un nuevo
> `gow3_pc/packaging/linux/README.md`, el equivalente Linux del de Windows. **No se
> cambió ningún código en esta tarea: el build de Windows queda intacto por definición.**

## Entorno de verificación (Arch Linux)

La misma máquina que la Tarea 1: x86_64, GCC 16.2.1 (`cc`), CMake 4.4.4, Ninja 1.13.2,
pkgconf 3.0.7, instancia Vulkan 1.4.363 (NVIDIA GTX 1060), SDL3 3.4.18, FFmpeg 9.0.2.

## Lo que se hizo

1. Auditoría estática de la rama POSIX de `gow3_pc/build.sh` y de
   `gow3_pc/gpu/CMakeLists.txt` para enumerar **toda** dependencia externa y *cómo* se
   consume (`pkg-config`, `find_package`, `find_program`, submódulo o vendored en el
   repositorio).
2. Verificación en vivo de cada punto: `pkg-config --modversion` de los nueve módulos
   `.pc` que usa el build, los paquetes CMake config en `/usr/{lib,share}/cmake`, las
   herramientas en `PATH` y el estado de los seis submódulos.
3. Un **configuración CMake limpia fuera del árbol** (`cmake -S gpu -B /tmp/gow3-task02-verify`)
   más el target `host_shaders`, para demostrar que todos los `find` con `REQUIRED` se
   resuelven y que `glslang` funciona de punta a punta (31/31 headers de shader
   generados).
4. Comparación de la lista resultante con los paquetes MSYS2 de
   `gow3_pc/packaging/windows/README.md` y documentación del resultado en
   `gow3_pc/packaging/linux/README.md`.

## Dependencias de sistema necesarias (verificadas en Arch)

| Necesidad | Paquetes Arch (versión local) | Consumido por | Verificado |
|---|---|---|---|
| Compilador C | `base-devel` → `cc`/`gcc` 16.2.1 | `build.sh:49-57` (cadena de fallback de `CC`) | build completo de la Tarea 1 |
| Sistema de build | `cmake` 4.4.4, `ninja` 1.13.2 | `build.sh:60` (guarda), CMake ≥ 3.24 | configuración limpia, exit 0 |
| pkg-config | `pkgconf` 3.0.7 | `build.sh:60,66-67` (`vulkan sdl3 libav*`) | los 9 módulos se resuelven |
| Submódulos/patches | `git` 2.56.0 | `build.sh` (`git submodule update`, `git apply` de `gpu/patches/fsr-vulkan/*.patch`) | submódulos presentes; en POSIX no se necesita `patch` |
| Cargador Vulkan | `vulkan-icd-loader` 1.4.363.0 | `find_package(Vulkan)` (VulkanLoader), `pkg-config vulkan` (`vulkan.pc`), `libvulkan.so.1` | configuración limpia; smoke test `--vulkan-only` de la Tarea 1 |
| Headers Vulkan | `vulkan-headers` 1:1.4.363.0 | `find_package(Vulkan)` (VulkanHeaders) | configuración limpia |
| Alocador GPU | `vulkan-memory-allocator` 3.5.0 | `find_package(VulkanMemoryAllocator CONFIG QUIET)` → `/usr/share/cmake/VulkanMemoryAllocator` (CMakeLists:68) | CONFIG encontrado en la configuración limpia |
| Compilador de shaders | `glslang` 1:1.4.363.0 | `find_program(GLSLANG_COMPILER NAMES glslang glslangValidator REQUIRED)` (CMakeLists:94) | **31/31** headers de `host_shaders` generados por `/usr/bin/glslang` en el build temporal |
| Ventanas | `sdl3` 3.4.18 | `pkg-config sdl3` (build.sh:60,66-67; CMakeLists:78) | `sdl3.pc` → `libSDL3.so` |
| Códecs | `ffmpeg` 2:9.0.2 | `pkg_check_modules(FFMPEG ... libavformat libavcodec libavutil libswscale libswresample)` (CMakeLists:156) + build.sh:66-67 | los 5 módulos `.pc` se resuelven |
| Asio | `boost` 1.92.0 (solo headers, `BOOST_ASIO_STANDALONE`) | `find_package(Boost REQUIRED)` (CMakeLists:58) → `Boost::headers` | config `Boost-1.92.0` resuelto |
| Formateo | `fmt` 12.2.0 | `find_package(fmt REQUIRED)` (CMakeLists:57) | config `fmt` resuelto |
| Mapa hash | `robin-map` 1.4.1 | `find_package(tsl-robin-map REQUIRED)` (CMakeLists:67) | config `tsl-robin-map` resuelto |
| Hashes | `xxhash` 0.8.4 | `pkg_check_modules(XXHASH ... libxxhash)` (CMakeLists:77) | `libxxhash.pc` → `libxxhash.so` |
| Desensamblador | `zydis` 4.1.1 | `find_package(Zydis REQUIRED)` (CMakeLists:93) | config `zydis` resuelto |
| Pegamento X11 | `libx11` 1.8.13 | `pkg_check_modules(X11 ... x11)` bajo `if (NOT WIN32)` (CMakeLists:79-81) | `x11.pc` → `libX11.so` |

**No son paquetes (por diseño, igual que en Windows):** `magic_enum`, `miniz`, `xbyak`,
`imgui` y FSR-Vulkan — submódulos bajo `gpu/third_party/` (`.gitmodules`); `LibAtrac9`
bajo `third_party/`. En la configuración limpia, `magic_enum_DIR-NOTFOUND` y
`miniz_DIR-NOTFOUND` → CMake usó los submódulos exactamente como pretende el código
(CMakeLists:59-66, 82-92), igual que en Windows. `sirit` está versionado en el
repositorio (con sus propios `externals/SPIRV-Headers`).

## La lista del roadmap: qué se sostuvo y qué no

El roadmap listaba 16 paquetes. **12 son necesarios** (la tabla de arriba, sin las
herramientas de build). Los otros 4 **no los necesita el build**:

- `spirv-cross` — solo lo usan las herramientas opcionales de assets
  (`tools/fsr4_optimize.sh:23`, `tools/fetch_fsr4_assets.sh:35`). El build nunca la
  llama: la conversión shader→header la hace `glslang`, y el ensamblado dinámico de
  SPIR-V usa `sirit`, un ensamblador en tiempo de ejecución autocontenido (CMakeLists:96
  fuerza `SIRIT_USE_SYSTEM_SPIRV_HEADERS OFF`, así que se usa
  `externals/SPIRV-Headers`, no un paquete del sistema).
- `xbyak` — solo headers y se consume del submódulo
  (`target_include_directories ... third_party/xbyak`); en Arch ni siquiera está
  empaquetado (verificado contra los repositorios).
- `libxcb`, `wayland` — no los referencia `build.sh` ni `CMakeLists.txt`; están en
  `shell.nix` y solo se necesitan en **tiempo de ejecución** por la vía Wayland de SDL3
  (y los cargadores de ICD de Vulkan). Lo mismo para `xorgproto` (dependencia de build
  de los paquetes `libxcb`/`wayland`).

**Faltaban en la lista del roadmap (necesarios, ya documentados):** `base-devel` (el
compilador C), `cmake`, `ninja`, `pkgconf`, `git`. `python`/`python3` lo necesitan
`tools/` y el launcher (Tareas 9-10), no `build.sh` — está en `shell.nix` para eso.

## Evidencia de la configuración limpia

```
$ cmake -S gpu -B /tmp/gow3-task02-verify -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
  → exit 0; la caché muestra:
    Vulkan_LIBRARY=/usr/lib/libvulkan.so        (VulkanHeaders + VulkanLoader 1.4.363)
    VulkanMemoryAllocator_DIR=/usr/share/cmake/VulkanMemoryAllocator
    fmt_DIR=/usr/lib/cmake/fmt                  Boost_DIR=/usr/lib/cmake/Boost-1.92.0
    tsl-robin-map_DIR=/usr/share/cmake/tsl-robin-map
    Zydis_DIR=/usr/lib/cmake/zydis
    GLSLANG_COMPILER=/usr/bin/glslang
    pkgcfg: SDL3→libSDL3.so, X11→libX11.so, XXHASH→libxxhash.so,
            FFMPEG→libavformat/libavcodec/libavutil/libswscale/libswresample
    magic_enum_DIR=miniz_DIR=NOTFOUND → entra el fallback a submódulos (como está diseñado)
$ ninja -C /tmp/gow3-task02-verify host_shaders
  → [31/31] todos los headers de host_shaders generados por glslang
```

Los artefactos de la Tarea 1 siguen enlazando limpios: `ldd out/gow3-probe` resuelve
todo; el conjunto directo es `libgow3gpu.so` (RUNPATH `$ORIGIN/gpu`),
`libvulkan.so.1`, `libSDL3.so.0`, los cinco `libav*`, `libfmt.so.12`,
`libZydis.so.4.1` (+`libZycore`), `libxxhash.so.0`, `libX11.so.6` — el resto de la
salida de `ldd` es el cierre transitivo de `libav*`/SDL3.

## Comparación MSYS2 ↔ Arch (`packaging/windows/README.md`)

| MSYS2 (CLANG64) | Arch | Notas |
|---|---|---|
| `mingw-w64-clang-x86_64-toolchain` | `base-devel` | clang → GCC de la distro; `make`/`binutils` vienen en ella, pero solo se usa `ninja` |
| `mingw-w64-clang-x86_64-cmake` / `-ninja` / `-pkgconf` | `cmake` / `ninja` / `pkgconf` | uno a uno |
| `mingw-w64-clang-x86_64-python` | `python` | tools y launcher, no `build.sh` |
| `git`, `patch` | `git` | la rama POSIX aplica los parches de FSR-Vulkan con `git apply`; `patch` es solo para Windows |
| `mingw-w64-clang-x86_64-vulkan-headers` / `-vulkan-loader` | `vulkan-headers` / `vulkan-icd-loader` | uno a uno |
| `mingw-w64-clang-x86_64-vulkan-memory-allocator` / `-glslang` | `vulkan-memory-allocator` / `glslang` | uno a uno |
| `mingw-w64-clang-x86_64-sdl3` / `-fmt` / `-boost` / `-robin-map` / `-xxhash` / `-ffmpeg` / `-zydis` | `sdl3` / `fmt` / `boost` / `robin-map` / `xxhash` / `ffmpeg` / `zydis` | uno a uno |
| `mingw-w64-clang-x86_64-spirv-tools` / `-spirv-headers` / `-spirv-cross` | — | no los necesita `build.sh` en ningún SO: `sirit` es autocontenido; `spirv-cross` sirve a las herramientas opcionales de assets. (En MSYS2 además hacen de herramientas de desarrollo para los mismos scripts.) |
| (submódulos: `magic_enum`, `miniz`, `xbyak`, `imgui`, FSR-Vulkan) | los mismos submódulos | `xbyak` no está empaquetado en Arch; los paquetes de `magic_enum`/`miniz` son opcionales porque `find_package(... QUIET)` cae en los submódulos (el comentario de CMakeLists:59 dice que este es el comportamiento previsto) |

Resultado: las dos plataformas necesitan el **mismo conjunto lógico de dependencias**;
solo cambian los nombres de paquete y dos extras solo-Windows (`patch` y el trío
`spirv-*` como herramientas de desarrollo).

## Decisiones técnicas y observaciones

1. **Documento nuevo: `gow3_pc/packaging/linux/README.md`.** Replica al gemelo de
   Windows (`packaging/windows/README.md`) en estructura: intro corta, un bloque de
   instalación, la nota de submódulos, el comando de build y las notas
   específicas de plataforma. Documenta *qué instalar*; la evidencia y la comparación
   MSYS2 viven en este informe.
2. **`vulkan-memory-allocator` es el punto frágil.** En esta máquina está instalado
   (3.5.0-1) pero **no existe en la base de datos de sincronización** (`pacman -S` no
   puede instalarlo aquí; en Arch estándar está en `extra`). El build lo resuelve por el
   paquete CMake CONFIG que empaqueta la distro. Ojo: el fallback solo-headers de
   `gpu/CMakeLists.txt:68-75` busca con `PATH_SUFFIXES vma` — eso coincide con el
   **layout de MSYS2** (`include/vma/vk_mem_alloc.h`); Arch instala el header en
   `/usr/include/vk_mem_alloc.h`, así que en Arch el paquete CONFIG es la vía soportada.
   Se deja tal cual: el fallback existe para MSYS2 y tocar el CMake compartido violaría
   la regla de "no mover la rama de Windows" sin beneficio.
3. **La guarda de `build.sh:60` se cumple en Arch** (`pkg-config --exists vulkan sdl3` +
   `cmake` + `ninja` presentes), así que la reactivación automática de `nix-shell`
   (`build.sh:61-62`) no se dispara; `shell.nix` sigue siendo el fallback reproducible
   (Plan B).
4. **`shell.nix` vs. realidad:** lista extras que el build no necesita
   (`magic-enum`, `miniz`, `xbyak`, `spirv-cross`, `spirv-headers`, `libxcb`, `xorgproto`,
   `wayland`, `gnumake`, `python3`). Todos son inofensivos (los submódulos ganan para
   magic_enum/miniz; el resto es runtime/tooling). No se recorta: `shell.nix` es el
   entorno de NixOS/Plan B y los paquetes extra son seguro barato, y cambiarlo no puede
   romper Windows.
5. **El desfase de versiones es inofensivo.** Algunos paquetes instalados van por detrás
   de las versiones actuales del repositorio (SDL3 3.4.18 vs 3.4.20, FFmpeg 9.0.2-2 vs
   -5, libx11 1.8.13 vs 1.8.14); el build y los smoke tests de la Tarea 1 pasan con las
   versiones instaladas.
6. **Compatibilidad con Windows:** tarea solo-documentación. Cero cambios en `build.sh`,
   `CMakeLists.txt` o cualquier fuente: el build de Windows queda intacto, demostrable.

## Riesgos / observaciones para las tareas siguientes

- **Tarea 3 (`build.sh --test`):** no hay dependencias nuevas — los binarios de tests
  enlazan el mismo conjunto de librerías (verificado leyendo la rama `--test`,
  `build.sh:108-130`).
- **Tarea 7 (`package_linux.sh`):** el cierre de `.so` del paquete es el conjunto
  directo de `ldd` listado arriba (`libgow3gpu.so`, `libvulkan.so.1`, `libSDL3.so.0`,
  los cinco `libav*`, `libfmt`, `libZydis`+`libZycore`, `libxxhash`, `libX11` + la
  familia de runtime C/C++); todo lo demás del `ldd` es cierre transitivo de
  `libav*`/SDL3 y se queda en la distro.
- **Tarea 9 (launcher Tkinter):** necesita `python`, `python-tk` y `Pillow` (o
  `python-pip` + wheels) encima de esta lista.
- **ICD de Vulkan:** el paquete del driver (p. ej. `mesa-vulkan-drivers` o el
  propietario de NVIDIA) es un requisito de tiempo de ejecución, no de build; queda
  anotado en el README nuevo.
