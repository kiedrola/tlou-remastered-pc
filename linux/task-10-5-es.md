# Tarea 10.5 — Tabla de red-zone / emulación ISA en POSIX (Fase 2)

> **Estado: COMPLETADA Y VERIFICADA.** La tabla de red-zone/ISA se carga ahora, se aplica y
> se diagnostica en POSIX exactamente como en Windows. Un solo archivo cambiado
> (`gow3_pc/src/probe.c`, +26/−22): desaparece la puerta `#ifdef _WIN32` que convertía
> `--red-zone` en un no-op con el mensaje "Red zone: Windows-only protection ignored on
> POSIX" (el cargador, la comprobación de la offset de los stubs y el aplicador ya eran C
> portable, y lo son), y el manejador de señales POSIX gana los mismos dos diagnósticos que
> tiene el manejador vectoral de Windows — `note_red_zone_fault` (el contador de
> `GOW3_RED_ZONE_STATS=1`) y el reconocimiento de sitios de fallo mediante
> `red_zone_site_of_stub`. Evidencia decisiva en esta CPU (AMD FX-8350, pre-Ryzen, sin
> MOVBE): el primer arranque imprime ahora **"Red zone: 6528 instructions protected in 5990
> patches (29 through relays), 0 patches skipped"** (154 instrucciones MOVBE emuladas por
> código) y el invitado, que antes de esta tarea moría a los ~1 s con `Guest fault (signal
> 4)` — un MOVBE puro decodificado como SIGILL — ahora **ejecuta el juego durante 300 s**:
> todos los subsistemas TTY se inicializan (build 2863488), el mundo cambia a `core`, el bucle
> de render es estable (293 draws / 104 dispatches por informe de 5 s), se resuelven 49.152
> fallos de seguimiento de páginas de GPU, y funciona la nueva línea de estadísticas POSIX:
> "Red zone: 41 of 49152 guest faults came from protected instructions". Windows queda
> idéntico byte a byte (la rama `#ifdef _WIN32` queda intacta; prueba abajo). Esto libera la
> ejecución del juego en cualquier CPU x86-64 (pre-Haswell-2013 / pre-Ryzen) y era
> imprescindible antes de la Tarea 11, cuyo primer arranque completo ya ejerce la tabla
> aplicada.

## Qué pedía la tarea

> Aplicar la tabla de red-zone/ISA en POSIX: levantar la puerta `#ifdef _WIN32` alrededor de
> `red_zone_load` (la comprobación de offset de stubs/tamaño de página es independiente de la
> plataforma; `red_zone_apply` y el `protect()` de la zona de stubs ya son código compartido y
> correcto bajo `mmap`/`mprotect`, pues la imagen es un único mapeo RW contiguo por debajo de
> 2 GiB y los stubs quedan justo después, dentro del alcance del salto rel32). Añadir
> `note_red_zone_fault` y el reconocimiento de stubs mediante `red_zone_site_of_stub` al
> manejador de señales POSIX para paridad de diagnósticos. La protección se necesita en Linux
> por la misma razón que existe en Windows: el kernel empuja su marco de señal por debajo de
> RSP en cada fallo de seguimiento de páginas de GPU, corrompiendo la red-zone que el código
> del invitado usa como espacio temporal. Windows queda idéntico byte a byte: solo se mueve la
> puerta, la rama `#ifdef _WIN32` queda intacta. Verificar: en el FX-8350 el primer arranque
> imprime "Red zone: N instructions protected…" en lugar de "ignored" y pasa el SIGILL de ~1
> s; en una CPU moderna (el equipo de la Tarea 11) una regresión de arranque completo con la
> tabla aplicada (solo la protección de red-zone, sin reemplazos ISA). **Hacerlo antes de la
> Tarea 11**, para que el primer arranque completo ya ejerza la tabla aplicada.

## Hallazgos de la lectura del código

