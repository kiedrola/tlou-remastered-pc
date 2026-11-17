# Tarea 7 — Paquete de jugador para Linux: `tools/package_linux.sh` (Fase 2)

> **Estado: COMPLETADA Y VERIFICADA.** `tools/package_linux.sh` genera
> `dist/TLOU-Remastered-linux/` y `dist/TLOU-Remastered-linux.zip`: una carpeta que funciona
> en un PC Linux x86-64 con GPU Vulkan y las librerías de la distribución, espejo de
> `tools/package_windows.sh` (mismo layout, misma puerta de `tools/package_check.py`, sin
> ficheros del juego, sin MSYS2). Verificado en Arch Linux ejecutando el runtime empaquetado y
> el launcher empaquetado desde una extracción limpia del zip publicado: `--check-imports`
> con la copia real de `CUSA00552` 01.00 + actualización 01.11 termina con `Imports: 0
> missing`, código 0, y el launcher Tkinter abre una ventana real y cierra limpiamente. Lo
> único que esta máquina no puede verificar — un `--boot` real — lo bloquea la CPU (sin
> MOVBE; ver Riesgos), tal como predijo la Tarea 5, y se reproduce idéntico con el runtime
> del repositorio (sin strip) usando una imagen de guest idéntica byte a byte, lo que
> demuestra que el fallo no lo introduce el paquete.

## El cambio

Tres ficheros nuevos (nada existente cambió salvo dos guardas de plataforma en los ficheros
compartidos del launcher, documentadas abajo):

1. **`tools/package_linux.sh`** — el equivalente Linux de `tools/package_windows.sh`.
   Comprueba los inputs del build (el runtime de la Tarea 1, `iced_x86` en
   `gow3_pc/out/pydeps`, el Python a empaquetar con `tkinter` y `PIL`), prepara el stage,
   hace strip de los binarios, normaliza el `RUNPATH` del runtime con `patchelf` (el build
   deja en él la ruta absoluta de `out/gpu` de esta máquina; `package_check.py` la rechazaría),
   copia solo las librerías compartidas no-sistema que informa `ldd` (en la práctica, solo
   `libgow3gpu.so`; las `libstdc++`/SDL3/Vulkan/loader/códecs de la distribución se
   documentan, no se copian), compila `iced_x86` a solo-bytecode (sus fuentes generados
   disparan falsos positivos de antivirus), empaqueta el Python (intérprete, librería
   estándar, `libpython`, `PIL`) con `tools/copy_python_linux.py`, reúne las licencias de
   terceros y ejecuta `tools/package_check.py`, que tanto rechaza ficheros del juego/rutas de
   la máquina como produce el zip.
2. **`tools/copy_python_linux.py`** — el equivalente POSIX de `tools/copy_python.py`:
   copia el intérprete en curso a `python/bin/python3`, su `libpython` enlazado, la
   librería estándar (saltando tests y piezas no ejecutables), `PIL`/Pillow desde
   `site-packages`, y se niega a continuar sin la extensión `lib-dynload/_tkinter` (el
   launcher es Tkinter).
3. **`gow3_pc/packaging/linux/README-player.txt`** — el README para el jugador (PT/EN) que
   se publica como `README.txt`: no incluye ficheros del juego, los paquetes de la
   distribución necesarios (con la línea para Arch), cómo arrancar el launcher y elegir la
   carpeta del juego, la convención de la actualización 1.11 (`<nombre>-patch` junto al
   juego), la pista del límite de 30 FPS, dónde van los logs, y la licencia.

Dos ficheros compartidos recibieron las guardas de plataforma del roadmap (comportamiento de
Windows idéntico byte a byte):

- **`launcher/tlou_launcher.pyw`** — siete guardas, todas `sys.platform == "win32"` (o
  `!= "win32"`): `SetCurrentProcessExplicitAppUserModelID` (identidad en la barra de tareas
  de Windows), `game_running()` (`tasklist` vs `pgrep -f gow3-probe`), `recycle()`
  (Papelera vs borrar las cachés regenerables de shaders/pipeline), la comprobación de
   instalación (`gow3-probe.exe` vs `gow3-probe`), el `subprocess.Popen` del arranque del
   juego (`creationflags=0x08000000` es solo de Windows y POSIX rechaza un valor no nulo),
   y `open_log()` (`os.startfile` vs `xdg-open`).
- **`launcher/updater.py`** — el asset del release y la lista de "lo que un paquete debe
  contener" pasan a ser por plataforma (`TLOU-Remastered-linux.zip`, `python/bin/python3`,
  `gow3-probe`), y `start_apply()` lanza el instalador de la nueva versión con el
  `python3` empaquetado en vez de `pythonw.exe` más las flags Windows
  `DETACHED_PROCESS`/`CREATE_NEW_PROCESS_GROUP`.

