# Tarea 8 — Los puntos de entrada del launcher en Linux: `TLOU-Launcher`, `TLOU-Game`, `build-linux.sh` (Fase 2)

> **Estado: COMPLETADA Y VERIFICADA.** Los contrapartes en Linux de los starters de Windows
> `TLOU Launcher.exe` / `TLOU Game.exe` existen y son los puntos de entrada del paquete:
> `launcher/TLOU-Launcher` (abre el launcher) y `TLOU-Game` (inicia el juego directamente con
> los ajustes guardados del launcher), más `launcher/build-linux.sh`, que los instala — sin
> compilador y sin `windres`. Verificado en Arch Linux a través de la cadena completa de
> `TLOU-Game` con el juego real `CUSA00552` 01.00 + actualización 1.11 (starter →
> `tools/run_game.py` → `probe_tlou.py --boot` → runtime nativo: los ajustes de `tlou.ini`
> aplicados, la 1.11 superpuesta, la emulación ISA activa, y la única falla SDL: "No
> available video device" — el final de la línea en entorno headless, no del starter), cada
> camino de error, el camino sin terminal (doble clic), el `wait_for_exit` POSIX de
> `apply_update.py` en la topología de procesos real, y una extracción fresca del paquete de
> 879 archivos ejecutando `./TLOU-Game` con su Python incluido, y la **ventana** del
> launcher a través del nuevo starter bajo Xvfb (instalado para esta tarea:
> `sudo pacman -S xorg-server-xvfb`): una ventana Tk real de `The Last of Us Remastered`,
> auto-cerrada con `--close-after`, salida 0 — desde el repositorio y desde una extracción
> fresca con el Python incluido. Lo único que esta máquina sigue sin poder verificar — un
> `--boot` real del juego — está bloqueado por el CPU (sin MOVBE): bajo Xvfb la ejecución
> pasa la ventana de SDL y Vulkan (sobre la GPU NVIDIA real), ejecuta el huésped hasta los
> 64 chunks de PlayGo, y falla ahí — el fallo de las Tareas 5/7, sin cambios.

## El cambio

Tres archivos nuevos:

1. **`launcher/TLOU-Launcher`** (script bash ejecutable) — el contraparte en Linux de
   `TLOU Launcher.exe` (`launcher/tlou-launcher.c`, compilado en Windows por
   `build-starters.sh`). Abre el launcher (`tlou_launcher.pyw`). En el repositorio está
   junto a `tlou_launcher.pyw` (en `launcher/`); en un paquete de release está arriba,
   sobre `launcher/` y `python/`. Encuentra su Python: un paquete trae uno
   (`python/bin/python3`, con Tkinter y Pillow); un checkout del repositorio usa el
   `python3` del sistema; se niega a correr sin Tkinter con un aviso de instalación por
   distribución; los argumentos extra se pasan (`--close-after 5`, pruebas). Desde un
   terminal hace `exec` del launcher (la salida y las fallas van al terminal); sin uno
   (doble clic en el gestor de archivos) la salida va con `tee` a
   `analysis/tlou/launcher-start.log` junto a los datos del jugador y se conserva el código
   de salida — el log es la única huella de un arranque que no llegó al juego, como hace el
   propio juego (el log de su ejecución es `analysis/tlou/launcher-run.log`).
2. **`TLOU-Game`** (script bash ejecutable, arriba del repositorio / arriba del paquete) —
   el contraparte en Linux de `TLOU Game.exe` (`launcher/tlou-game.c`). Inicia el juego sin
   abrir el launcher: ejecuta `tools/run_game.py`, que lee los ajustes del launcher
   (`analysis/tlou-user/launcher.json`) con el propio código del launcher y arranca el
   runtime (`tools/probe_tlou.py`). Su búsqueda de Python imita la del starter C:
   `python/bin/python3` junto a él (el paquete), luego
   `dist/TLOU-Remastered-linux/python/bin/python3` (un repositorio que ya construyó un
   paquete, el espejo de la verificación de `dist\TLOU-Remastered-windows` del starter C),
   luego el `python3` del sistema. Mismo chequeo de Tkinter (el código del launcher que lee
   lo importa), mismo comportamiento terminal/sin-terminal, y log en
   `analysis/tlou/game-start.log` sin terminal.
3. **`launcher/build-linux.sh`** (script bash ejecutable) — el contraparte en Linux de
   `build-starters.sh`: **no compila nada** y no usa `windres`. Los starters C existen para
   esquivar las heurísticas de antivirus sobre binarios Windows pequeños y sin firmar
   (issues #88, #112, #148), algo que no aplica a un script de Linux — el intérprete es el
   Python que trae un paquete o el del sistema. Devuelve el bit ejecutable (un checkout
   hecho en otro lado puede haberlo perdido), chequea la sintaxis de ambos scripts con
   `bash -n` (un starter roto fallaría ante el jugador, no en la compilación), y reporta
   qué necesita el Python de un checkout del repositorio (una nota, no una falla — una copia
   empaquetada trae su Python).

Seis archivos existentes cambiados, todos de forma aditiva y protegida por plataforma
(Windows idéntico a nivel de bytes):

- **`gow3_pc/build.sh`** — la rama POSIX ahora llama a `bash ../launcher/build-linux.sh`
  justo después de construir el runtime, como la rama de Windows llama a
  `build-starters.sh`.
- **`tools/run_game.py`** — `game_running()` conserva la rama Windows de `tasklist`
  tal cual y agrega la rama POSIX: `pgrep -f gow3-probe` — el chequeo propio del launcher
  (Tarea 7), para que starter y launcher coincidan en qué significa "ya está corriendo".
- **`tools/apply_update.py`** — `wait_for_exit()` conserva la rama Windows de
  `ctypes`/`WaitForSingleObject` tal cual y agrega la rama POSIX: un proceso que ya no
  existe ya no responde a una verificación por señal 0 (`os.kill(pid, 0)`;
  `ProcessLookupError` → ya no existe, `PermissionError` → existe pero es de otro usuario;
  se consulta hasta el plazo). La reinicio tras actualizar lanza `TLOU-Launcher` en POSIX y
  `TLOU Launcher.exe` en Windows.
- **`tools/package_linux.sh`** — ahora incluye `tools/run_game.py` (el script del starter
  `TLOU-Game`, como `TLOU Game.exe` en Windows) y los dos starters arriba del paquete, con
  el bit ejecutable.
- **`tools/package_check.py`** — el barrido de rutas privadas ahora cubre `TLOU-Launcher`
  y `TLOU-Game` (ya cubría los dos starters `.exe` de Windows).
- **`launcher/updater.py`** — la lista de Linux de "lo que un paquete debe traer" ahora
  incluye `TLOU-Launcher`, `TLOU-Game` y `tools/run_game.py` (una actualización sin un
  starter instalaría un paquete que el jugador ya no puede abrir).
- **`gow3_pc/packaging/linux/README-player.txt`** — iniciar con `./TLOU-Launcher` (o
  `./TLOU-Game` sin el launcher), más una línea de `chmod +x` para extractores que pierdan
  el bit ejecutable (PT + EN).

¿Scripts y no starters C? Los starters C son un remedio específico de Windows (falsos
positivos de antivirus sobre ejecutables pequeños sin firma que solo hacen `CreateProcess`);
en Linux un script ejecutable es lo ordinario, no necesita compilador ni `windres` (el
requisito de la tarea), y el trabajo pesado (ajustes, entorno, línea de comando) ya era
Python portable — `run_game.py` construye exactamente el comando y el entorno que
construiría el propio botón JOGAR del launcher, usando el propio código del launcher.

## Entorno de verificación (Arch Linux)

La misma máquina de las Tareas 1–7: x86_64, Python 3.14.8 (sistema, con Tkinter y Pillow),
AMD FX-8350 (sin MOVBE — la emulación ISA reemplaza sus 154 usos), NVIDIA GTX 1060. No hay
una sesión de escritorio usable para este agente (`:0` existe pero su autorización X es
privada de sddm), así que la verificación a nivel de ventana corrió bajo Xvfb (instalado
para esta tarea: `sudo pacman -S xorg-server-xvfb`, display privado `:99`); lo demás se
verifica en headless. Juego real: `CUSA00552-1.00` +
`CUSA00552-patch-1.11` a través de los enlaces del workspace (`TLOU_UPDATE` apunta a la
pasta de la actualización), datos del jugador en `analysis/tlou-user/` escritos con la
propia estructura `write_ini`/JSON del launcher (`output_res=1920x1080`, `fps_limit=30`,
`system_language=3`, la pasta del juego en `launcher.json`).

## Verificación (todo en esta máquina)

1. **Instalación.** `bash gow3_pc/build.sh` (rama POSIX) construye el runtime y luego
   ejecuta `build-linux.sh`: `Starters: .../launcher/TLOU-Launcher and .../TLOU-Game (both
   executable)`. Ambos quedan en modo 755 y pasan `bash -n`.
2. **El starter habla el idioma del launcher.** `./TLOU-Game --print` imprime el
   comando/entorno/pastas/conmutadores exactos que `run_game.py` ejecutaría — construidos
   por el propio código del launcher (imports, entorno, `--game` desde la pasta guardada en
   `launcher.json`, todo presente). Con el Python del sistema sacado del `PATH`, cae
   transparentemente a un paquete construido en el repositorio
   (`dist/TLOU-Remastered-linux/python/bin/python3`) — el espejo del
   `dist\TLOU-Remastered-windows` del starter C, verificado en vivo.
3. **La cadena completa, juego real.** `TLOU_UPDATE=... ./TLOU-Game --seconds 0` ejecutó
   starter → `run_game.py` → `probe_tlou.py --boot --seconds 0 --game ...` →
   `out/gow3-probe`: la actualización superpuesta (46 archivos sobre el juego), los ajustes
   de `tlou.ini` aplicados (`Resolution: the game draws at 1920x1080`,
   `system_language=3` aceptado sin aviso, los parches de frame-timeout/pro-shadows/
   ambient activados), la emulación ISA de MOVBE activa (154 instrucciones reemplazadas), y
   la ejecución paró en `Failed to initialize SDL video: No available video device` — el
   final de la línea en headless. La salida del runtime va al log del juego como en Windows
   (`launcher-run.log` / `boot.log`), y el código de salida del runtime se propaga por
   toda la cadena.
4. **Caminos de error, cada uno con mensaje claro y salida no nula:**
   - hay un proceso `gow3-probe` en la tabla de procesos → `The game is already running.
     Close it before starting another one.` (salida 2) — `pgrep` coincide con el launcher;
   - falta `tlou_launcher.pyw` → `TLOU-Launcher: tlou_launcher.pyw was not found ...`;
   - no hay `python3` en ninguna parte (`PATH` vacío de él, sin copia empaquetada) →
     `TLOU-Launcher: Python 3 was not found ...` (salida 1);
   - un Python sin Tkinter → `TLOU-Launcher: ... has no Tkinter. Install it (Arch:
     sudo pacman -S tk; ...) and try again.` (salida 1).
5. **El camino sin terminal (doble clic).** Con el stderr en una tubería (no un tty),
   ambos starters mandan su salida con `tee` a `analysis/tlou/launcher-start.log` /
   `game-start.log` (creadas con sus carpetas) y salen con el código del hijo:
   `TLOU-Game --print` sale 0 con el JSON completo en el log; `TLOU-Launcher` sin pantalla
   usable sale 1 con el `TclError` en el log. Verificado tanto desde el repositorio como
   desde una extracción fresca.
6. **`wait_for_exit` (POSIX), topología real.** El updater **no** es el padre del
   launcher, así que un launcher cerrado es recogido por su padre real (shell/init) y
   nunca se vuelve zombie: verificado con un proceso doble-fork re-adoptado por el PID 1 —
   uno vivo devuelve `False` al agotarse el plazo; uno que muere a los 2 s devuelve
   `True` a los 1.7 s; un pid que ya no existe devuelve `True` de inmediato.
7. **Plomería de la actualización.** Los requisitos Linux de `updater.py` ahora listan
   `TLOU-Launcher`, `TLOU-Game` y `tools/run_game.py` (importado y chequeado en runtime);
   la reinicio tras actualizar apunta a `TLOU-Launcher` en POSIX.
8. **El paquete.** `tools/package_linux.sh` (con su puerta de verificación) construye
   `dist/TLOU-Remastered-linux.zip`: **879 archivos, 17 MiB** (los 876 de la Tarea 7 + los
   tres nuevos), `tools/package_check.py` no reporta archivos de juego ni rutas privadas —
   los starters ya están dentro del barrido de rutas privadas. Los starters conservan su
   modo 755 dentro del zip (verificado por los atributos unix almacenados, `0o100755`).
   Desde una extracción fresca, `./TLOU-Game --print` corre con el Python **incluido** del
   paquete, y `./TLOU-Launcher` falla en headless exactamente como en el repositorio — con
   su `TclError` registrado en `analysis/tlou/launcher-start.log` dentro del paquete.
9. **La ventana, bajo Xvfb.** Con `DISPLAY=:99` (`Xvfb :99`), `./launcher/TLOU-Launcher
   --close-after 8` abrió una ventana real — `xwininfo -root -tree` durante la ejecución
   muestra `0x200078 "The Last of Us Remastered": ("tk" "Tk") 1180x690` — y el launcher se
   cerró solo, salida 0. El shell de este agente no es un tty, así que también corrió la
   rama sin-terminal: el log se creó (vacío — un arranque exitoso no imprime nada) junto a
   los datos del jugador. Lo mismo desde una extracción fresca, con el Python **incluido**
   del paquete: salida 0, log creado dentro del paquete.
10. **`TLOU-Game` no abre ventana del launcher.** Bajo Xvfb la ejecución creó la propia
    ventana SDL del runtime (`Window: Windowed, 1632x918` en el log del juego — no una
    ventana Tk), inicializó Vulkan sobre la GPU NVIDIA real, ejecutó el huésped hasta
    PlayGo (64 chunks instalados), y falló con SIGILL en el offset de huésped 0xcafaa3 —
    la limitación MOVBE del CPU (Tareas 5/7), un paso más allá que la falla headless de
    SDL, sin que el starter haga nada en medio.

## Decisiones técnicas

- **Scripts, no starters C.** La tarea permite "un binario ligero **o un script
  ejecutable**". Los starters C son un remedio para los falsos positivos de antivirus en
  Windows; en Linux el script es lo ordinario, no necesita cadena de herramientas
  (`build-linux.sh` no compila nada y no usa `windres`), y el único comportamiento que
  vale la pena conservar — "un arranque por doble clic que falla deja huella" — son unas
  pocas líneas de bash (`[[ -t 2 ]]`, `tee -a`, `${PIPESTATUS[0]}`).
- **Los starters solo poseen tres cosas:** encontrar su Python (empaquetado → build del
  repositorio → sistema), probar que Tkinter existe (un `python -c` cada uno), y terminal
  vs. sin-terminal. Todo lo demás — los ajustes, el entorno, la línea de comando, el
  chequeo de "ya corriendo" — queda en `run_game.py`/`probe_tlou.py`, de modo que hay un
  solo lugar que construye el arranque del juego, y el modo `--print` del starter es ese
  lugar mostrado.
- **`TLOU-Game` está arriba del repositorio** (como estará arriba del paquete), mientras
  que `TLOU-Launcher` está en `launcher/` en el repositorio y se mueve arriba en el paquete
  (imitando el layout de Windows, donde ambos `.exe` están arriba). Los scripts encuentran
  sus objetivos en ambos layouts, así que una copia de cualquiera de los dos funciona desde
  su carpeta.
- **`pgrep -f gow3-probe`** coincide con el propio `game_running()` del launcher (Tarea 7)
  en lugar de un nombre de proceso, porque el runtime se lanza como `gow3-probe` con
  argumentos; los dos puntos de entrada deben coincidir en "ya está corriendo".
- **`wait_for_exit` por señal 0**, no `waitpid`: el updater no es el padre del launcher,
  así que `waitpid` se lo negaría; un proceso que ya no existe deja de responder a la
  señal 0. En la topología real no pueden quedarse zombies (el padre del launcher lo
  recoge).

## Garantía de compatibilidad con Windows (prueba)

- Ningún archivo de Windows fue tocado: `tlou-launcher.c`, `tlou-game.c`,
  `build-starters.sh`, `tlou-starter.rc/.manifest`, `package_windows.sh` no cambiaron
  (verificado con `git status`).
- Las cuatro ediciones compartidas en Python/shell están protegidas por plataforma y las
  ramas Windows son el código antiguo tal cual: `run_game.py` `game_running()` (el cuerpo
  de `tasklist` no cambió, ahora dentro de `if sys.platform == "win32":`), `apply_update.py`
  `wait_for_exit()` (el cuerpo de `ctypes.windll` no cambió; la rama POSIX solo corre
  cuando `ctypes.windll` no existe), su reinicio (`"TLOU Launcher.exe" if
  sys.platform == "win32" else ...`), y `updater.py` (`REQUIRED`/`ASSET` dentro del
  `if/else` existente — la tupla de Windows es idéntica a nivel de bytes).
- `package_linux.sh` y `package_check.py` son en sí mismos la puerta Linux: ejecutarlos no
  toca el paquete de Windows, y el cambio en `build.sh` está dentro de su rama POSIX (la
  rama de Windows no cambió).

## Riesgos / observaciones para las siguientes tareas

- **Un `--boot` real del juego sigue bloqueado por el CPU** (sin MOVBE; Tareas 5/7): bajo
  Xvfb la cadena está verificada hasta el fallo del huésped (SIGILL en el offset de huésped
  0xcafaa3, después de la ventana de SDL, Vulkan y los 64 chunks de PlayGo) — el fallo es
  del CPU, no del starter. Un CPU con MOVBE (y la pantalla real de una sesión de escritorio,
  en vez de Xvfb) es lo que un jugador necesitaría para jugar; en esta máquina el starter
  está verificado de punta a punta hasta ese punto.
- **La ventana se verificó bajo Xvfb, no una sesión de escritorio** (este agente no tiene
  una usable: la autoridad de `:0` es privada de sddm). Todo lo que hace el starter es
  ajeno a la pantalla (solo encuentra un Python, chequea Tkinter, y hace `exec` o `tee`);
  el único riesgo X restante es cosmético (decoraciones del gestor de ventanas, ícono), no
  funcional.
- **`pgrep -f` coincide líneas de comando:** un shell cuya propia línea de comando
  contiene la cadena literal `gow3-probe` (andamios de pruebas) cuenta como "el juego está
  corriendo". El flujo del jugador nunca tiene esa cadena en la línea de comando de ningún
  ancestro; misma advertencia que el chequeo propio del launcher (Tarea 7).
- **El bit ejecutable vive en el zip:** `unzip` (y la mayoría de las herramientas) lo
  restaura — verificado; un extractor que lo pierda queda cubierto por la línea de
  `chmod +x` del README. El paquete no trae `build-linux.sh` (es una herramienta de
  compilación, como `build-starters.sh` no está en el paquete de Windows).
- **Nota para la Tarea 9+:** `TLOU-Launcher`/`TLOU-Game` dentro del paquete resuelven la
  pasta del juego y la actualización a través de `launcher.json`/`TLOU_UPDATE` exactamente
  como el launcher; cualquier tarea que cambie el formato de esos archivos debe actualizar
  `run_game.py` (y queda cubierta por `--print`).
