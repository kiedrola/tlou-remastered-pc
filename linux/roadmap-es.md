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

El código ya está protegido con `#ifdef _WIN32` / `#ifndef _WIN32` (el shim `compat_win.c`/`.h` solo actúa en Windows). Hay que limpiar las suposiciones restantes de Windows en las herramientas y el launcher.

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

- [ ] **Tarea 8 — Prioridad: Media | Esfuerzo: 3 h**
  **Evidencia:** `launcher/build-starters.sh` (solo Windows: `windres`, `-Wl,--subsystem,windows`, `-nostdlib`).
  Crear el punto de entrada del launcher para Linux: un runner nativo (binario ligero o script ejecutable) que sustituya a `TLOU Game.exe`, y adaptar `build-starters.sh` o crear un `build-linux.sh` que no use `windres`.

- [ ] **Tarea 9 — Prioridad: Media | Esfuerzo: 2 h**
  **Evidencia:** `launcher/tlou_launcher.pyw` (app Tkinter de Windows) y `tools/copy_python.py`.
  Asegurar que el launcher Tkinter funciona con el Python de Linux (el `.pyw` debe pasarse a `python3`; verificar los imports de Pillow/Tkinter en el entorno Linux).

- [ ] **Tarea 10 — Prioridad: Baja | Esfuerzo: 2 h**
  **Evidencia:** `gow3_pc/gpu/shim/window.cpp` y `gow3_pc/gpu/shim/sdl_window.h` (SDL3, X11/Wayland).
  Verificar los iconos de ventana/recursos entre plataformas (el BMP cargado con `SDL_LoadBMP` frente a los recursos de Windows) y la ruta de la fuente del overlay (`GOW3_FONT_PATH` en `CMakeLists.txt`).

---

## Fase 3 — Pruebas

- [ ] **Tarea 11 — Prioridad: Alta | Esfuerzo: 4 h**
  **Evidencia:** `tools/probe_tlou.py` (--boot, --check-imports, --cpu-only, --capture); `tools/tlou_pad.py`, `tools/tlou_bench.py`.
  Ejecutar `python3 tools/probe_tlou.py --boot --seconds 0 --game <ruta al juego>` con una copia real del juego y su actualización 1.11; confirmar que el runtime arranca, carga la caché de shaders y entra en el juego.

- [ ] **Tarea 12 — Prioridad: Alta | Esfuerzo: 6 h**
  **Evidencia:** sección *Status* de `README.md` (regla de "jugable de principio a fin") y `gow3_pc/README.md` (variables de diagnóstico como `GOW3_FRAME_STATS`, `GOW3_GPU_CHECKPOINTS`).
  Verificar al menos una sesión completa sin pérdida de dispositivo, grabando la entrada del mando (`GOW3_PAD_RECORD`) para poder reproducir los fallos.

- [ ] **Tarea 13 — Prioridad: Media | Esfuerzo: 4 h**
  **Evidencia:** `tools/tlou_replay.py`, `tools/tlou_compare_shots.py`.
  Reproducir una ejecución grabada en Linux para confirmar que el bucle de replay (la base para depurar pérdidas de dispositivo) funciona con el binario nativo.

- [ ] **Tarea 14 — Prioridad: Media | Esfuerzo: 3 h**
  **Evidencia:** `gow3_pc/tests/` (tests unitarios multiplataforma).
  Añadir, si no existen, tests específicos de plataforma (ruta del binario, resolución de rutas de fichero POSIX) y ejecutarlos en el flujo de `build.sh --test`.

- [ ] **Tarea 15 — Prioridad: Media | Esfuerzo: 3 h**
  **Evidencia:** sección *Requirements* de `README.md` (AVX/SSE4.2/POPCNT; Vulkan 1.3; 4 GiB VRAM).
  Registrar el rendimiento y la estabilidad por tipo de GPU (NVIDIA pre-Turing, AMD, Intel) en Linux, en particular los puntos ya documentados para Windows (solver de tela `Cs_ClothSimulate512`, barriers en `if`, `GOW3_RTZ`).

