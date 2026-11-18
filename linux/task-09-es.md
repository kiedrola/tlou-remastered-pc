# Tarea 9 — El launcher en Python de Linux: remapeo de teclas y detección de SDL3 (Fase 2)

> **Estado: COMPLETADA Y VERIFICADA.** El launcher Tkinter corre en el Python de Linux (el
> `.pyw` se pasa a `python3` desde `TLOU-Launcher`, Tarea 8; los imports de `tkinter`/Pillow
> existen en el Python del sistema y en el empaquetado) y ahora funciona *correctamente* en
> las dos cosas que no tenían contraparte en Linux: el **remapeo de teclado** y la
> **detección de mandos**. El remapeo era el problema de fondo: el launcher no tenía ningún
> camino de teclas para Linux (`key_from_event` era puro `MapVirtualKeyW` de Windows) y uno
> basado en caracteres sería dependiente del layout (la máquina de prueba es española, donde
> `ñ`/`ç`/acentos muertos imprimen caracteres con acento mientras el SDL3 del juego nombra la
> tecla *física*). El launcher replica ahora el propio algoritmo de mapeo de SDL3 (sus
> drivers de X11 y Wayland construyen el layout de teclas desde
> `xfree86_scancode_table2`, indexado por código de tecla de hardware, con un sobreescrito
> por keysym para teclas que no son caracteres), usando cuatro tablas generadas del propio
> código fuente de SDL3 3.4.20, de modo que un bind hecho en el launcher se resuelve en el
> juego a la misma tecla física en cualquier layout, y coincide con un bind hecho en
> Windows. La pestaña de mandos carga el `libSDL3.so.0` de la distribución (el paquete no
> trae SDL3: es de la distro, la misma librería que carga el runtime del juego). Verificado
> en Arch Linux: un harness de unit tests contra los códigos de tecla X11 en vivo del layout
> español de la máquina (`0 failures`, cada nombre resuelto con el
> `SDL_GetScancodeFromName` real) y un test end-to-end bajo Xvfb con dos displays que inyecta
> eventos de tecla XTEST reales en una ventana Tk en vivo (display `:91`) y una ventana SDL3
> en vivo (display `:92`) y compara el nombre del launcher con el de SDL3 para cada tecla:
> **51/51 coinciden, `BAD: 0`**. Windows byte-identical (el único archivo modificado es
> `launcher/tlou_launcher.pyw`, +166/−13, todo aditivo y con guardas de plataforma).

## El cambio

Un solo archivo del repositorio cambia, `launcher/tlou_launcher.pyw` (+166/−13):

