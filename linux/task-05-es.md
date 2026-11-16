# Tarea 5 — Binario de runtime según el SO en `tools/probe_tlou.py` (Fase 2)

> **Estado: COMPLETADA Y VERIFICADA.** `game_executable()` ahora elige el binario del
> runtime según el SO: `gow3-probe.exe` en Windows (`sys.platform == "win32"`),
> `gow3-probe` en POSIX. Verificado de tres formas en Arch Linux: comprobaciones
> unitarias de la función (plataforma real, Windows simulado, error por binario
> ausente), una prueba antes/después con el cambio en *stash*, y una ejecución
> end-to-end en vivo con una copia real de `CUSA00552` 01.00 + update 01.11: la
> herramienta preparó el juego, construyó el conjunto de parches y lanzó el
> `gow3-probe` nativo, que completó `--check-imports` con `Imports: 0 missing` y
> código de salida 0.

## El cambio

`tools/probe_tlou.py`, `game_executable()` (la única referencia al binario en el
archivo; su único consumidor es el comando de arranque,
`command = [str(game_executable()), ...]`, que se pasa a `subprocess.Popen`):

```python
def game_executable():
    name = "gow3-probe.exe" if sys.platform == "win32" else "gow3-probe"
    built = ROOT / "gow3_pc/out" / name
    if not built.is_file():
        raise SystemExit(f"Build the runtime first (gow3_pc/build.sh): {built} is missing")
    return built
```

Una línea añadida; la comprobación de existencia y el mensaje de `SystemExit` no
cambian.

## Entorno de verificación (Arch Linux)

Misma máquina que las Tareas 1–4: x86_64, Python 3.14.8, runtime construido por la
Tarea 1 (`gow3_pc/out/gow3-probe`, ELF de 1,7 MB, `ldd` limpio, `--vulkan-only` PASS).

- **CPU:** AMD FX-8350 Eight-Core (Piledriver). Según `/proc/cpuinfo` tiene SSE4.2,
  POPCNT, AVX, F16C, LZCNT y BMI1, pero **le falta MOVBE** (ver Riesgos: esta máquina
  no puede decodificar el código del juego; afecta a la Tarea 11, no a esta).
- **Juego real:** `/roms-ps4/Roms/PS4/The Last of Us Remastered/CUSA00552-1.00`
  (APP_VER 01.00) con su update `CUSA00552-patch-1.11` (APP_VER 01.11, comprobado con
  el propio `prepare.sfo` del proyecto). Accesible mediante los enlaces simbólicos
  `../CUSA00552` y `../CUSA00552-patch` del espacio de trabajo: el valor por defecto de
  `--game` es `ROOT.parent / "CUSA00552"`, y `args.game.resolve()` sigue el enlace hasta
  `CUSA00552-1.00`.

## Verificación en vivo

1. **Antes/después con el cambio en *stash*** (`git stash push tools/probe_tlou.py`):
   - **Sin el cambio:** la herramienta ejecuta toda la preparación (detección del
     update, vista de arranque, `prepare.py`/`link_modules.py`/`content_profile.py`,
     etapa red-zone/ISA) y entonces se detiene: `Build the runtime first
     (gow3_pc/build.sh): <repo>/gow3_pc/out/gow3-probe.exe is missing` — no puede
     avanzar en absoluto en Linux.
   - **Con el cambio:** continúa hasta lanzar el runtime (punto 3). El *stash* se
     restauró; el árbol de trabajo lleva exactamente este cambio de una línea más el
     `-dirty` previo del submódulo `fsr-vulkan` (build.sh aplica ahí los parches del
     proyecto, Tarea 1).
2. **Comprobaciones unitarias** (importando el módulo, sin juego):
   - Linux: `game_executable()` devuelve `<repo>/gow3_pc/out/gow3-probe` y el archivo
     existe (artefacto de la Tarea 1).
   - Windows simulado (`sys.platform` fijado temporalmente a `"win32"`, restaurado en
     un `finally`): la función elige `gow3-probe.exe`; aquí lanza el `SystemExit` con
     exactamente la ruta de antes del cambio en el mensaje, lo que prueba que la rama
     Windows resuelve el mismo `Path`.
   - Binario ausente (`ROOT` apuntando a una carpeta vacía): `SystemExit` con el
     mensaje `Build the runtime first`.
