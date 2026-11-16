# Tarea 3 — Tests unitarios en Linux (Fase 1)

> **Estado: COMPLETADA Y VERIFICADA.** `gow3_pc/build.sh --test` compila los diez tests
> unitarios con `-Wall -Wextra -Werror` de GCC y **todos pasan** en Arch Linux x86-64:
> pad (hilos/SDL), runtime (hilos, memoria, mutexes, resolutor), lector-escritor, ficheros
> de mods, semáforos, AppContent, MP3, decodificación de vídeo, servicios y HTTP. El
> comando completo termina con 0.

## Entorno de verificación (Arch Linux)

Misma máquina que las Tareas 1-2: x86_64, GCC 16.2.1 (`cc`), CMake 4.4.4, Ninja 1.13.2,
pkgconf 3.0.7, Vulkan 1.4.363 (NVIDIA GTX 1060 3GB), SDL3 3.4.18, FFmpeg 9.0.2.
Los tests corren en modo *headless*: `pad-test` usa `SDL_VIDEODRIVER=dummy` y un mando
virtual de SDL, así que no hace falta pantalla ni mando físico.

## Resultados de los tests (los 10, de la rama `--test`, `build.sh:108-130`)

| Binario | Fuente | Área | Resultado |
|---|---|---|---|
| `out/pad-test` | `tests/test_pad.c` | ABI de pad, clics izquierdos/derechos escalonados, acorde L1, coordenadas táctiles de SDL, captura de la superposición (mando SDL3 + touchpad virtual) | `PASS: pad ABI, staged left/right clicks, L1 chord, SDL touch coordinates, overlay capture` |
| `out/runtime-test` | `tests/test_runtime.c` + todos los `src/runtime*.c` | ciclo de vida de callbacks, ABI de la guarda, errores de mutex, memoria directa compartida, primitivas de memoria, alcance del resolutor | `PASS: callback lifecycle, guard ABI, mutex errors, shared direct memory, memory primitives, resolver scope` |
| `out/rwlock-rp-test` | `tests/test_rwlock_rp.c` | los lectores se anidan tras un escritor en cola (el caso donde el rwlock de winpthreads se bloquea); los bloqueos se convierten en fallo con `alarm()` | `reader-preferring rwlock tests passed` |
| `out/file-mods-test` | `tests/test_file_mods.c` | ficheros de mods del invitado: lecturas, stat, listado fusionado, assets solo-lectura, guardados escribibles | `... PASS` |
| `out/sema-test` | `tests/test_sema.c` + todos los `src/runtime*.c` | contratos de `libSceSema` (crear/atributos/esperar/cancelar) | `PASS: semaphore contracts` |
| `out/content-test` | `tests/test_content.c` + `src/runtime_content.c` | ciclo de vida, perfil y límites de salida de AppContent | `PASS: AppContent lifecycle, profile and output boundaries` |
| `out/mp3-test` | `tests/test_mp3.c` + `src/runtime_mp3.c` + LibAtrac9 | MP3 a través de `libSceAjm`: análisis de cabeceras/gapless, trabajos de decodificación (`tests/data/sine.mp3`) | `mp3 test: ok` |
| `out/videodec-test` | `tests/test_videodec.c` | unidades de acceso H.264 a cuadros NV12 vía FFmpeg (`tests/data/tiny.h264`) | `videodec test: ok` |
| `out/services-test` | `tests/test_services.c` | diálogos (las barras de progreso corren hasta cerrarse), PlayGo, trofeos | `services test: ok` |
| `out/http-test` | `tests/test_http.c` | peticiones de `libSceHttp`, servicio de ficheros con `GOW3_HTTP_FILES` | `http test: ok` |

Los binarios de test se recompilan siempre en la rama `--test` (no están cacheados por
ninja), así que el flujo es autónomo e idempotente. Tras el arreglo, el `out/gow3-probe`
y el `out/gow3-gpu-capabilities` reconstruidos siguen pasando los tests de humo de la
Tarea 1 (`--vulkan-only`: `command submission + 4096-byte readback PASS`).

## Problemas encontrados y decisiones técnicas