1. **Cuatro tablas de mapeo embebidas** (literales Python estáticos, generados — ver *Cómo
   se hicieron las tablas*). El juego resuelve las entradas de `key_map` con
   `SDL_GetScancodeFromName` (`gow3_pc/src/runtime_pad.c`), así que todo valor es un nombre
   de scancode de SDL:
   - **`LINUX_XFREE86_2`** (175 entradas) — la `xfree86_scancode_table2` de SDL3, la tabla
     que usan sus drivers de X11 *y* Wayland para construir el layout de teclas: índice =
     código de tecla X11 − 8 (el `event.keycode` de Tk en el backend X11) = código de tecla
     de Linux → nombre de SDL. Es la posición física: `25: "W"`, `49: "`"` (la tecla que es
     `` ` ``/`ñ`/masculino según el layout), `125: "Keypad ="`.
   - **`LINUX_KEYSYM_NAMES`** (220 entradas) — el nombre de keysym X que Tk reporta para las
     teclas sin carácter propio (flechas, teclas F, teclado numérico, modificadores,
     multimedia), leído en group 0 / level 0 — el mismo nivel que lee SDL3 — → nombre de
     SDL: `"Next": "PageDown"`, `"KP_Divide": "Keypad /"`, `"XF86AudioMute": "Mute"`.
   - **`LINUX_CHAR_NAMES`** (77 entradas) — el carácter que una tecla imprime → nombre de
     SDL, el respaldo cuando el evento de Tk no trae un código de tecla usable:
     `" ": "Space"`, etc. (la tecla física del `é` no está aquí: se binda por posición).
   - **`LINUX_CHAR_SC`** (53 nombres de scancode) — el conjunto de SDL3 "el keysym de este
     scancode es un carácter", necesario para aplicar la regla de sobreescrito de SDL.
2. **`_linux_backend()`** — `'wayland'` si `WAYLAND_DISPLAY` está definido y `DISPLAY` no,
   `'x11'` en otro caso (un Tk sobre XWayland sigue siendo el backend X11). Solo decide si
   el `event.keycode` de Tk es un código X11 (código Linux + 8) o un código Linux (Wayland
   nativo), es decir si hay que restar 8. El launcher no expone una única raíz Tk global que
   el helper pueda consultar, así que el backend sale de las variables de entorno.
3. **`_linux_key_name(keysym, keycode)`** — el mapeo en sí, replicando `X11_InitKeyboard`
   de SDL3 para eventos reales de tecla:
   - nombre de posición = `LINUX_XFREE86_2[keycode − 8]` (la posición física de la tecla);
   - nombre de keysym = `LINUX_KEYSYM_NAMES[keysym]` para teclas con nombre (los caracteres
     simples se mapean por posición/carácter, nunca por nombre — eso es lo que hace los
     binds independientes del layout);
   - si ambos existen y coinciden: el nombre de posición; si falta uno: el otro, si no el
     respaldo por carácter; si **discrepan**: gana el nombre de keysym — salvo para teclas de
     carácter (`LINUX_CHAR_SC`) y los once scancodes fijos que SDL3 no deja reasignar por
     keysym (Escape, CapsLock, Numlock, L/R Shift, L/R Ctrl, L/R Alt, L/R GUI) — exactamente
     la regla de remapeabilidad de SDL3, la que hace que `F24` se bindee como `F24` donde la
     tabla de posiciones dice `ModeSwitch`.
4. **`key_from_event()`** — la rama de Windows es el código antiguo textual (primero
   `KEYSYM_NAMES`, luego `MapVirtualKeyW` → tabla de scancodes); en Linux delega en
   `_linux_key_name`.
5. **`sdl_candidates()`** — en no-Windows también produce el nombre de loader
   `"libSDL3.so.0"`, el SDL3 de la distro resuelto contra el sistema (el paquete de Linux no
   trae SDL3; `licenses/THIRD-PARTY.txt` lo documenta como de la distro, y es la misma
   librería que carga el runtime, así que la pestaña de mandos lee el mando exactamente como
   lo hará el juego).
6. **`PadReader.library()`** — la guarda "saltar si no es archivo" ahora solo aplica en
   Windows (un nombre de loader como `libSDL3.so.0` no es un archivo, pero el linker dinámico
   lo resuelve); docstrings actualizadas.
7. **`open_log()`** — el `xdg-open` POSIX va envuelto en `try/except OSError`: una consola
   desnuda sin `xdg-open` no debe tumbar el launcher al abrir el log.
8. **Mensaje de la pestaña de mandos** — el texto "SDL3 no encontrado" dice `SDL3.dll` en
   Windows y `libSDL3.so.0` en Linux.

### Cómo se hicieron las tablas

Ni a mano ni con un diccionario ad-hoc de nombres Tk/X11 — ambos se probaron primero y ambos
fallaron: el mapeo por carácter es dependiente del layout, y los nombres de keysym X de Tk
no son el espacio de nombres de SDL (Tk reporta `"Next"`, SDL dice `"PageDown"`; el keysym
`"Menu"` de Tk es el `SDL_SCANCODE_APPLICATION` de SDL, que SDL3 *renombra* a `"Menu"` en la
inicialización de vídeo en X11, Wayland *y* Windows). Las tablas se generan del propio código
de mapeo de SDL3 3.4.20: un programa C independiente incorpora `src/events/
SDL_keysym_to_scancode.c`, `src/events/scancodes_xfree86.h` y `scancodes_linux.h` del
tarball oficial 3.4.20 (la versión instalada) y ejecuta el camino real
`SDL_GetScancodeFromKeySym` de SDL3 contra el mapa de teclado X de la máquina, más la
`xfree86_scancode_table2` cruda. Dos rarezas de la librería hubo que replicar, no llamar:

- **`SDL_Init()` devuelve un `bool` de C** (no cero = éxito) — la comprobación inicial de
  fallo `!= 0` estaba invertida y el generador "funcionaba" sobre nada.
- **`XStringToKeysym` (libX11 1.8.14) sufre segfault en esta máquina**, así que el paso
  carácter→keysym usa una implementación local de su comportamiento documentado y estable
  (ASCII de 7 bits = el valor, `0x80`–`0xFF` = `0x01000000|c`); y el renombrado a nivel de
  driver de SDL3, `SDL_SCANCODE_APPLICATION → "Menu"`, se replica en el generador, si no la
  tabla emitida diría `"Application"` y el juego no lo resolvería.

El generador emite un archivo JSON; un script compacto lo convierte en los cuatro literales
Python ahora embebidos en el launcher, cada uno con un comentario que apunta a este informe.
Regenerar es una compilación de un paso para un SDL3 nuevo — las tablas son datos, no lógica.

## Entorno de verificación (Arch Linux)

Misma máquina que las Tareas 1–8: x86_64, AMD FX-8350, NVIDIA GTX 1060, Python 3.14.10
(sistema, con Tkinter y Pillow), Tk 8.6.16 (windowing X11), SDL3 3.4.20
(`/usr/lib/libSDL3.so.0`, drivers X11 + Wayland), **layout de teclado español**. La sesión
de escritorio es XWayland/KWin (`:0`), que no acepta foco sintético a nivel X para captura
end-to-end, así que la verificación en vivo corrió bajo Xvfb: display `:91` (solo el lado
Tk/launcher) y display `:92` (solo el lado SDL3), inyectando eventos de tecla reales a
nivel de protocolo X con `XTestFakeKeyEvent`. El launcher se importó como módulo
(`SourceFileLoader` — `spec_from_file_location` devuelve un spec sin loader para archivos
`.pyw`), no se re-ejecutó como aplicación.

## Verificación (todo en esta máquina)

1. **El launcher compila e importa en Python de Linux.** `python3 -m py_compile
   launcher/tlou_launcher.pyw` limpio; el módulo importa en el Python del sistema (y en el
   empaquetado: la Tarea 8 verificó `./TLOU-Launcher` abriendo una ventana real desde una
   extracción fresca — el `.pyw` se pasa a `python3`, `tkinter` y `PIL` están presentes).
2. **Harness de unit tests contra el layout español en vivo** (`0 failures`, ~70
   comprobaciones). Los códigos de tecla esperados son los X11 reales de la máquina, volcados
   con `XkbKeycodeToKeysym` en group 0 / level 0 — el mismo nivel que lee SDL —, así que los
   casos son además prueba de independencia del layout:
   - Windows simulado (`sys.platform = "win32"`): se reproduce exactamente el
     comportamiento antiguo — `KEYSYM_NAMES` primero, `("w", 0x11)` → `None` sin `windll`,
     exactamente dos `sdl_candidates`, ambos `Path`, las rutas de Windows;
   - trampas del layout español: `("ñ", 49)` → `` "`" ``, `("é", 48)` → `"'"`,
     `("dead_grave", 34)` → `"["` (la tecla muerta es el `[` físico), `("masculine", 49)` →
     `` "`" ``, `("ntilde", 47)` → `";"`, `("+", 21)` → `"="` (tecla de `!` con shift: la
     posición física de `=`), `("Multi_key", 49)` → `` "`" `` (un keysym desconocido cae a la
     posición);
   - las tablas provisionales antiguas traían dos entradas erróneas, ahora corregidas y
     comprobadas: `("Delete", 119)` → `"Delete"` (era `"Keypad ."`), `("KP_Equal", 125)` →
     `"Keypad ="` (era `"Numlock"`);
   - la regla de sobreescrito: `("F24", 93)` → `"F24"` donde la tabla de posiciones dice
     `ModeSwitch`; `("ISO_Level3_Shift", 92)` → `"Right Alt"` (scancode fijo, gana la
     posición);
   - **cada nombre que el camino de Linux puede producir, y cada nombre de las cuatro tablas
     embebidas, se resuelve con el `SDL_GetScancodeFromName` real** del `libSDL3.so.0`
     instalado, y `"Application"` no aparece en ninguna parte (el renombrado a `"Menu"` está
     en las tablas);
   - lado de mandos: `sdl_candidates()` termina en el nombre de la distro,
     `PadReader.library()` carga el SDL3 de la distro, `PadReader().ready` es `True` sin
     mando conectado (informa "no hay mando", no "no hay SDL3");
   - comportamiento sin cambios: pasan las comprobaciones de `game_command`/`game_env`/
     `python_exe`/`key_label`.