3. **End-to-end con el juego real** — el modo por defecto (sin `--boot`), que es
   exactamente el flujo `--check-imports` de la herramienta: prepara el juego,
   calcula los parches, construye el comando y lanza el runtime; el modo lista los
   imports de PS4 que el runtime no provee y **nunca decodifica ni ejecuta código del
   invitado**:

   ```
   GOW3_CPU_CHECK=0 TLOU_UPDATE=<.../CUSA00552-patch-1.11> TLOU_ISA_REWRITE=off \
       python3 tools/probe_tlou.py
   ```

   - El `command[0]` del JSON de resultados es `<repo>/gow3_pc/out/gow3-probe`: el
     binario nativo, elegido por la función corregida (con el overlay del update:
     46 ficheros, 7 carpetas).
   - El runtime corrió hasta el final en 1,165 s: ventana SDL3 creada (1632x918),
     NVIDIA GTX 1060 detectada, renderizador Vulkan completamente inicializado (modo
     baja VRAM, caché de pipelines, upscaler), todos los parches del juego aplicados
     (`frame-timeout`, `job-sleep`, `pro-shadows`, `ambient-shadows-half`,
     `all-languages`, `flashlight`, ...) y el propósito del modo:
     `Imports: 0 missing`. `exit_code: 0`.
   - `GOW3_CPU_CHECK=0` hizo falta porque la propia comprobación de CPU del runtime
     se detendría en este FX-8350 sin MOVBE (`STOP: this CPU lacks instruction sets
     the game's code uses: MOVBE ...`); en el modo check-imports es seguro
     precisamente porque no corre código del invitado. Ver Riesgos.

## Decisiones técnicas

1. **Patrón de guarda: `sys.platform == "win32"`** — el patrón que prescribe la regla
   innegociable de Windows del roadmap para Python, y el que ya existe en el
   repositorio (`gow3_pc/scripts/cpu_features.py:77`). El repo también usa
   `os.name == "nt"` en `mods.py`/`game_update.py`, pero para semánticas de
   *rutas/junctions* de Windows, no como guarda de plataforma; el roadmap manda
   `sys.platform` aquí. Ojo: el Python de MSYS2 también reporta `win32`, así que la
   compilación MSYS2 (que produce `gow3-probe.exe`) sigue resolviendo el mismo
   nombre que antes.
2. **Guardar el nombre, no sondear ambos nombres.** Las dos ramas de compilación son
   mutuamente exclusivas por construcción: la rama Windows de `build.sh` (líneas 35–40)
   produce `out/gow3-probe.exe` y la rama POSIX (línea 104) produce `out/gow3-probe`.
   Un único nombre guardado es por tanto exacto, conserva la semántica de error
   existente ("is missing") y no puede elegir un artefacto obsoleto del otro SO.
3. **Alcance limitado a la tarea.** Solo cambió `game_executable()`. La inyección del
   PATH de MSYS2 (líneas 553–554) es de la Tarea 6 y no se tocó — la ejecución
   end-to-end prueba que hoy es *inocua* en Linux (una `C:/msys64/clang64/bin` que no
   existe se antepone simplemente a `PATH` y el runtime arranca y corre igualmente).
4. **Las variables de entorno del end-to-end usaban la interfaz documentada de la
   herramienta, no truquillos:** `TLOU_UPDATE=<carpeta>` porque `find_update` busca
   `<nombre>-patch`/`-update` junto a la ruta *resuelta* del juego (el destino del
   enlace, `CUSA00552-1.00`), mientras que la carpeta de update de esta máquina se
   llama `CUSA00552-patch-1.11` y está junto al enlace; `TLOU_ISA_REWRITE=off` para
   saltarse la puerta de emulación ISA de la herramienta en una CPU sin MOVBE (aquí
   terminaría con error duro, porque la tabla de `iced-x86` que esa puerta necesita es
   una función solo-Windows del loader POSIX — ver Riesgos).