## Entorno de verificación (Arch Linux)

Misma máquina que las Tareas 1–6: x86_64, Python 3.14.8 (sistema, con Tkinter 8.6.16 y
Pillow 12.3.0), AMD FX-8350 (Piledriver — sin MOVBE; ver Riesgos), NVIDIA GTX 1060,
`patchelf` 0.19 (instalado para esta tarea: `sudo pacman -S patchelf`). Runtime compilado
por la Tarea 1. Juego real: `CUSA00552-1.00` + `CUSA00552-patch-1.11` a través de los
symlinks del workspace (como en la Tarea 5, `TLOU_UPDATE` apunta a la carpeta de la
actualización).

## El paquete

`dist/TLOU-Remastered-linux.zip`: 876 ficheros, 54 MiB sin comprimir, 17 MiB comprimidos
(nivel 9).

```
TLOU-Remastered-linux/
  gow3_pc/out/gow3-probe          el runtime (strip, RUNPATH $ORIGIN/gpu)
  gow3_pc/out/gpu/libgow3gpu.so   el núcleo de vídeo (strip; la única librería no-sistema)
  gow3_pc/out/pydeps/iced_x86     la herramienta de red-zone/ISA, solo bytecode (sin .py)
  gow3_pc/scripts/                los scripts de preparación (prepare, link, patches, red zone)
  python/                         Python 3.14.8 + Tkinter + Pillow (RUNPATH $ORIGIN/../lib)
  launcher/                       tlou_launcher.pyw, updater.py
  tools/                          probe_tlou.py, apply_update.py   (sin run_game.py: maneja el
                                   flujo tasklist de Windows y no lo usa el camino Linux)
  licenses/                       las licencias de terceros + THIRD-PARTY.txt, que lista las
                                   librerías de la distribución que enlaza el runtime y su dueño
  README.txt  LICENSE.txt  VERSION
```

Las librerías de la distribución se documentan, no se copian: `THIRD-PARTY.txt` lista cada
objeto compartido que `ldd` informa bajo `/lib`, `/lib64`, `/usr/lib`, `/usr/lib64` con su
paquete Arch dueño, y `README.txt` da la línea de instalación (`sudo pacman -S sdl3 ffmpeg
fmt zydis xxhash libx11 vulkan-icd-loader tcl tk` más un driver Vulkan). Empaquetar un
conjunto de librerías enlazadas contra glibc filtraría las rutas de esta máquina y rompería
entre distribuciones; el paquete Windows publica los DLLs de la toolchain MSYS2 porque
allí no son de la distribución. Tcl/Tk permanecen como dependencia de la distribución (el
paquete `tk`), igual que un Python de sistema normal; el bundle publica solo la extensión
`_tkinter` propia de Python.

## Verificación

1. **Puerta + zip desde el propio script.** `bash tools/package_linux.sh --no-build` ejecuta
   `tools/package_check.py` sobre la carpeta preparada — el mismo comprobador que usa el
   paquete Windows, que rechaza ficheros del juego (`.psarc`/`.sprx`/.../`eboot.bin`/...),
   carpetas prohibidas y cualquier byte de las rutas de esta máquina — y solo entonces
   escribe el zip. Resultado: `876 files, 54 MiB -> 17 MiB`. Volver a ejecutar el
   comprobador sobre la carpeta publicada de forma independiente reproduce la misma lista de
   ficheros.
2. **`--check-imports` desde una extracción limpia, con el Python empaquetado.** Descomprimir
   el zip publicado en una carpeta limpia y ejecutar el intérprete empaquetado:
   `GOW3_CPU_CHECK=0 TLOU_UPDATE=<.../CUSA00552-patch-1.11> ./python/bin/python3
   tools/probe_tlou.py --game <CUSA00552>` → el runtime empaquetado prepara el juego, aplica
   la actualización (46 ficheros) y todos los patches del juego, e informa `Imports: 0
   missing`, `exit_code: 0` (1,6 s). Es el mismo nivel de verificación que establecieron las
   Tareas 5 y 6 — check-imports nunca decodifica ni ejecuta código de guest, por eso
   `GOW3_CPU_CHECK=0` es seguro.
3. **El launcher funciona con el Python empaquetado.** `./python/bin/python3
   launcher/tlou_launcher.pyw --close-after 5` desde la carpeta extraída abre la ventana
   Tkinter real (X11, vía el Tcl/Tk 8.6.16 de la distribución) y cierra limpiamente: 5,6 s,
   código 0, sin traceback. La misma creación de ventana se verificó bajo `env -i`
   (entorno vacío), de modo que no se necesita nada fuera del paquete salvo el display.