3. **End-to-end con dos displays: eventos de tecla reales, ambos lados, cada tecla** — la
   comprobación más fuerte sin un ser humano:
   - display `:91`: una ventana Tk en vivo con el `key_from_event` del launcher ligado a
     `<KeyPress>`; display `:92`: una ventana SDL3 real (`SDL_Init` + `SDL_CreateWindow`);
   - el mismo evento XTEST de tecla se inyecta en ambos displays para 50 teclas (letras,
     dígitos, puntuación, `F1`/`F5`/`F12`, `Return`/`Tab`/`Space`/`BackSpace`, flechas,
     `Home`/`PageUp`/`Delete`/`End`/`Insert`, el teclado numérico incl. `KP_Equal`/
     `KP_Divide`/`KP_Enter`, ambos shifts, ambos alts, `Super_L`/`Super_R`, `Menu`, `Mute`,
     `Pause`);
   - el lado Tk produce el nombre del launcher, el lado SDL3 produce
     `SDL_GetScancodeName(event.scancode)` del evento de tecla que SDL3 recibió realmente;
   - **51/51 coinciden, `BAD: 0`** — p. ej. `kc=125 tk=('KP_Equal') sdl=Keypad =
     launcher=Keypad =`, `kc=135 tk=('Menu') sdl=Menu launcher=Menu`, `kc=138
     tk=('SunProps') sdl=AC Properties launcher=AC Properties`. Sea cual sea la tecla física
     que pulse el jugador, el launcher escribe el nombre que el juego asignará a esa tecla.
