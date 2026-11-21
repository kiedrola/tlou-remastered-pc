# Tarea 11 — Arranque real y la velocidad del motor a 30 fps (Fase 3)

> **Estado: ARRANQUE VERIFICADO EN LA MÁQUINA DE LA TAREA 11 (7950X / RTX 3090, Arch); BUG
> DEL PRIMER JUEGO ENCONTRADO Y ARREGLADO.** El primer arranque real —
> `python3 tools/run_game.py --seconds 0` (la misma ruta que usa el lanzador `TLOU-Game`),
> eboot real 1.11 `t1ps4` más su update 1.11 — arranca limpio hasta el menú principal
> (prefetch de PlayGo), salida 1280×720, mando mapeado (XInput→Sofa2), sin pérdida de
> dispositivo. La primera partida del usuario expuso el bug más visible del port: con el
> límite de **60 FPS por defecto del port, todo el juego va al doble de velocidad**
> (animaciones, físicas, audio). Causa raíz y arreglo abajo. Lo que queda de Tarea 11/12 es
> una sesión completa a la velocidad corregida (y la Tarea 11.5 opcional: un modo 60 fps
> real).

## Lo que pedía la tarea

> Ejecutar el probe con una copia real del juego y su update 1.11; confirmar que el runtime
> arranca, carga la caché de shaders y entra en el juego.

## Lo que se encontró

El juego va a 60 FPS estables y *todo se mueve demasiado rápido*. La sospecha inicial era el
reloj del huésped: en Windows el huésped usa `rdtsc` (`GOW3_NATIVE_TSC`), y una fuente de
reloj mal elegida en Linux escalaría todo el código basado en tiempo. **No es el reloj**:

- En POSIX `read_tsc()` / `process_time_counter()` devuelven `CLOCK_MONOTONIC` en
  nanosegundos (`gow3_pc/src/runtime_kernel.c`) — sin ninguna conjetura de frecuencia.
- El `eboot.bin` 1.11 **no contiene instrucciones `rdtsc`** (solo el build 1.00 tiene dos,
  en un contexto que parece datos). La ruta de Windows `GOW3_NATIVE_TSC` es solo
  `#ifdef _WIN32`.
- El reloj del huésped funciona: las líneas de temporización del arranque son coherentes.

La causa real es el **framerate para el que el motor está diseñado**. El motor de TLOU
Remastered (`t1ps4`, PS4) es **basado en frames a 30 fps**: su lógica da un paso fijo de
1/30 por frame presentado. Evidencia en el `eboot.bin` 1.11:

- Los float `1/30f` (0x3D088888) aparecen **22 veces**, `1/60f` (0x3C23D70A) **12** — las
  constantes por frame del motor se construyen alrededor de un tick de 30 fps (TLOU
  Remastered es el "modo 30 fps"; el modo 60 fps de PS4 Pro era una presentación separada
  ligada al display — Digital Foundry: "modo 1080p60 … bloqueado … cuando la consola está en
  salida 1080p").
- Los scripts LISP del juego hacen ramas sobre **`is-locked-at-30fps?`** — el comportamiento
  se adapta al modo.
- Existe un comando de debug del juego: **`forcefps <número>`** — "force the framerate to
  `<número>`" (se usa con la consola de debug; esa consola aún no está cableada en este
  port).

Con el port presentando a 60 frames/s (su defecto de `fps_limit`), el motor hacía 60 pasos
de 1/30 por segundo → **velocidad ×2 en animaciones, físicas y audio** mientras el contador
de FPS leía tranquilamente 60. El README upstream lo dice en la práctica: "un límite fijo de
30 FPS … hace que el movimiento sea claramente más regular" (escrito para Windows, donde 60
es el defecto y el jugador elige 30 en el menú).

## Lo que se cambió (solo POSIX; Windows intacto)

- `gow3_pc/gpu/shim/gow3_settings.h` — defecto de `fps_limit`: `30` en POSIX, `60` en
  Windows (`#ifdef _WIN32` alrededor del inicializador). El valor en vivo sigue viniendo del
  menú del juego / `tlou.ini`; esto solo cambia con qué arranca un fichero de ajustes nuevo.