La rama `--test` es solo POSIX (la rama Windows de `build.sh` termina con `exit 0` antes de
llegar a ella) y, según las limitaciones conocidas del roadmap, **nunca se había ejecutado
en una máquina Linux real**: la primera ejecución falló, y la segunda (después del primer
arreglo) volvió a fallar. Tres defectos distintos, todos de link/compilación de los tests,
ninguno de la lógica de los tests — los propios asserts pasan intactos.

### 1. `pad-test`: `sincosf@@GLIBC_2.2.5` sin definir + stubs faltantes
Primer síntoma: `ld: ... referencia sin definir al símbolo «sincosf@@GLIBC_2.2.5»` junto a
`libm.so.6: DSO missing from command line` (en español del sistema: «Falta DSO en la línea
de órdenes»).

- **`sincosf`:** `runtime_pad.c:139-141` llama a `sinf(x)` y `cosf(x)` con el *mismo*
  argumento; con `_GNU_SOURCE` y `-O2`, GCC prueba que es un par `sincosf` y emite una sola
  llamada a `sincosf` — una extensión de glibc que no existe en el CRT de MSYS2, por lo que
  Windows nunca la vio. La línea de link de `pad-test` no tenía `-lm` (los demás tests que
  usan matemáticas sí lo tienen).
  **Arreglo:** `-lm` añadido a la línea de `pad-test` en `build.sh` (un token).
- **Stubs faltantes:** después, el link reveló 14 símbolos indefinidos más: la API de
  controles/prompts/ratón de la librería GPU (`gow3gpu_controls_*`, `gow3gpu_prompts_*`,
  `gow3gpu_mouse_*`) y dos módulos del runtime (`runtime_gamesettings_tick`,
  `runtime_mouselook_{set_stick_boost,aim_recent,camera_recent}`). `test_pad.c` sigue el
  patrón establecido del proyecto para tests unitarios — hace `#include` de
  `src/runtime_pad.c` como su propia unidad de traducción y stubbea sus dependencias (ya
  stubbeaba `runtime_lookup` y `gow3gpu_overlay_captures_input`; `test_services.c`/
  `test_videodec.c` hacen lo mismo con las suyas). La línea de build nunca enlazó la
  librería GPU para `pad-test`, así que esos stubs simplemente nunca se escribieron (la
  línea nunca se había ejecutado).
  **Arreglo:** 15 stubs neutros añadidos a `test_pad.c` en el mismo estilo de línea única
  con casts `(void)` que los existentes (sin entrada de ratón, sensibilidad por defecto
  `1.0f`, sin inversión, sin hook de cámara, generación `0`). `gow3gpu_controls_vibration`
  no necesitaba stub: su único caller es una función estática eliminada por *dead code
  elimination* en `-O2` (verificado por el link).

### 2. `runtime-test` / `sema-test`: tres símbolos exclusivos de `probe.c`
Enlazar el `libgow3gpu.so` compartido exige *todos* sus símbolos `runtime_*` indefinidos en
el ejecutable (a diferencia de la librería estática de Windows, que solo arrastra los
objetos referenciados). Tres de ellos solo están definidos en el bloque `#ifndef _WIN32` de
`probe.c` (arreglo de la Tarea 1): `__ImageBase` (ancla de la imagen del cargador),
`runtime_exec_watch_retarget` y `runtime_exec_watch_set_reads` (sin efecto; los reales usan
los registros de depuración de Windows). Los tests deliberadamente no enlazan `probe.c` —
ya stubbean `runtime_restart` por exactamente esta razón.
**Arreglo:** los mismos tres símbolos stubbeados en `test_runtime.c` y `test_sema.c`, junto
al stub existente de `runtime_restart` (un `char __ImageBase;` y dos funciones sin efecto).
Es el cambio mínimo; mover las definiciones fuera de `probe.c` a un módulo del runtime
habría tocado código compartido sin ganancia.
Nota: a `mp3-test` se le sospechó el mismo problema, pero **no** enlaza la librería GPU en
`build.sh` (y `test_mp3.c`/`runtime_mp3.c`/`runtime_ajm.c` no referencian ningún símbolo
`gow3gpu_*`): solo enlaza LibAtrac9 + FFmpeg, igual que su objetivo CMake de Windows
(`gpu/CMakeLists.txt:253-255` enlaza `mp3-test` con `atrac9 PkgConfig::FFMPEG`, sin
`gow3gpu`). No hace falta ningún cambio.

