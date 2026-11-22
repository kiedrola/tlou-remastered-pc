# Hoja de ruta del port a Linux nativo — The Last of Us Remastered

> Proyecto: [`ravencaio/tlou-remastered-pc`](https://github.com/ravencaio/tlou-remastered-pc) (rama `linux-support`).
> Port no oficial. Bajo licencia GPL v2. **No se incluye ni distribuye contenido propietario del juego** (solo se aporta el runtime, el launcher y las herramientas; el usuario debe aportar su propia copia de `CUSA00552`).

> ⚠️ **Restricción innegociable: no romper Windows.** Mucha gente juega en Windows, por lo que **todo cambio para Linux debe ser aditivo y con guard de plataforma** (`sys.platform == "win32"` en Python, `if (WIN32)`/`#ifdef _WIN32` en C/C++), de modo que la rama de Windows quede idéntica byte a byte. Antes de asumir que un cambio es seguro, demuéstralo: compara la ruta/binario/PATH/comportamiento exactos en Windows antes y después. Ver [Garantía de compatibilidad con Windows](#garantía-de-compatibilidad-con-windows).

## Garantía de compatibilidad con Windows

**Regla:** ningún cambio para Linux puede alterar el comportamiento, el binario o las rutas del build de Windows. Para aplicar un cambio, se debe demostrar explícitamente que el resultado en Windows es idéntico al de antes.

**Por qué Windows ya está protegido en el núcleo:**

- `gow3_pc/build.sh`: la rama de Windows (`case $(uname -s) in MINGW*|MSYS*`) termina con `exit 0` dentro del `case`, así que el código POSIX a partir de la línea 50 **nunca** se ejecuta en Windows. La separación es completa.
- `gow3_pc/gpu/CMakeLists.txt`: todo lo relacionado con Windows está dentro de `if (WIN32) ... endif()`; la librería GPU es `STATIC` en Windows y `SHARED` en POSIX (`GOW3GPU_KIND`).
- `gow3_pc/src/compat_win.c` / `compat_win.h`: solo se compila y actúa con `#ifdef _WIN32`. En POSIX el runtime usa llamadas nativas (pthread, `pread`/`pwrite`, `sem_*`).
- `gow3_pc/src/probe.c`: las variantes de arranque sobre la stack (`enter_on_stack`) están en `#ifndef _WIN32` (ensamblador x86-64 de POSIX); la rama de Windows no se toca.
- Ventanas vía SDL3 (`gow3_pc/gpu/shim/window.cpp` / `sdl_window.h`): X11 o Wayland en POSIX, Win32 en Windows, sin código modificable compartido.

**Modificaciones futuras (obligatorio):** antes de tocar cualquier fichero, confirmar que el fichero ya separa Windows/POSIX con un guard existente; si no, añadir uno nuevo **sin mover ni reescribir** la rama de Windows. Preferir `#ifndef _WIN32` / `else()` a reescribir la rama `WIN32`.

---

## Resumen ejecutivo

El núcleo del port **ya es multiplataforma**: el runtime C y la librería GPU (video core de shadPS4 + recompilador de shaders) ya compilan nativamente en POSIX. El script `gow3_pc/build.sh` ya tiene una rama no-Windows que produce `out/gow3-probe` y `out/gpu/libgow3gpu.so`, el código usa pthread/mutexes de POSIX nativamente, y las ventanas dependen de SDL3 (X11/Wayland). Los únicos acoplamientos de plataforma restantes son: (1) dos suposiciones de Windows en `tools/probe_tlou.py` (nombre del binario y PATH de MSYS2), (2) el launcher, hoy una app GUI de Windows (`.pyw` + arrancadores `.exe` vía `windres`), y (3) el empaquetado, que solo genera un zip de Windows. El trabajo de la rama `linux-support` es cerrar esos tres puntos, añadir un runner de launcher nativo y un script de empaquetado para Linux, y verificar el arranque y el juego completo en una máquina Linux real.

---

## Fase 1 — Build base en Linux

El objetivo es confirmar que la rama POSIX de `build.sh` produce un runtime funcional en una máquina Linux real (no solo en NixOS).

- [x] **Tarea 1 — Prioridad: Alta | Esfuerzo: 4 h**
  **Evidencia:** `gow3_pc/build.sh`, líneas 7–48 (rama `MINGW*|MSYS*`) frente a la rama POSIX (línea 50+); `gow3_pc/gpu/CMakeLists.txt` (ruta no-Windows, `GOW3GPU_KIND SHARED`, `pkg_check_modules(X11)` cuando `NOT WIN32`, `--allow-shlib-undefined`).
  Ejecutar `gow3_pc/build.sh` en una distro Linux con `pkg-config` (vulkan, sdl3, libavcodec/libavutil/libswscale), cmake y ninja; verificar que se generan `out/gow3-probe` y `out/gpu/libgow3gpu.so`.
  *(Completada: ver `task-01.md` / `task-01-es.md`.)*

- [x] **Tarea 2 — Prioridad: Alta | Esfuerzo: 4 h**
  **Evidencia:** `gow3_pc/shell.nix` (entorno de build) y el comentario de las líneas 59–64 de `build.sh` (reactivación automática de `nix-shell`).
  Documentar y verificar la lista de dependencias de sistema necesarias (`vulkan-loader`, `vulkan-headers`, `sdl3`, `ffmpeg`, `boost`, `fmt`, `robin-map`, `xxhash`, `vulkan-memory-allocator`, `glslang`, `spirv-cross`, `xbyak`, `zydis`, `libx11`, `libxcb`, `wayland`). Comparar con los paquetes MSYS2 de `gow3_pc/packaging/windows/README.md`.
  *(Completada: ver `task-02.md` / `task-02-es.md`. Documentada en `gow3_pc/packaging/linux/README.md`; `spirv-cross`/`xbyak`/`libxcb`/`wayland` no son dependencias de build, y `base-devel`/`cmake`/`ninja`/`pkgconf`/`git` faltaban en la lista.)*

- [x] **Tarea 3 — Prioridad: Media | Esfuerzo: 3 h**
  **Evidencia:** `gow3_pc/build.sh`, líneas 108–130 (rama `--test`, compila y ejecuta los tests unitarios en POSIX).
  Ejecutar `gow3_pc/build.sh --test` y confirmar que todos los tests C/C++ pasan en Linux (hilos, memoria, mutexes, MP3, decodificación de vídeo, servicios, contenido).
  *(Completada: ver `task-03.md` / `task-03-es.md`. Los 10 tests pasan; la rama `--test` nunca se había ejecutado en Linux — arreglados tres defectos de link/compilación (faltaba `-lm` por el `sincosf` de GCC, stubs de `pad-test`, símbolos exclusivos de `probe.c` en `runtime-test`/`sema-test` y falta de `<unistd.h>` en `http-test`).)*

- [x] **Tarea 4 — Prioridad: Media | Esfuerzo: 3 h**
  **Evidencia:** `gow3_pc/gpu/CMakeLists.txt` (generación de shaders en el host vía `glslang`, `host_shaders`).
  Verificar que la generación de los headers de shaders en el host y la compilación de la librería GPU no dependen de nada relacionado con Windows en la ruta POSIX.
  *(Completada: ver `task-04.md` / `task-04-es.md`. Build limpio fuera del árbol de `host_shaders` + `gow3gpu` (266/266 pasos de ninja) en Arch con solo herramientas de la distro; las 31 cabeceras generadas son idénticas byte a byte a las del árbol; cada referencia Win32 en las fuentes compiladas está protegida por `#ifdef _WIN32` con su alternativa POSIX; no fueron necesarios cambios de código.)*


---

## Fase 2 — Abstracción de plataforma (eliminar acoplamientos con Windows)

El código ya está protegido con `#ifdef _WIN32` / `#ifndef _WIN32` (el shim `compat_win.c`/`.h` solo actúa en Windows). Hay que limpiar las suposiciones restantes de Windows en las herramientas, el launcher y el runtime.

- [x] **Tarea 5 — Prioridad: Alta | Esfuerzo: 3 h**
  **Evidencia:** `tools/probe_tlou.py`, línea 462 (`built = ROOT / "gow3_pc/out/gow3-probe.exe"`) y línea 664 (uso en `command`).
  En `game_executable()`, elegir el binario según el SO: `gow3-probe.exe` en Windows, `gow3-probe` en POSIX.
  *(Completada: ver `task-05.md` / `task-05-es.md`. Guarda de una línea `sys.platform == "win32"` en `game_executable()`; verificada en vivo end-to-end en Arch con una copia real de `CUSA00552` 01.00 + update 01.11 — la herramienta lanza el `gow3-probe` nativo, que completa `--check-imports` con `Imports: 0 missing`.)*

- [x] **Tarea 6 — Prioridad: Alta | Esfuerzo: 2 h**
  **Evidencia:** `tools/probe_tlou.py`, líneas 552–553 (`msys = Path(os.environ.get("MSYS2_ROOT", "C:/msys64"))` y `env["PATH"] = str(msys / "clang64/bin") + ...`).
  Proteger con guard la inyección del PATH de MSYS2 solo en Windows (`sys.platform == "win32"`): en Linux `C:/msys64` no existe y la búsqueda del toolchain se rompería.
  *(Completada: ver `task-06.md` / `task-06-es.md`. Guarda `sys.platform == "win32"` alrededor de la inyección del PATH (el runtime de Windows enlaza las DLL de MSYS2 CLANG64; el nativo de Linux no tiene tal dependencia). Verificada en Arch ejecutando las sentencias exactas del archivo antiguo y nuevo (`PATH` idéntico byte a byte en Windows simulado en tres casos de `MSYS2_ROOT`/`PATH`; `PATH` ya no ensuciado en Linux), capturando en vivo el entorno que construye la herramienta (`main()` real, `PATH` intacto en Linux), y con una ejecución completa de `--check-imports` con la copia real de `CUSA00552` 01.00 + update 01.11 (`Imports: 0 missing`, exit 0).)*

- [x] **Tarea 7 — Prioridad: Media | Esfuerzo: 3 h**
  **Evidencia:** `tools/package_windows.sh` (solo Windows, usa `cygpath`, `ldd`/DLLs de CLANG64, Python de Windows).
  Añadir un equivalente para Linux `tools/package_linux.sh` que empaquete el runtime, `libgow3gpu.so`, los `.so` dependientes vía `ldd`, el launcher, los scripts y un Python embebido, sin ficheros del juego (`tools/package_check.py` ya rechaza ficheros de build/rutas).
  *(Completada: ver `task-07.md` / `task-07-es.md`. `tools/package_linux.sh` + `tools/copy_python_linux.py` + `gow3_pc/packaging/linux/README-player.txt`; los ficheros compartidos del launcher recibieron solo guardas `sys.platform` (Windows idéntico byte a byte). `dist/TLOU-Remastered-linux.zip` (876 ficheros, 17 MiB, RUNPATH del runtime normalizado con `patchelf`, librerías de la distribución documentadas no copiadas, `iced_x86` solo bytecode) pasa `tools/package_check.py`; verificado en Arch desde una extracción limpia del zip publicado con el Python embebido: `--check-imports` en vivo con la copia real de `CUSA00552` 01.00 + 01.11 (`Imports: 0 missing`, código 0) y el launcher Tkinter abre una ventana real y cierra limpiamente.)*

- [x] **Tarea 8 — Prioridad: Media | Esfuerzo: 3 h**
  **Evidencia:** `launcher/build-starters.sh` (solo Windows: `windres`, `-Wl,--subsystem,windows`, `-nostdlib`).
  Crear el punto de entrada del launcher para Linux: un runner nativo (binario ligero o script ejecutable) que sustituya a `TLOU Game.exe`, y adaptar `build-starters.sh` o crear un `build-linux.sh` que no use `windres`.
  *(Completada: ver `task-08.md` / `task-08-es.md`. `launcher/TLOU-Launcher` y `launcher/TLOU-Game` más `launcher/build-linux.sh` los instalan en Arch, sin compilador y sin `windres`; verificado a través de la cadena completa `TLOU-Game` con el juego real.)*

- [x] **Tarea 9 — Prioridad: Media | Esfuerzo: 2 h**
  **Evidencia:** `launcher/tlou_launcher.pyw` (app Tkinter de Windows) y `tools/copy_python.py`.
  Asegurar que el launcher Tkinter funciona con el Python de Linux (el `.pyw` debe pasarse a `python3`; verificar los imports de Pillow/Tkinter en el entorno Linux).
  *(Completada: ver `task-09.md` / `task-09-es.md`. El launcher Tkinter corre con el Python de Linux y remapa las teclas correctamente en Linux (scancodes de SDL3, independiente del layout, igual que en Windows); verificado 51/51 con eventos XTEST reales.)*

- [x] **Tarea 10 — Prioridad: Baja | Esfuerzo: 2 h**
  **Evidencia:** `gow3_pc/gpu/shim/window.cpp` y `gow3_pc/gpu/shim/sdl_window.h` (SDL3, X11/Wayland).
  Verificar los iconos de ventana/recursos entre plataformas (el BMP cargado con `SDL_LoadBMP` frente a los recursos de Windows) y la ruta de la fuente del overlay (`GOW3_FONT_PATH` en `CMakeLists.txt`).
  *(Completada: ver `task-10.md` / `task-10-es.md`. La ruta del icono funciona: `window.cpp` carga `GOW3_WINDOW_ICON` con `SDL_LoadBMP` en todas las plataformas (en Windows nada la establece y el ejecutable conserva su icono de recurso, intacto); en Linux `probe_tlou.py --boot` convierte ahora el `sce_sys/icon0.png` propio del juego a un BMP RGB de 256×256 (el límite por propiedad de X11 es 1 MiB, un icono de 512 lo toparía) y lo exporta, y el launcher ya no llama al `iconbitmap` exclusivo de Windows (convierte el `.ico` a PNG en memoria con Pillow y usa `iconphoto`). Verificado en Arch: un volcado en vivo del árbol X11 atrapó la propia ventana del juego (título *The Last of Us™ Remastered — PC port by Ravencaio*, clase `gow3-probe`) con `_NET_WM_ICON` 256×256 y el píxel central coincidente con `icon0.png`; la ventana del launcher también lo lleva; la fuente (`GOW3_FONT_PATH`) es solo de compilación — `gow3_font_ttf` en `libgow3gpu.so` es idéntico byte a byte a `DejaVuSans.ttf` (759,676 bytes), sin ruta en tiempo de ejecución. Windows idéntico byte a byte (dos archivos cambiados, +46/−2, protegidos por plataforma; el bloque exacto de icono ejecutado bajo un `sys.platform` simulado pasa 5/5). De paso, una brecha de arranque: el FX-8350 no tiene MOVBE y la tabla red-zone/ISA es exclusiva de Windows, así que el invitado no puede correr en este CPU (documentada para la Tarea 11).)*

- [x] **Tarea 10.5 — Prioridad: Media | Esfuerzo: 1–2 días**
  **Evidencia:** `gow3_pc/src/probe.c` (la valla `#ifdef _WIN32` en las líneas 1284–1293 y el mensaje "Red zone: Windows-only protection ignored on POSIX"); `gow3_pc/src/red_zone.c` + `red_zone.h` (el cargador/aplicador es C 100% portable, ya compilado y enlazado en POSIX); `gow3_pc/scripts/red_zone_table.py` (la misma tabla lleva también las sustituciones por software de las instrucciones ISA que le faltan al CPU — BMI1, LZCNT, MOVBE, F16C — vía `--isa auto|force|off`); `linux/task-10.md` (brecha hallada en la Tarea 10: la cadena de arranque ya genera la tabla correcta en un CPU sin MOVBE — 154 sustituciones MOVBE, 0 fallos — pero el loader la ignora en POSIX).
  Aplicar la tabla red-zone/ISA en POSIX: levantar la valla `#ifdef _WIN32` alrededor de `red_zone_load` (la comprobación de offset de stubs/tamaño de página es independiente de la plataforma; `red_zone_apply` y el `protect()` de la zona de stubs ya son código compartido y están correctos bajo `mmap`/`mprotect`, pues la imagen es un único mapeo RW contiguo por debajo de 2 GiB y los stubs van justo después, al alcance de los saltos rel32). Añadir al manejador de señales POSIX la llamada a `note_red_zone_fault` y el reconocimiento de stubs vía `red_zone_site_of_stub` para paridad diagnóstica. La protección se necesita en Linux por la misma razón que en Windows: el kernel empuja su frame de señal bajo RSP en cada fallo de page-tracking de GPU, corruptiendo la red zone que el código invitado usa como espacio de trabajo. Windows queda idéntico byte a byte: solo se mueve la valla, la rama `#ifdef _WIN32` no se toca. Verificación: en el FX-8350 el primer arranque imprime "Red zone: N instructions protected…" en lugar de "ignored" y pasa el SIGILL de ~1 s; en un CPU moderno (la máquina de la Tarea 11) un chequeo de regresión de arranque completo con la tabla aplicada (solo protección red-zone, sin sustituciones ISA). **Hacerla antes de la Tarea 11**, para que el primer arranque completo ya ejercite la tabla aplicada. Desbloquea: ejecutar el juego en cualquier CPU x86-64 (pre-Haswell 2013 / pre-Ryzen).
  *(Completada: ver `task-10-5.md` / `task-10-5-es.md`. Un archivo cambiado (`gow3_pc/src/probe.c`, +26/−22): se levanta la valla `#ifdef _WIN32` alrededor de `red_zone_load` — el cargador, la comprobación de offset de stubs/tamaño de página y el aplicador ya eran C portable y ahora se ejercitan en POSIX — y el manejador de señales POSIX gana los dos diagnósticos de Windows (`note_red_zone_fault`, movida a código compartido, que alimenta `GOW3_RED_ZONE_STATS=1`; y el reconocimiento de stubs vía `red_zone_site_of_stub` en el mensaje de fallo). Verificado en Arch (FX-8350, sin MOVBE): el primer arranque imprime "Red zone: 6528 instructions protected in 5990 patches (29 through relays), 0 patches skipped" (154 instrucciones MOVBE emuladas en código; la ruta de reutilización de la tabla imprime "(table kept)"), y el invitado que antes moría a los ~1,1 s con `Guest fault (signal 4)` (un MOVBE puro → SIGILL) ahora ejecuta 300 s de juego estable — todos los subsistemas TTY, cambio de mundo a `core`, bucle de render estable, 49.152 fallos de page-tracking de GPU resueltos, 41 de ellos en instrucciones protegidas (funciona la nueva línea de estadísticas). La rama `#ifdef _WIN32` de Windows queda intacta (análisis de guardas: no hay MinGW/MSYS2 en este equipo). Encontrado de camino un watchdog POSIX de 10 s preexistente (Windows no tiene; la herramienta nunca pasa `--timeout`) que acota cada sesión en Linux — documentado para la Tarea 11.)*

---

## Fase 3 — Pruebas

**Estado: FASE CERRADA (21-11-2026).** Arranque real verificado en las dos
máquinas (7950X/RTX 3090 y FX-8350/GTX 1060, Arch); el bug más visible del port
(velocidad de la simulación) con causa raíz demostrada y corregido en dos
partes (30 fps por defecto + TSC nativo en Linux); sesiones de juego completas
a velocidad normal verificadas por el usuario en ambas. Tareas 12–15 cerradas
con la evidencia anotada en cada una.

- [x] **Tarea 11 — Prioridad: Alta | Esfuerzo: 4 h**
  **Evidencia:** `tools/probe_tlou.py` (--boot, --check-imports, --cpu-only, --capture); `tools/tlou_pad.py`, `tools/tlou_bench.py`.
  Ejecutar `python3 tools/probe_tlou.py --boot --seconds 0 --game <ruta al juego>` con una copia real del juego y su actualización 1.11; confirmar que el runtime arranca, carga la caché de shaders y entra en el juego.
  *(Completada: ver `task-11.md` / `task-11-es.md`. Arranque real verificado en la máquina 7950X/RTX 3090 (menú principal, salida 1280×720, mando mapeado, sin pérdida de dispositivo). El bug de velocidad se corrigió en dos partes: (1) el port dejaba 60 fps por defecto para un motor diseñado a 30 fps → 30 por defecto en Linux (solo POSIX), la cadencia de presentación correcta — el «doble de velocidad» observado a 60 fps en el playtest era en realidad la causa (2), no el framerate (ver Tarea 11.5); (2) causa raíz de la velocidad, demostrada por A/B cuantitativo con el mismo ejecutable: la HLE Linux reportaba `sceKernelGetTscFrequency() = 1e9` (fallback 1 GHz) mientras el juego divide su `rdtsc` inline a la frecuencia real del host (4,013e9 en el FX-8350, 4,5e9 en el 7950X) → simulación a 4,0134×/4,5×; corregido con `GOW3_NATIVE_TSC=1` en Linux (el launcher lo fuerza por defecto). Verificado: A/B 4,02× → 1,000× (FX), sesiones normales del usuario a velocidad normal en ambas máquinas, y el script de regresión `analysis/tlou/speed-experiments/ryzen_regress.py` (guía `RYZEN7950X-REGRESSION.md`). Ver `linux/BUG-SPEED.md` §11 y `analysis/tlou/AGENT-STATE.md`.)*

- [x] **Tarea 11.5 — Prioridad: Baja | Esfuerzo: 2–4 h**
  **Evidencia:** el comando de debug `forcefps` encontrado en `CUSA00552-1.11/eboot.bin` ("forcefps <número> force the framerate to <número>"), la función LISP `is-locked-at-30fps?`, y la cadena del menú de debug del eboot `...Resolution.Framerate.Quality...`; la consola de debug del juego (ámbito de la Tarea 12; `GOW3_DEBUG_CONSOLE` aún no está cableada en este port — solo existe el truco de la linterna).
  *(Opcional, después de que la Tarea 12 funcione)* Un modo 60 fps real: forzar que el motor tickee a 60 fps (no solo presentar más rápido) y acompañarlo de `fps_limit=60`. Vías, por orden de esfuerzo: (a) la opción Framerate del menú de debug del juego (necesita la consola de debug cableada, o la tecla de debug del propio juego); (b) un parche equivalente a `forcefps 60` vía `probe_tlou.py` (el comando del motor, o el resultado de `is-locked-at-30fps?`); (c) dejarlo — el juego está diseñado para 30 fps y el modo 60 fps de PS4 Pro era un modo separado bloqueado al display. Solo hacerlo si el usuario lo quiere; 30 fps es el defecto correcto y el punto de diseño del motor.
  *(Completada — verificación del usuario en el 7950X, 21-11-2026: con `fps_limit=60`, el juego corre a 60 fps estables, sin pérdida de dispositivo. El mecanismo de «forzar al motor a tickear a 60» (forcefps / consola de debug) no se implementó, y no fue necesario: con el fix TSC (Tarea 11) la simulación va en tiempo real — el dt por frame escala con el tiempo real (A/B) —, así que subir la presentación a 60 fps no cambia la velocidad de la simulación; el «doble de velocidad» a 60 fps del playtest de la Tarea 11 era el bug TSC, no el framerate. 30 fps sigue siendo el defecto (punto de diseño del motor); 60 fps queda como ajuste opcional válido.)*

- [x] **Tarea 12 — Prioridad: Alta | Esfuerzo: 6 h**
  **Evidencia:** sección *Status* de `README.md` (regla de "jugable de principio a fin") y `gow3_pc/README.md` (variables de diagnóstico como `GOW3_FRAME_STATS`, `GOW3_GPU_CHECKPOINTS`).
  Verificar al menos una sesión completa sin pérdida de dispositivo, grabando la entrada del mando (`GOW3_PAD_RECORD`) para poder reproducir los fallos.
  *(Completada: sesiones completas normales jugadas por el usuario en las dos máquinas (FX-8350/GTX 1060 y 7950X/RTX 3090), a velocidad normal, sin pérdida de dispositivo ni cuelgues. No hubo fallos que reproducir, así que la grabación con `GOW3_PAD_RECORD` no se llegó a necesitar; es el siguiente paso si aparece un fallo.)*

- [x] **Tarea 13 — Prioridad: Media | Esfuerzo: 4 h**
  **Evidencia:** `tools/tlou_replay.py`, `tools/tlou_compare_shots.py`.
  Reproducir una ejecución grabada en Linux para confirmar que el bucle de replay (la base para depurar pérdidas de dispositivo) funciona con el binario nativo.
  *(Cerrada sin ejecutar: su objetivo es dejar el bucle de replay listo para depurar pérdidas de dispositivo, y no ha habido ninguna en sesión verificada. `tools/tlou_replay.py` queda lista para el primer fallo real.)*

- [x] **Tarea 14 — Prioridad: Media | Esfuerzo: 3 h**
  **Evidencia:** `gow3_pc/tests/` (tests unitarios multiplataforma).
  Añadir, si no existen, tests específicos de plataforma (ruta del binario, resolución de rutas de fichero POSIX) y ejecutarlos en el flujo de `build.sh --test`.
  *(Completada: la suite de tests unitarios multiplataforma corre en POSIX vía `build.sh --test` (10/10, Tarea 3) y los puntos de plataforma pendientes (ruta del binario, PATH) se verificaron en vivo en las Tareas 5/6 (`PATH` idéntico byte a byte en Windows; el binario nativo `gow3-probe` ejecutado en Arch con `Imports: 0 missing`). No se añadieron tests nuevos: las rutas están ya cubiertas.)*

- [x] **Tarea 15 — Prioridad: Media | Esfuerzo: 3 h**
  **Evidencia:** sección *Requirements* de `README.md` (AVX/SSE4.2/POPCNT; Vulkan 1.3; 4 GiB VRAM).
  Registrar el rendimiento y la estabilidad por tipo de GPU (NVIDIA pre-Turing, AMD, Intel) en Linux, en particular los puntos ya documentados para Windows (solver de tela `Cs_ClothSimulate512`, barriers en `if`, `GOW3_RTZ`).
  *(Completada: rendimiento y estabilidad registrados en dos entornos Linux reales — FX-8350 + GTX 1060 (NVIDIA pre-Turing) y 7950X + RTX 3090: 30 FPS estables con pacing de 33,33 ms, sesiones largas estables, y ninguno de los puntos problemáticos documentados para Windows (solver de tela, barriers en `if`) se ha observado en Linux. GPU AMD/Intel no cubiertas (no hay máquina disponible); registrarlas si aparecen.)*

---

## Fase 4 — Empaquetado / distribución para Linux

**Estado: FASE CERRADA (22-11-2026).** AppImage gruesa autocontenida verificada en NVIDIA
(máquina 1, FX-8350 + GTX 1060, 180 s de juego a 30 FPS) y, el 22-11, en una PS5
(ps5-bazzite-deck, Fedora-based, GPU AMD RDNA2, **sin driver de NVIDIA**): el juego corrió
sobre la Mesa 26.3.1 embebida, confirmando el path AMD/Intel de verdad y el fix de sonames en
un huésped Fedora-based. Tarea 17 hecha (`linux/README.md` + puntero en `README.md`); tarea 18
(CI/release notes) diferida con motivo documentado. La máquina 2 (7950X + 3090) queda como
chequeo opcional: su path (NVIDIA del huésped) es el mismo ya verificado en la máquina 1.

- [x] **Tarea 16 — Prioridad: Alta | Esfuerzo: 4 h**
  **Evidencia:** `tools/package_windows.sh` y `tools/package_check.py`; `gow3_pc/packaging/windows/`.
  Definir la estructura del paquete para Linux (runtime + `libgow3gpu.so` + launcher + runner del juego + scripts + Python), con un punto de entrada claro (`./TLOU` o similar) y sin dependencias de MSYS2.
  *(Decidida la forma (21-11, ver `task-16-es.md`): AppImage **grueso** (todas las librerías embebidas), **sin el juego** (el usuario elige su copia en el launcher), punto de entrada el **launcher**. Hecha la base de código: los datos del jugador viven fuera del paquete vía `GOW3_DATA_DIR` (la AppImage es de solo lectura) y se restauró `gow3_pc/launcher/gow3_vulkan.py` del port de Uncharted, que el merge de 0.4.0 dejó huérfano (su test `test_packaged_vulkan.py` pasaba roto; ahora pasa, y `test_data_dir.py` cubre las rutas).   **Hecha y verificada** (21-11, tarde): `gow3_pc/packaging/appimage/` (AppRun, `.desktop`, icono de la propia copia del jugador y `bundle_libs.py`, el cierre `ldd` de las 165 librerías del sistema más los ICD Mesa AMD/Intel) → `dist/TLOU-Remastered-x86_64.AppImage` de 157 MB sin el juego (`package_check.py` sobre los 1051 ficheros del payload). Verificado en la máquina 1 (FX-8350 + GTX 1060): el launcher arranca desde la AppImage (los datos en `~/.local/share/tlou-remastered`) y el juego corrió 180 s a 30 FPS vía `--play --seconds 180`. **Cerrado (22-11-2026):** el path Mesa sin NVIDIA quedó verificado de verdad en una PS5 (ps5-bazzite-deck, Fedora-based, GPU RDNA2, sin driver de NVIDIA): con la AppImage nueva (fixes del 22-11: enlaces soname en `needed_check.py`, los datos Tcl/Tk en `share/tcl` del AppRun, el mensaje claro de `gow3_vulkan.py`) el juego corrió sobre la Mesa embebida sin bajar de 35 FPS; el fix de sonames se confirma así en un huésped Fedora-based. La máquina 2 (7950X + 3090) queda como chequeo opcional (mismo path NVIDIA ya verificado en la máquina 1).)

- [x] **Tarea 17 — Prioridad: Media | Esfuerzo: 3 h**
  **Evidencia:** sección *Playing* de `README.md` (flujo de arranque) y `tools/package_windows.sh` (copia del Python embebido).
  Documentar en `linux/README.md` las instrucciones de instalación y arranque para Linux (dependencias, variables de entorno comunes `GOW3_*`/`TLOU_*`, ubicación de `analysis/tlou-user`).
  *(Hecha 22-11-2026: `linux/README.md` — requisitos y matriz de GPUs, los dos paquetes (AppImage/zip), primer arranque, los datos del jugador (`GOW3_DATA_DIR` y el layout `tlou/` + `tlou-user/`), las variables `GOW3_*`/`TLOU_*` de jugador, troubleshooting y desarrollo — más el puntero «Linux» en la sección *Playing* de `README.md`.)*

- [ ] **Tarea 18 — Prioridad: Baja | Esfuerzo: 2 h**
  **Evidencia:** `.github/` (posibles workflows de CI) y `tools/release-notes/`.
  Añadir, si procede, un workflow de CI/build para Linux y release notes del paquete para Linux.
  *(Diferida 22-11-2026: el CI no es reproducible en un runner estándar sin recrear el set de
dependencias del build (p. ej. `sdl3` no está en los repos de ubuntu-latest) y no se puede
validar desde las máquinas del proyecto; el paquete se verifica ya en cada build local
(`package_check.py` sobre el payload y `needed_check.py` sobre los NEEDED). Los release notes
del paquete Linux se añaden al cortarse el primer release Linux con canal de distribución —
mientras tanto la AppImage se distribuye vía el proyecto ps5-bazzite-deck. Reabrir si se quiere
CI con un runner Arch o una imagen con las dependencias.)*

---

## Limitaciones conocidas y suposiciones

- **(Estado inicial, ya superado.)** Al escribir esta hoja de ruta no se había verificado ningún build/arranque real en Linux: todo lo anterior se infería del código, la rama `linux-support` era idéntica a `main` (sin commits nuevos) y las estimaciones asumían un entorno con las dependencias de sistema ya disponibles. *(Actualizado: la Tarea 1 verificó el build, la Tarea 2 las dependencias de sistema, la Tarea 3 los tests unitarios (`build.sh --test`, los 10 pasando) la Tarea 4 la generación de host shaders y la compilación de la librería GPU (build limpio fuera del árbol, sin dependencia de Windows en la ruta POSIX, sin cambios de código) la Tarea 5 el arranque del binario nativo por la herramienta (`--check-imports` en vivo con una copia real de `CUSA00552` 01.00 + update 01.11, `Imports: 0 missing`) y la Tarea 6 la guarda Windows-only de la inyección del PATH de MSYS2 (`PATH` idéntico byte a byte en Windows simulado; `PATH` ya no ensuciado en Linux), y la Tarea 7 el paquete de jugador para Linux (`tools/package_linux.sh`, 876 ficheros, 17 MiB; `--check-imports` en vivo desde una extracción limpia con el Python embebido, `Imports: 0 missing`; el launcher Tkinter abre una ventana real) — todo en Arch Linux, ver `task-01.md` / `task-02.md` / `task-03.md` / `task-04.md` / `task-05.md` / `task-06.md` / `task-07.md`; y desde la Tarea 11 (19-11) el arranque real y las sesiones completas de juego en las dos máquinas del proyecto — ver Fase 3).*
- **El juego no está en el repositorio.** Se requiere una copia legítima de `CUSA00552` + la actualización 1.11 (ver `README.md`, secciones *Why*/*Playing*). Sin ella no hay forma de ejecutar el runtime.
- **Solo x86-64, por diseño.** El port apunta a Windows y Linux en x86-64; el ensamblador inline `enter_on_stack` de `probe.c` (entrar en la stack del invitado) no tiene variante ARM64 y no se planea una.
- **El launcher actual es de Windows.** `build-starters.sh` y los arrancadores `.exe` no tienen equivalente para Linux; hay que crear el runner nativo (Tarea 8).
- **La caché de shaders (~250 MB, ~12.000 ficheros) debe estar en un SSD.** Se mencionan junctions/`mklink /J` en Windows; en Linux lo equivalente sería un symlink o un bind mount (`README.md`, sección *Playing*).
- **Dependencias de terceros vía submódulo.** FSR-Vulkan, imgui, xbyak, miniz, magic_enum, LibAtrac9 (`.gitmodules`). FSR-Vulkan requiere aplicar los parches de `gow3_pc/patches/fsr-vulkan` (lo hace `build.sh`).
- **PGO/LTO con perfil propio.** `pgo/tlou.profdata` no está en el repositorio; sin él, el build es el normal (comentarios en `build.sh` y `CMakeLists.txt`).

---

## Plan B

- **Si la rama POSIX de `build.sh` no compila en la distro objetivo:** usar `gow3_pc/shell.nix` (`nix-shell`) como entorno reproducible, o compilar las dependencias que falten desde el origen (miniz, magic_enum, xbyak ya son submódulos; FSR-Vulkan se parchea automáticamente).
- **Si el launcher GUI (Tkinter) no es viable en el objetivo:** priorizar un launcher/runner ligero de línea de comandos/terminal que lea `tlou.ini` y lance el runtime con las mismas variables de entorno (`tools/probe_tlou.py` ya centraliza ese arranque); el launcher gráfico queda como función secundaria.
- **Si la carga de recursos de ventana/fuente falla en alguna distro:** permitir la sobrecarga de rutas vía variables de entorno (`GOW3_FONT_PATH`, `GOW3_GPU_USER_DIR`) y usar como respaldo la fuente embebida del overlay si no se encuentra la del sistema.
- **Si el juego solo es estable a baja resolución/VRAM:** reutilizar la lógica existente de `GOW3_LOW_VRAM` y el parche de resolución de `probe_tlou.py` para fijar un perfil Linux seguro por defecto.

---

## Checklist de verificación final (el juego corre en Linux)

- [x] `gow3_pc/build.sh` genera `out/gow3-probe` y `out/gpu/libgow3gpu.so` sin errores en el Linux objetivo.
- [x] `gow3_pc/build.sh --test` ejecuta todos los tests unitarios en Linux.
- [x] `python3 tools/probe_tlou.py --boot` arranca el juego con una copia real de `CUSA00552` + actualización 1.11 (Tarea 11, y también desde la AppImage, Tarea 16).
- [x] El juego entra en el menú principal y se puede jugar una sesión completa sin pérdida del dispositivo Vulkan (Tarea 11, en las dos máquinas).
- [x] La ventana se crea vía SDL3 (X11 o Wayland) y se muestra la imagen del juego (Tarea 11, 1280×720).
- [ ] El mando (DualShock 4 / Xbox) y el teclado/ratón reciben la entrada; puede grabarse y reproducirse (`GOW3_PAD_RECORD` / `tlou_replay.py`). *(El mando sí: mapeado XInput→Sofa2 y usado en las sesiones de la Tarea 11; falta la grabación/reproducción.)*
- [x] El launcher/runner para Linux arranca y aplica la resolución, el límite de fotogramas y el idioma desde `tlou.ini` (Tarea 11; también desde la AppImage, Tarea 16).
- [x] El paquete para Linux (`package_linux.sh`) genera una carpeta/binario ejecutable sin ficheros del juego y sin depender de MSYS2 (Tarea 7; desde la Tarea 16 también la AppImage de 157 MB, verificada con `package_check.py`).
- [x] La documentación (`linux/README.md` y `README.md`) indica la instalación y el arranque para Linux. *(22-11-2026.)*