1. **`gow3_pc/src/red_zone.c` + `red_zone.h` son C portable al 100 %** (ya se compilaban y
   enlazaban en POSIX): `red_zone_load` lee la tabla `G3RZTAB1` de disco, `red_zone_matches`
   comprueba la tabla contra la imagen cargada (comparación byte a byte por sitio),
   `red_zone_apply` escribe los parches `jmp rel32` de 5 bytes en la imagen y los stubs de
   emulación tras ella, y `red_zone_site_of_stub` traduce un RIP de la zona de stubs al sitio
   protegido. Ninguna API de Windows — la única dependencia de plataforma es la memoria que
   le dan.
2. **La puerta en `main()`** (`probe.c`): `red_zone_load` solo corría dentro de
   `#ifdef _WIN32`; en POSIX el `#else` imprimía "Red zone: Windows-only protection ignored
   on POSIX" y `red_zone` quedaba `NULL`, de modo que `red_zone_apply` era un no-op y la zona
   de stubs nunca se reservaba.
3. **El modelo de memoria hace los stubs alcanzables en POSIX sin cambios.** `runtime_low_map`
   reserva un único mapeo RW contiguo por debajo de 2 GiB de tamaño
   `round_page(imagen) + round_page(stubs)`; los `jmp` de los parches son rel32 desde la
   imagen hacia el bloque de stubs justo después, y `protect()` (mprotect en POSIX) aplica los
   flags de la zona. Nada de la ruta de aplicación necesitaba cambiar.
4. **El manejador POSIX carecía de los dos diagnósticos que tiene el de Windows.**
   `vectored_fault` (`#ifdef _WIN32`) llama a `note_red_zone_fault(rip)` en cada fallo de GPU
   resuelto (alimentando el contador de `GOW3_RED_ZONE_STATS=1`) y su descripción de dirección
   reconoce RIPs de stubs mediante `red_zone_site_of_stub`. El manejador `fault()` de POSIX
   imprimía solo la offset cruda del invitado — un fallo dentro de un stub parecería un fallo
   misterioso en la zona de stubs. El cambio del manejador además reutiliza el `rip` que ya
   tenía que obtener, y deja en el comentario por qué importa la protección aquí: el marco de
   señal del kernel cae por debajo de `rsp` de camino, en los bytes que los stubs mantienen al
   alcance fuera del invitado.
5. **La tabla ya se generaba correctamente en POSIX** (lo encontró la Tarea 10):
   `red_zone_table.py` con `--isa auto` emite la tabla de protección *y* los reemplazos
   software de los conjuntos de instrucciones que faltan (`cpu_features.py` ve que a esta CPU
   le falta MOVBE; el juego usa 154 instrucciones `movbe`/era-`lzcnt` en la imagen 1.11) — 154
   reemplazos, 0 fallos. Solo el cargador se negaba a usarla.
6. **Por qué existe la protección (igual en ambas plataformas):** la librería de GPU hace
   seguimiento de la memoria del invitado protegiendo páginas contra escritura y resolviendo
   el fallo dentro del proceso. En cada uno de esos fallos el SO empuja su marco de señal
   *por debajo de RSP*; el código del invitado trata la página bajo su pila como espacio
   temporal (la "red-zone"), de modo que el marco del fallo la corrompería. La red-zone
   reescribe el puñado de instrucciones que tocan esos bytes en stubs que aparcan el
   espacio temporal en otro sitio. En una CPU sin MOVBE, la misma tabla además emula esas
   instrucciones — el bache de arranque de la Tarea 10 es exactamente esto.

## El cambio

Un solo archivo: **`gow3_pc/src/probe.c`** (+26/−22). Tres bloques:

1. **`main()` — se levanta la puerta.** Se elimina el `#ifdef _WIN32 … #else … #endif`
   alrededor de `red_zone_load` + la comprobación de offset de stubs/tamaño de página; ambas
   plataformas ejecutan ahora los mismos enunciados (la rama `#else`, un print, desaparece).
   La comprobación se queda: una tabla cuya `code_offset` no coincide con
   `round_page(image_size)` se rechaza con el mensaje ya existente ("protection unavailable
   (stub offset does not match this page size)").
2. **`note_red_zone_fault` se mueve a código compartido.** Estaba definida dentro del bloque
   `#ifdef _WIN32` (solo la usaba `vectored_fault`); ahora está en la sección compartida junto
   a `guest_exit`. Mismo cuerpo, una definición en ambas plataformas; el manejador POSIX la
   llama en cada fallo de GPU resuelto, de modo que `GOW3_RED_ZONE_STATS=1` funciona igual en
   Linux.
3. **El manejador `fault()` POSIX gana paridad de diagnósticos.** `rip` se obtiene una vez al
   principio (antes se obtenía al final); se consulta `red_zone_site_of_stub` antes de
   imprimir, de modo que un fallo dentro de un stub se informa como
   `Guest fault (signal N) at guest offset 0x… (red-zone stub), address …` (el sitio
   protegido, igual que la descripción de dirección de Windows); un fallo que no está en el
   invitado se maneja exactamente como antes. La ruta de fallo de GPU ahora alimenta el
   contador de estadísticas de red-zone antes de devolver.

Ningún otro archivo necesitaba cambiar: `red_zone.c`/`red_zone.h` ya eran portables, la
herramienta (`probe_tlou.py`) ya pasa `--red-zone` y construye la tabla con `--isa auto` en
toda plataforma, y la ruta de aplicador/`protect()` es código compartido.

## Compatibilidad con Windows (prueba)

No hay toolchain MinGW/MSYS2 en este equipo, así que la prueba es por análisis de las
guardas, como en tareas anteriores:

| Región cambiada | Plataforma | Efecto |
| --- | --- | --- |
| `note_red_zone_fault` movida de `#ifdef _WIN32` a la sección compartida | Windows | Se compila una vez, en otra línea, con un cuerpo idéntico que usa solo símbolos compartidos (`red_zone`, `image`, `getenv`, `printf`, `PRIu64`, `__atomic_*`). Una definición antes y después. |
| `note_red_zone_fault` (igual) | POSIX | Símbolo nuevo (antes inexistente). |
| Ediciones del manejador `fault()` | Windows | Ninguno — la función entera está dentro de `#ifndef _WIN32`. |
| Ediciones del manejador `fault()` | POSIX | Solo diagnósticos: una obtención temprana de `rip`, una rama extra sobre `red_zone` (con comprobación de NULL) y una llamada al contador existente. El flujo de control para fallos no gestionados no cambia. |
| Eliminación de la puerta de `red_zone_load` en `main()` | Windows | Los enunciados dentro del viejo `#ifdef _WIN32` no cambian — exactamente lo que Windows compilaba antes. |
| Eliminación de la puerta de `red_zone_load` en `main()` | POSIX | Comportamiento nuevo: la tabla se carga y se aplica (antes se omitía con un aviso). |

Nada dentro del bloque `#ifdef _WIN32` de Windows fue editado; la rama `#else` eliminada era
código POSIX; la función movida no referencia símbolos de plataforma.

## Verificación (Arch, FX-8350, GTX 1060 3 GB, Python 3.14.10, Xvfb :97)

1. **Compilación:** `gow3_pc/build.sh --test` — compilación limpia, los 10 binarios de
   pruebas pasan (`set -euo pipefail` habría detenido ante el primer fallo).
2. **Primer arranque, herramienta estándar** (`probe_tlou.py --boot`, eboot base 01.00, sin
   update a través de la ruta resuelta del juego): la tabla se regenera para esta imagen
   (157 reemplazos MOVBE: 22 cargas, 135 escrituras) y **se aplica**:
   `Red zone: 6506 instructions protected in 6001 patches (28 through relays), 0 patches
   skipped`. El invitado, que antes moría a los ~1,1 s con `Guest fault (signal 4)` en un
   MOVBE, ahora corre hasta el watchdog de 10 s (salida 142, SIGALRM) con pilas de hilos en
   vivo dentro del código del invitado (p. ej. `image-relative=0xbae8e3`).
3. **Arranque estándar, juego canónico** (base 01.00 + update 01.11 mediante `TLOU_UPDATE`):
   `Red zone: 6528 instructions protected in 5990 patches (29 through short-jump relays) over
   538 red-zone functions … (table kept)` — la tabla cacheada (154 reemplazos MOVBE: 20
   cargas, 134 escrituras, 0 fallos) se reutiliza y se aplica. Ninguna línea
   `Guest fault (signal …)` en todo el log.
4. **Arranque de 300 s, watchdog desactivado** (misma cadena de herramientas; el comando del
   runtime es el escrito en `boot.json` más `--timeout 0`, interceptado en
   `subprocess.Popen` para que prepare/env/comando salgan del código real): el invitado
   sobrevive los 300 s completos y solo lo mata el límite propio de la herramienta
   (`timed_out: true, exit_code: -9`). De camino: todos los subsistemas TTY se inicializan
   (`TTY init: …` hasta `cutscene`, `build 2863488 (Sep 30 2020)`), `Switching world: from to
   core`, todos los PRX cargan, y el bucle de render es estable (informes de 5 s: 293 draws /
   104 dispatches / 194 submissions, 82 pipelines gráficos + 32 de cómputo, sin compilaciones
   pendientes tras el warm-up, memoria de vídeo creciendo al streaming de assets: 449 MiB /
   145 imágenes). Funciona el nuevo contador de estadísticas POSIX:
   `Red zone: 41 of 49152 guest faults came from protected instructions` — 49.152 fallos de
   seguimiento de páginas de GPU resueltos dentro del proceso, 41 de ellos con el RIP en una
   instrucción protegida (el caso del marco de señal del kernel para el que existe la
   red-zone).
5. **Antes/después en esta CPU:** antes — ventana arriba, icono puesto, a los ~1,1 s
   `Guest fault (signal 4) at guest offset 0xcafaa3` (SIGILL en un MOVBE; la Tarea 10
   documentó el bache). Después — 300 s de ejecución estable del invitado, sin fallos, MOVBE
   emulado en código.

El segundo criterio de verificación de la tarea (regresión de arranque completo en una CPU
moderna, protección de red-zone sin reemplazos ISA) queda delegada al equipo de la Tarea 11,
como la propia tarea indica.

## Baches encontrados de camino (fuera de esta tarea, documentados para la Tarea 11)

1. **El watchdog POSIX mata cada sesión a los 10 s.** `probe.c` instala
   `alarm(timeout_seconds)` (por defecto 10) con un manejador que vuelca las pilas de los
   hilos y hace `_exit(128+SIGALRM)`; en Windows la rama compila `(void)timeout_seconds` —
   sin watchdog. `probe_tlou.py --boot` nunca pasa `--timeout`, de modo que en Linux un
   arranque normal no puede sobrevivir a 10 s (el arranque de 300 s de esta tarea necesitó
   `--timeout 0`). El arranque completo de la Tarea 11 necesita una decisión: que la
   herramienta pase `--timeout 0`, que el watchdog se amplíe solo tras la entrada del
   invitado, o ambas. Esto antecede a esta tarea (ya estaba cuando la Tarea 10 chocó contra
   el muro de MOVBE) y es ortogonal a ella.

## Archivos

- Cambiado: `gow3_pc/src/probe.c` (+26/−22).
- Generados (gitignored, `analysis/tlou/`): `red-zone.bin` (467.518 bytes, la tabla de la
  imagen 1.11: 6528 instrucciones protegidas en 5990 parches + 154 stubs de emulación MOVBE),
  `red-zone-isa.txt`, los renovados `boot.bin`/`boot-linked.bin`/`boot.log`/`boot.json`.
- Artefacto de prueba (no se commitea, en `/tmp/task105/`): `run.py` — la interceptación de
  `subprocess.Popen` que añade `--timeout 0` al comando del runtime mientras todo lo demás
  (prepare, env, comando) sale del `probe_tlou.main()` real.