## Garantía de compatibilidad con Windows (prueba)

| Cambio | Efecto en Windows |
|---|---|
| `game_executable()`: `name = "gow3-probe.exe" if sys.platform == "win32" else "gow3-probe"`; `built = ROOT / "gow3_pc/out" / name` | En Windows (CPython nativo y Python de MSYS2 por igual: `sys.platform == "win32"`) `name` es `"gow3-probe.exe"`, y `ROOT / "gow3_pc/out" / "gow3-probe.exe" == ROOT / "gow3_pc/out/gow3-probe.exe"` — el valor `Path` *idéntico* al de antes (verificado: igualdad de `Path` y `str()` igual). La comprobación `is_file()`, el mensaje de `SystemExit` y todos los demás caminos del archivo no cambian. |

No se modificó ningún otro archivo para esta tarea.

## Riesgos / observaciones para las siguientes tareas

- **Tarea 6 (PATH de MSYS2):** la inyección sin guarda está probada como inocua en
  esta ejecución, pero la guarda `sys.platform == "win32"` sigue pendiente según lo
  planificado; en Linux antepone hoy `C:/msys64/clang64/bin` (una ruta relativa que no
  existe) a `PATH`.
- **La Tarea 11 (arranque real) en *esta* máquina lo bloquea la CPU, no la
  herramienta.** La comprobación de CPU del runtime se detiene con `STOP: this CPU
  lacks instruction sets the game's code uses: MOVBE (Intel Haswell 2013 or newer, or
  AMD Ryzen or newer, is needed)`. El camino de emulación ISA es solo-Windows en el
  loader POSIX: `red_zone_table.py` construye la tabla (necesita `iced-x86` en
  `gow3_pc/out/pydeps`, hoy vacío aquí) y la rama POSIX de `probe.c` acepta
  `--red-zone` pero explícitamente no lo aplica (`Red zone: Windows-only protection
  ignored on POSIX`). Por tanto instalar `iced-x86` **no** haría capaz al FX-8350 de
  correr el juego en Linux; la Tarea 11 en esta máquina necesita una CPU con MOVBE o
  una nueva vía de emulación ISA en POSIX. El `GOW3_CPU_CHECK=0` usado para esta
  verificación **no** debe llevarse a la Tarea 11. (En una CPU con MOVBE no se
  necesita nada de esto: `isa_names` queda vacío, la falta de `iced-x86` solo deja
  `red_zone=None` y la ejecución continúa.)
- **Detección automática del update vs. enlaces simbólicos (comportamiento previo,
  idéntico en Windows):** `--game` resuelve enlaces y la búsqueda del sufijo
  `-patch`/`-update` se hace junto al destino resuelto, así que un update guardado
  junto al enlace (como en esta máquina) no se encuentra automáticamente — hace falta
  `TLOU_UPDATE=<carpeta>` (o una carpeta `<nombre>-patch` junto al juego real).
  Relevante para la checklist de la Tarea 11.
- **El resto de `tools/` sigue suponiendo Windows (fuera del alcance de la Tarea 5):**
  `stall_watch.py`, `sample_threads.py`, `profile_rank.py`, `region_map.py`,
  `peek_any.py`, `run_game.py`, `snapshot_guest_textures.py`, `inspect_runtime_audio.py`
  e `inspect_game_audio.py` fijan `gow3-probe.exe` y/o utilidades solo-Windows
  (`tasklist`, `C:/msys64/clang64/bin/llvm-nm.exe`). `tlou_replay.py` y
  `tlou_bench.py` arrancan a través de `probe_tlou.py` (no el binario directamente),
  así que heredan esta corrección; las Tareas 13/15 probablemente necesitarán
  equivalentes POSIX de las herramientas de diagnóstico.
- **Los artefactos de verificación están en `.gitignore`:** la ejecución end-to-end
  escribió en `analysis/tlou` (vista de arranque, `boot-linked.bin`, parches, logs,
  JSON de resultados) y `analysis/tlou-user` — cubierto por `.gitignore`
  (`/analysis/*`), así que el repositorio queda limpio; `git status` muestra solo
  `tools/probe_tlou.py` modificado para esta tarea.