4. **El Python empaquetado es relocatable.** `env -i ./python/bin/python3 -c 'import
   tkinter, PIL'` desde la carpeta extraída: `tkinter` 8.6, `PIL` 12.3.0, la librería
   estándar resuelve desde `python/lib/python3.14` (el `RUNPATH` del intérprete es
   `$ORIGIN/../lib`, ajustado por `patchelf`; una copia a un prefijo ajeno importa todo y
   crea una ventana `Tk()`).
5. **Sin restos de la máquina de build.** `patchelf --print-rpath`: `gow3-probe` →
   `$ORIGIN/gpu`, `python/bin/python3` → `$ORIGIN/../lib`. `package_check.py` escanea cada
   fichero de launcher/tools/`gow3_pc` en busca de las rutas de esta máquina (ambas formas
   de barra y la ruta del repositorio) y no encuentra ninguna. Ambos binarios llevan
   `strip --strip-debug`; `gpu/` contiene solo `libgow3gpu.so`.
6. **`iced_x86` viaja solo como bytecode.** 0 ficheros `.py`, 40 `.pyc` bajo
   `gow3_pc/out/pydeps/iced_x86` (las fuentes generados son el disparador conocido de
   falsos positivos de AV), y la etapa de red-zone/ISA del runtime empaquetado sigue
   funcionando con él: al construir la tabla informa `154 instructions replaced
   (movbe_load 20, movbe_store 134)`.

## Decisiones técnicas

1. **`patchelf --set-rpath` para el RUNPATH.** El build de la Tarea 1 enlaza el runtime con
   `-Wl,-rpath,'$ORIGIN/gpu' -Wl,-rpath,"$PWD/out/gpu"` (la entrada absoluta para que un
   build fresco funcione en su sitio). Un paquete publicado no debe llevar `$PWD`, y
   `package_check.py` lo rechaza, así que el script lo sustituye por el único
   `$ORIGIN/gpu` (`--set-rpath`, que también cubre las diferencias `RPATH`/`RUNPATH`).
   `patchelf` es por tanto una dependencia del build (documentada en el mensaje de error
   del script: `sudo pacman -S patchelf`).
2. **Librerías de sistema documentadas, no copiadas.** El bucle de copia toma lo que
   `ldd` informe que no está bajo `/lib`, `/lib64`, `/usr/lib`, `/usr/lib64` o
   `/usr/local/lib` (una librería en falta falla el build con ruido). En un Arch normal eso
   es exactamente un fichero: `libgow3gpu.so`. El resto de la salida de `ldd` va a
   `THIRD-PARTY.txt` con su paquete dueño según `pacman -Qo`. Esto mantiene el paquete
   pequeño (17 MiB frente a los 233 MiB que ocupaba cuando el bucle llegó a copiar las
   librerías de sistema) y honesto sobre lo que el jugador debe instalar.
3. **`iced_x86` solo bytecode.** Su `Code.py` generado (una tabla de 2 MB de
   instrucciones) es marcado por varios antivirus como falso positivo; el paquete lo compila
   con `compileall -b` y borra los `.py` y los stubs `.pyi`, de modo que solo viajan los
   `.pyc` y la extensión nativa `_iced_x86_py`.
4. **`run_game.py` no se incluye.** Envuelve el flujo `tasklist`/`taskkill` de Windows y el
   `gow3-probe.exe` de Windows; el camino de arranque Linux es `probe_tlou.py` (guardado en
   las Tareas 5–6) más el launcher, así que el paquete incluye solo lo que el camino Linux
   ejecuta.
5. **Las guardas del launcher siguen el patrón establecido.** Cada edición es la guarda
   `sys.platform == "win32"` que manda el roadmap (o su negación); el
   `try/except AttributeError` preexistente alrededor de las llamadas `MapVirtualKeyW`
   dejaba ya a los helpers de nombres de teclas seguros en Linux y no se tocó. El
   `recycle()` POSIX borra solo las cachés regenerables de shaders/pipeline del jugador (no
   hay Papelera), y el `game_running()` POSIX compara `gow3-probe` mediante `pgrep -f`.

## Garantía de compatibilidad con Windows (prueba)