### 3. `http-test`: declaración implícita de `rmdir`
`-Werror -Wimplicit-function-declaration`: en POSIX `rmdir` se declara en `<unistd.h>`; en
Windows llega a través de la cadena de cabeceras de Win32. (El `mkdir`/`remove` del test ya
estaban cubiertos: `mkdir` vía `<sys/stat.h>` arrastrado por el `runtime_http.c` incluido, y
`remove` vía `<stdio.h>`.)
**Arreglo:** `#ifndef _WIN32 #include <unistd.h> #endif` en `tests/test_http.c` — el idiom
estándar del proyecto para includes de POSIX faltantes (la Tarea 1 hizo lo mismo en
`runtime_services.c`).

## Garantía de compatibilidad con Windows (prueba)

| Fichero | Cambio | Efecto en Windows |
|---|---|---|
| `gow3_pc/build.sh` | `-lm` en la línea de `pad-test` (`build.sh:109`) | La línea está dentro del bloque `--test`, que solo existe tras la rama Windows; esa rama termina con `exit 0` dentro del `case`, así que nunca llega a esta línea. La ruta Windows queda idéntica a nivel de byte. |
| `gow3_pc/tests/test_pad.c` | 15 stubs de dependencias | El fichero solo lo compila la rama `--test` de POSIX; ninguna build de Windows lo referencia (los objetivos de test CMake de Windows usan `test_pad_buffer.c`/`test_pad_map.c`; verificado por grep sobre `CMakeLists.txt`/`build.sh`/`*.nix`/`*.py`). Ningún binario ni ruta de Windows cambia. |
| `gow3_pc/tests/test_runtime.c`, `tests/test_sema.c` | 3 stubs de `probe.c` en cada uno | Igual: solo los compila la rama `--test` de POSIX; `test_runtime.c`/`test_sema.c` no aparecen en ningún objetivo CMake de Windows. |
| `gow3_pc/tests/test_http.c` | `#include <unistd.h>` bajo `#ifndef _WIN32` | El include no se compila en Windows; aunque el fichero se compilara algún día allí, `rmdir` sigue declarada por las cabeceras de Win32 como antes. |

No cambió ninguna *lógica* de test: todos los asserts, valores esperados y ficheros de datos
están intactos; los tests que fallaban antes lo hacían en **compilar/enlazar**, no en sus
comprobaciones.

## Riesgos / observaciones para las tareas siguientes

- **Los objetivos de test C++ de CMake están fuera de alcance y sin verificar en Linux.**
  `gpu/CMakeLists.txt` define ~30 ejecutables de test `EXCLUDE_FROM_ALL` (pipeline-cache,
  motion, fiber, ...), que en Windows se compilan a mano con `ninja -C out/gpu <objetivo>`.
  Los que hacen `target_link_libraries(... gow3gpu)` chocarán en POSIX con el mismo
  problema de link «la librería compartida necesita todos los símbolos `runtime_*`» que esta
  tarea arregló para los tests C (ahora mismo no enlazan objetos del runtime). La Tarea 14
  («añadir tests específicos de plataforma y ejecutarlos en el flujo `build.sh --test`»)
  debería decidir la política: mantener la rama `--test` como ejecutor canónico de los
  tests C y extenderla, o añadir un equivalente para los objetivos C++.
- **La ancla `__ImageBase` es por binario.** En el probe real vive en `probe.c` (la imagen
  del cargador); en los tests es un `char` por binario de test. Nada de las rutas probadas
  depende de su posición, así que el stub es seguro; queda anotado por si un test futuro
  ejercita código de la imagen GPU que calcule offsets desde `&__ImageBase`.
- **La optimización `sincosf` de GCC es una mina de portabilidad para el código
  compartido.** Cualquier par `sinf(x); cosf(x)` nuevo en una unidad de traducción
  `-O2 -Werror` que se enlaza sin `-lm` fallará igual. Todas las líneas de link con
  matemáticas de la rama `--test` llevan ahora `-lm`.
- **El testeo *headless* funciona.** `pad-test` demostró que el driver dummy de SDL3 + el
  mando virtual bastan para los tests de mando — las Tareas 11-13 (arranque/reproducción)
  seguirán necesitando pantalla y dispositivo Vulkan reales, pero el trabajo a nivel de
  unidades no necesita interfaz gráfica.