4. **La captura combinada Tk+SDL en el mismo display se intentó y es poco fiable** (documentado,
   no es un bug del launcher): en un display X11 sin gestor de ventanas SDL3 fuerza el foco
   de entrada X a su propia ventana al mapearla (la rama `!data->videodata->net_wm` de
   `SDL_x11window.c`) y mantiene el grab del teclado hasta procesar un FocusOut; el
   `focus_force()` de Tk toma un grab X que bloquea al otro lado; y uno de los probes
   crasheó en libX11 — que resultó ser un cambio real de la API, no nuestro código: **la
   `XFree` de libX11 1.8.14 ahora es `int XFree(void*)`** (un solo argumento, verificado
   contra el source upstream de 1.8.14), así que la llamada clásica de dos argumentos
   `XFree(dpy, ptr)` liberaba en silencio la *Display*. El diseño de dos displays esquiva
   todo eso (sin contienda de foco; sin XFree en el camino que importa), y la `XFree` de un
   argumento se confirmó segura en el harness.

## Decisiones técnicas

- **Los nombres de scancode de SDL3 son el vocabulario de los binds.** El juego resuelve las
  entradas de `key_map` con `SDL_GetScancodeFromName` (`runtime_pad.c`); un nombre que el
  juego no pueda resolver es un bind muerto, así que el launcher escribe exactamente los
  nombres que los drivers de SDL3 producen — en Windows (el `SCAN_NAMES` preexistente de la
  Tarea 9 vía `MapVirtualKeyW`) y ahora en Linux.
- **Posición primero, keysym segundo, con la regla exacta de sobreescrito de SDL** — no un
  diccionario de nombres Tk/X11. El driver X11 de SDL3 hace huella dactilar del teclado y
  construye `key_layout[keycode] = xfree86_scancode_table2[keycode − 8]` (en esta máquina
  las distancias de huella son 0 para la tabla2 y 6 para la tabla1) y después sobreescribe
  con el keysym para teclas no-carácter remapeables; el driver Wayland usa la misma tabla2
  indexada por el código de tecla de Linux. Replicar el algoritmo (con sus once scancodes
  fijos) es lo que hace que teclas muertas, acentos y teclados ISO/AZERTY se comporten como
   en juego, en ambos backends con una sola tabla.
- **Las tablas son datos embebidos, generados una vez.** El código de mapeo del launcher es
  Python puro sobre literales estáticos: sin FFI en el momento del mapeo (sin nueva clase de
  riesgo de segfault), sin paso de compilación, idéntico en el repositorio y en el paquete.
  Un SDL3 nuevo implica regenerar los literales (el generador y este informe son la receta),
  no reescribir lógica.