---

## Fase 4 — Empaquetado / distribución para Linux

- [ ] **Tarea 16 — Prioridad: Alta | Esfuerzo: 4 h**
  **Evidencia:** `tools/package_windows.sh` y `tools/package_check.py`; `gow3_pc/packaging/windows/`.
  Definir la estructura del paquete para Linux (runtime + `libgow3gpu.so` + launcher + runner del juego + scripts + Python), con un punto de entrada claro (`./TLOU` o similar) y sin dependencias de MSYS2.

- [ ] **Tarea 17 — Prioridad: Media | Esfuerzo: 3 h**
  **Evidencia:** sección *Playing* de `README.md` (flujo de arranque) y `tools/package_windows.sh` (copia del Python embebido).
  Documentar en `linux/README.md` las instrucciones de instalación y arranque para Linux (dependencias, variables de entorno comunes `GOW3_*`/`TLOU_*`, ubicación de `analysis/tlou-user`).

- [ ] **Tarea 18 — Prioridad: Baja | Esfuerzo: 2 h**
  **Evidencia:** `.github/` (posibles workflows de CI) y `tools/release-notes/`.
  Añadir, si procede, un workflow de CI/build para Linux y release notes del paquete para Linux.

---

## Limitaciones conocidas y suposiciones

- **Aún no se ha verificado ningún build/arranque real en Linux.** Todo lo anterior se ha inferido del código; la rama `linux-support` era idéntica a `main` (sin commits nuevos). Las estimaciones asumen un entorno con las dependencias de sistema ya disponibles. *(Actualizado: la Tarea 1 verificó el build, la Tarea 2 las dependencias de sistema, la Tarea 3 los tests unitarios (`build.sh --test`, los 10 pasando) la Tarea 4 la generación de host shaders y la compilación de la librería GPU (build limpio fuera del árbol, sin dependencia de Windows en la ruta POSIX, sin cambios de código) la Tarea 5 el arranque del binario nativo por la herramienta (`--check-imports` en vivo con una copia real de `CUSA00552` 01.00 + update 01.11, `Imports: 0 missing`) y la Tarea 6 la guarda Windows-only de la inyección del PATH de MSYS2 (`PATH` idéntico byte a byte en Windows simulado; `PATH` ya no ensuciado en Linux), y la Tarea 7 el paquete de jugador para Linux (`tools/package_linux.sh`, 876 ficheros, 17 MiB; `--check-imports` en vivo desde una extracción limpia con el Python embebido, `Imports: 0 missing`; el launcher Tkinter abre una ventana real) — todo en Arch Linux, ver `task-01.md` / `task-02.md` / `task-03.md` / `task-04.md` / `task-05.md` / `task-06.md` / `task-07.md`).*
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
- [ ] `python3 tools/probe_tlou.py --boot` arranca el juego con una copia real de `CUSA00552` + actualización 1.11.
- [ ] El juego entra en el menú principal y se puede jugar una sesión completa sin pérdida del dispositivo Vulkan.
- [ ] La ventana se crea vía SDL3 (X11 o Wayland) y se muestra la imagen del juego.
- [ ] El mando (DualShock 4 / Xbox) y el teclado/ratón reciben la entrada; puede grabarse y reproducirse (`GOW3_PAD_RECORD` / `tlou_replay.py`).
- [ ] El launcher/runner para Linux arranca y aplica la resolución, el límite de fotogramas y el idioma desde `tlou.ini`.
- [ ] El paquete para Linux (`package_linux.sh`) genera una carpeta/binario ejecutable sin ficheros del juego y sin depender de MSYS2.
- [ ] La documentación (`linux/README.md` y `README.md`) indica la instalación y el arranque para Linux.