| Cambio | Efecto en Windows |
|---|---|
| `tools/package_linux.sh`, `tools/copy_python_linux.py`, `gow3_pc/packaging/linux/README-player.txt` | Ficheros nuevos; nada que Windows lea o ejecute. `tools/package_windows.sh`, `tools/copy_python.py` y `tools/package_check.py` quedan intactos. |
| `launcher/tlou_launcher.pyw` (7 guardas) | Cada rama `win32` contiene las declaraciones originales textualmente intactas (en `game_running()` las dos declaraciones originales solo se reindentan bajo la guarda, como en la Tarea 6); en cualquier Python de Windows (`sys.platform == "win32"`) el código ejecuta las mismas llamadas, en el mismo orden, con los mismos argumentos — incluido `creationflags=0x08000000`. Las ramas no-`win32` son código muerto en Windows. |
| `launcher/updater.py` (2 bloques) | En Windows `ASSET`/`REQUIRED` toman exactamente los valores constantes anteriores (mismas cadenas, misma tupla) y `start_apply()` usa el mismo `pythonw.exe` y las mismas flags `DETACHED_PROCESS | CREATE_NEW_PROCESS_GROUP`. |

En neto: el paquete Windows (`package_windows.sh`), el runtime Windows y el comportamiento
del launcher Windows son byte a byte lo que eran antes de esta tarea.

## Riesgos / observaciones para las tareas siguientes

- **Un `--boot` real no se puede verificar en esta máquina — por la CPU, no por el paquete.**
  Con `GOW3_CPU_CHECK=0` el runtime empaquetado (y el sin-strip del repositorio) atraviesa
  todo el arranque — Vulkan, PlayGo (64 chunks), hasta la transición a la escena `boot1` — y
  entonces falla: `Guest fault (signal 4) at guest offset 0xcafaa3`, de forma determinista,
  antes del primer frame. El mismo comando ejecutado desde el repositorio (binario original,
  `RUNPATH` original, directorio de usuario del repositorio) produce el fallo idéntico en el
  offset de guest idéntico, con una imagen de guest idéntica byte a byte (`boot-linked.bin`
  con el mismo SHA-256), así que el paquete no cambia nada aquí. La causa es la CPU
  anfitriona: el FX-8350 (Piledriver) no tiene MOVBE (la propia comprobación de CPU del
  runtime lo señala: `STOP: this CPU lacks instruction sets the game's code uses: MOVBE`) y
  el código de guest 1.11 alcanza instrucciones que este núcleo no puede decodificar. La
  tabla de red-zone/ISA no es el disparador: el fallo es idéntico con la tabla desactivada
  (`GOW3_RED_ZONE_REWRITE=0 TLOU_ISA_REWRITE=off`), y solo emula BMI1/LZCNT/MOVBE/F16C. La
  Tarea 11 (arranque real) necesita una CPU con MOVBE de generación reciente (Intel Haswell
  o más nueva, cualquier AMD Ryzen) o un camino de emulación de ISA en el loader POSIX; el
  empaquetado en sí no es el obstáculo.
- **La lista `PRIVATE` de `tools/package_check.py` es por-compilador.** Hardcodea el nombre
  de usuario de esta máquina y la ruta del repositorio (el script Windows tiene la misma
  propiedad). Un segundo compilador debe añadir la suya antes de que el comprobador pueda
  certificar su zip; las reglas de ficheros del juego y carpetas prohibidas son
  independientes de la máquina.
- **`apply_update.py` (publicado, camino Linux) no se probó de punta a punta.** Se publica y
  sus imports resuelven con el Python empaquetado, pero el ciclo completo de auto-actualizar
  (descargar el zip del nuevo release, prepararlo, relanzar el instalador con `--wait-pid`)
  necesita un segundo release publicado para probarlo; la Tarea 18 (proceso de release)
  debería ejecutarlo una vez.
- **Tcl/Tk permanecen como dependencia de la distribución.** La extensión `_tkinter`
  empaquetada necesita el `libtcl`/`libtk` de la distribución y sus datos; `README.txt` los
  lista. Si la distribución del jugador los parte de otra forma (Debian: `python3-tk` los
  trae; aquí `tcl tk`), el mensaje de rechazo de `copy_python_linux.py` cita los equivalentes
  de Debian/Fedora.
- **Los artefactos de verificación están gitignored.** Las ejecuciones escribieron en
  `analysis/tlou` / `analysis/tlou-user` (cubierto por `/analysis/*`); `git status` muestra
  solo los ficheros de esta tarea: `tools/package_linux.sh`, `tools/copy_python_linux.py`,
  `gow3_pc/packaging/linux/README-player.txt`, y `launcher/tlou_launcher.pyw` y
  `launcher/updater.py` modificados (más el `-dirty` preexistente del submódulo
  `fsr-vulkan` por los parches de la Tarea 1, y una carpeta `.idea/` no seguida del IDE que
   precede a esta tarea).