- **`_linux_backend()` por variables de entorno**, porque el launcher no expone una raíz Tk
  global única al helper; la regla (`WAYLAND_DISPLAY` definido y `DISPLAY` no → Wayland)
  coincide con la elección de backend de Tk (los clientes de XWayland son X11).
- **La pestaña de mandos usa el SDL3 de la distro** (`libSDL3.so.0`), coherente con que el
  paquete no trae SDL3 (Tarea 7) y con que el runtime carga la misma librería de la distro —
  un solo stack de mandos, sin desfase de versión entre "lo que ve el launcher" y "lo que ve
  el juego".
- **Las fallas de `xdg-open` no son fatales**: `open_log` traga el `OSError` — un
  `xdg-open` ausente (consola desnuda, caja mínima) no debe tumbar el launcher; la ruta del
  log está en pantalla de todas formas.

## Garantía de compatibilidad con Windows (prueba)

- `git status` muestra exactamente un archivo modificado: `launcher/tlou_launcher.pyw`
  (+166/−13). No se tocó ningún archivo de Windows (`tlou-launcher.c`, `tlou-game.c`,
  `build-starters.sh`, los scripts del paquete de Windows sin cambios).
- `key_from_event`: la primera línea (búsqueda en `KEYSYM_NAMES`) y toda la rama
  `sys.platform == "win32"` (`MapVirtualKeyW` → `SCAN_NAMES`) son el código antiguo
  textual; `_linux_key_name` solo se alcanza cuando la plataforma no es Windows.
- `sdl_candidates`: los dos produces de Windows (`gow3_pc/out/SDL3.dll`, el `clang64/bin/
  SDL3.dll` de MSYS2) no cambian y siguen siendo los primeros; el de la distro está dentro
  de `if sys.platform != "win32":`. El harness reproduce la lista de candidatos de Windows
  byte a byte bajo un `sys.platform` simulado.
- `PadReader.library`: el salto por "no es archivo" ahora es explícitamente
  `and sys.platform == "win32"` — en Windows el cuerpo del bucle no cambia.
- `open_log`: la rama `sys.platform == "win32": os.startfile(LOG)` es textual; solo la rama
  POSIX ganó el `try/except OSError`.
- Las tablas y helpers nuevos son código muerto en Windows (nunca se referencian:
  `key_from_event` devuelve antes de llegar a ellos).

## Riesgos / observaciones para las tareas siguientes

- **Tk en Wayland nativo no se ejercitó en vivo** (el Tk de esta sesión es XWayland/X11;
  Xvfb es X11). La rama Wayland es una línea de aritmética de keycode (`sin el −8`, el
  backend Wayland de Tk reporta códigos de entrada de Linux, el mismo índice que usa el
  driver Wayland de SDL3) y está comprobada por unit tests, pero un jugador en un escritorio
  Wayland puro será el primero en ejecutarla; si Tk reportara ahí códigos estilo X11, el
  síntoma sería un desfase de un keycode en los binds, trivialmente arreglable en
  `_linux_backend`.
- **Las tablas siguen SDL3 3.4.20** (la actual de la distro). Si SDL3 cambia la
  `xfree86_scancode_table2` o los renombrados de drivers, hay que regenerar los literales —
  el comentario sobre las tablas apunta a este informe. El runtime del juego y el launcher
  cargan el *mismo* SDL3 de la distro, así que en la práctica se desfasan juntos.
- **Un carácter sin código de tecla que SDL no pueda resolver queda sin bindear** (p. ej. un
  evento de `é` pegado): `key_from_event` devuelve `None`, lo mismo que Windows para teclas
  desconocidas. En uso real Tk siempre manda el código de tecla, así que esto solo afecta a
  eventos sintéticos.
- **La pestaña de mandos se verificó cargando e informando "no hay mando"**, no leyendo un
  mando en vivo (no hay mando en esta máquina); el camino de mandos de Windows no cambia y
  funcionaba antes.
- **La `XFree` de un argumento de libX11 1.8.14** es una trampa para cualquier trabajo
  futuro de `ctypes` con X11 en este proyecto (la llamada clásica de dos argumentos libera la
  `Display`); queda anotada aquí porque costó una ronda de depuración. El launcher en sí no
  hace FFI con X11.