- `launcher/tlou_launcher.pyw` — el defecto del campo FPS cuando `tlou.ini` no tiene
  `fps_limit`: `"30" if sys.platform != "win32" else "60"`. En `win32` la expresión evalúa
  al valor antiguo exacto (verificado ejecutando la expresión nueva bajo un
  `sys.platform` simulado: Windows idéntico, Linux 30).
- `analysis/tlou-user/tlou.ini` — `fps_limit=60` → `fps_limit=30` (el fichero de ajustes de
  este usuario; en Windows el fichero es del usuario y nunca lo toca el build).

No hacía falta tocar el presentador: `VideoOutDriver::PresentThread` ya pacea las
presentaciones a `fps_limit` cuando está por debajo del refresco del display (la ruta
"unlocked + límite"), y la fila del menú en juego ya muestra el límite actual.

## Verificado

- Build: `gow3_pc/build.sh` limpio (librería GPU, starters ejecutables).
- Arranque real: `python3 tools/run_game.py --seconds 150` → menú alcanzado (prefetch de
  PlayGo), y el presentador ahora registra **`VideoOut: vblank 480 Hz, frame limit 30 FPS`**
  (antes 60).
- Estado estable (menú, tras el estallido de streaming de assets):
  **`Frame stats: 30.0 FPS`** y **`Frame pacing: media 33.33 ms, desv. típ. 0.05–0.34 ms,
  p99 ≤ 35.3 ms, 0 frames sobre 1.5× la media`** — exactamente 30 fps, paceado de forma
  uniforme, con ~58–88% de holgura en el hilo GPU.
- Durante los estallidos de streaming de PlayGo en el menú la media cae (14–26 FPS) con picos
  de p99: esperado — un límite es un techo, y el trabajo de streaming está limitado por CPU
  (copias de host de hasta 122 ms en un frame, 653 fallos de escritura/s). Misma clase de
  caídas que ya mostraba la sesión a 60 fps del usuario al cargar.

## Para el usuario (comprobación de velocidad)

Ejecuta `TLOU-Game` (o `python3 tools/run_game.py --seconds 0`) y juega. Animaciones,
caminata, disparos y audio deberían estar ahora a velocidad normal con 30 FPS estables (el
overlay muestra los FPS; la fila "Frame rate limit" del menú del juego también muestra 30).
**Para este juego, mantén el límite en 30: cualquier valor mayor hace que el juego vaya más
rápido, no más fluido** — el motor es basado en frames a 30 fps; 60+ solo es útil para
juegos estilo gow3. Si algún día se quiere un modo 60 fps real, esa es la Tarea 11.5
(fuerzar que el motor mismo tickee a 60: el comando de debug `forcefps` / el LISP
`is-locked-at-30fps?` / la opción Framerate del menú de debug), no un cambio del lado de la
presentación.

*(Corrección, 21-11-2026: la afirmación de que «cualquier valor mayor hace que el
juego vaya más rápido» era errónea: el aumento de velocidad observado era el bug TSC
(la HLE reportaba 1 GHz mientras el juego divide su `rdtsc` a la frecuencia del host),
no el framerate — con la simulación en tiempo real (fix `GOW3_NATIVE_TSC=1`, ver
`BUG-SPEED.md` §11), la tasa de presentación no cambia la velocidad de la simulación.
Verificado por el usuario en el 7950X: 60 fps estables (`fps_limit=60`), sin pérdida de
dispositivo. 30 fps sigue siendo el defecto recomendado (punto de diseño del motor);
ver Tarea 11.5 del roadmap, cerrada.)*

## Seguridad de Windows

- `gow3_settings.h`: la rama `#ifdef _WIN32` es la línea original exacta
  (`std::atomic<int> fps_limit{60};`); el defecto de POSIX es código nuevo solo.
- `tlou_launcher.pyw`: con `sys.platform == "win32"` la expresión nueva devuelve la cadena
  antigua exacta; ejecutada bajo plataforma simulada, la salida de Windows es idéntica.
- `tlou.ini`: fichero de datos del usuario, nunca modificado por el build en ninguna
  plataforma.

## Abierto (terreno de la Tarea 12)

- Una sesión completa de principio a fin a 30 fps (mando grabado con `GOW3_PAD_RECORD`) para
  confirmar que no hay pérdida de dispositivo ni cuelgues en sesiones largas — la petición
  original de la Tarea 12.
- Tarea 11.5 opcional: modo 60 fps real del motor (ver roadmap).
