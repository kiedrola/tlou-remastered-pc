# BUG-SPEED — The Last of Us Remastered (PS4) en Linux

**Estado:** **CAUSA RAÍZ DEMOSTRADA Y ARREGLADA** (21-11-2026, A/B cuantitativo con el mismo ejecutable, §11). El bug: la HLE Linux reportaba `sceKernelGetTscFrequency() = 1e9` (fallback 1 GHz) mientras el reloj maestro del juego divide su `rdtsc` inline (host, 4.013.358.889 Hz en el FX-8350) por ese valor → dt por frame 4,0134× el real → animación/física a 4,0134×. Fix: `GOW3_NATIVE_TSC=1` ahora calibra el TSC nativo también en Linux (espejo del camino Windows, `runtime_kernel.c`); el launcher ya lo fuerza por defecto en este host. Con el fix: divisor 4.013.358.889, dt ≈33,3 ms, tiempo de juego 1,000×. **VERIFICADO POR EL USUARIO (21-11-2026):** sesión normal jugada con el launcher — animaciones y física a velocidad normal. El bug está cerrado en el FX-8350; queda pendiente la regresión en el Ryzen 7950X.
**Objetivo de este informe:** dar a otro LLM una base verificable para encontrar la causa antes de modificar el runtime. **Actualizado:** 21-11-2026 (§11; el informe original de 20-11 queda como historial, con sus conclusiones corregidas en §11).

## 0. Resumen ejecutivo / orden de investigación

> **Cerrado (21-11):** la causa está identificada y arreglada (§11). El resto de este
> informe es el historial de la investigación; las conclusiones de la revisión 20-11
> que se contradicen con §11 están marcadas en secciones 11.1-11.2.

**Orden actual: la estrategia de §10 (revisión 20-11).** La «primera prueba» original (VBlank 480 vs 60) **ya se ejecutó** (experimentos E1/E2 de §0.5: la cortina del título es idéntica en ambos; VBlank descartada para esa animación).

(Original) Comparar `GOW3_VBLANK_HZ=480` con `GOW3_VBLANK_HZ=60`, manteniendo fija la presentación en 30 FPS.

Razón: `probe_tlou.py` fuerza por defecto `GOW3_VBLANK_HZ=0` (que el runtime convierte en 480 Hz). En `driver.cpp`, el contador VBlank, las notificaciones de equeue y `sceVideoOutWaitVblank()` avanzan a esa frecuencia, aunque las presentaciones estén limitadas a 30 FPS. El comentario existente en `probe_tlou.py` recuerda una sesión del 24-10-2026 en la que usar VBlank a 60 Hz produjo 30–34 FPS en una zona exigente y «normal game speed». **Es una pista histórica en un comentario de código, no una prueba A/B contenida en los logs actuales.**

No implementar todavía un arreglo de TSC, `GOW3_TIME_SCALE`, `forcefps` ni un multiplicador de animación. Primero aislar si el motor está consumiendo VBlank, flips, un reloj propio o un timestep fijo.

> **Actualización 20-11-2026 (revisión, §0.5 y §10):** tras el A/B de VBlank (descartado para la cortina) y el análisis IDA del divisor TSC, se implementó y se verificó en el log un parche de calibración del divisor. **El síntoma persiste con el parche aplicado: la causa de la aceleración NO está identificada.** El mecanismo TSC es real (el reloj `rdtsc/divisor` avanza a 1,338× en FX con el divisor original) pero su causalidad con la velocidad de las animaciones NO está demostrada. Nueva estrategia de investigación: §10.

## 0.5 HALLAZGOS Y REVISIÓN DE CONCLUSIONES (sesión 20-11-2026, FX-8350)

> **SUPERADO por §11 (21-11-2026):** varias afirmaciones de esta sección eran
> incorrectas o inválidas: el divisor SÍ es escrito por el juego en el init
> (`sub_C86A70` ← `sceKernelGetTscFrequency`); el divisor en runtime era 1e9
> (de la HLE), no 3.000.000; el factor del bug es 4,0134× (TSC_host/1e9), no
> 1,338×; y el A/B E1/E3 «cortina idéntica» era predecible porque el parche de
> imagen no cambiaba nada en runtime. Ver §11.2.

> **REVISIÓN IMPORTANTE (20-11, tras el testeo del usuario):** el juego **sigue ejecutando las animaciones demasiado rápido con el parche TSC aplicado** (vblank 480, fps 30, divisor parcheado a 4.013.372 — el log lo confirma). Por tanto: **NO está demostrado que la discrepancia TSC sea la causa de la aceleración**. H3 **no** es una solución validada. **No declarar resuelto.** El test F2 (vblank 60, TSC fix, gameplay) quedó lanzado; su resultado debe registrarse antes de concluir nada sobre H1.

**Mecanismo TSC (demostrado estáticamente; causalidad NO demostrada):** el ELF contiene un reloj de milisegundos `rdtsc / divisor`, divisor en `0x153D958` = **3.000.000 ticks/ms (3,0 GHz, PS4)**, **nunca calibrado** por el juego (IDA: sin escrituras; 127 xrefs en 107 funciones, todas de lectura). Con el TSC host de FX (4,013 GHz) ese reloj avanza a 4,013/3,0 = **1,338×**. **Ojo:** no está demostrado que esas 127 referencias gobiernen el timestep de animación/física; hay que rastrear qué funciones concretas las usan y qué otras fuentes de tiempo existen.

**Parche (`gow3_pc/src/probe.c::calibrate_guest_tsc`, gateado por `GOW3_TSC_PATCH`):** reescribe el divisor a la frecuencia TSC medida del host (4.013.388) → reloj del juego a 1,0×. Verificado activo en run corregido (log: «guest TSC divisor 3000000 -> 4013388 ... the game's 3.0 GHz default ran 1.338x fast»). Desactivado con `GOW3_TSC_PATCH=0`.

**Evidencia A/B (`analysis/tlou/speed-experiments/`):**
- **H1 (VBlank) descartada para la cortina del título:** E1 (vb480) vs E2 (vb60), mismo TSC/fps → cortina idéntica (autocorr 8,9 s en ambos). VBlank no controla la velocidad.
- **Cortina del título no es simulation-driven:** E1 (divisor 3,0 MHz) vs E3 (divisor 4,0 MHz) → cortina idéntica (warp k=1,000 corr 0,811 vs k=1,338 corr 0,193). Se verificó que E1/E3 diferían solo en el divisor (logs `boot.log` de cada exp).
- **Audio a 1× (no acelerado):** transitorios (click del clicker) ≈8/s tanto en build bug (G1b: 8,33/s) como fix (G2: 7,94/s); ratio 1,05≈1,0, no 1,338. El audio es un loop fijo/real-time: mide el audio (que nunca estuvo acusado de ir rápido), no la velocidad de simulación.
- **Lo que NO demuestra:** el drift A/V de 0,338 s/s es una *predicción* de H3, no una medición (no hay evento común limpio entre vídeo y audio en las escenas alcanzables). Con el parche aplicado el síntoma persiste, por lo que la predicción de H3 no se confirma en lo observado.
- **G1/G2 (gameplay):** no contienen una comparación visual válida de la velocidad de las animaciones en gameplay (G1 entró con input de walk que altera la escena; G2 se quedó esperando el QTE; no hay evento común medible). Queda pendiente una escena reproducible de movimiento libre.

**Pendiente (sin confirmación visual directa):**
- A/B visual de animación de gameplay (free-roam): bloqueado por el QTE del Prólogo (los pacs de cuadrado no lo superan; run G5 quedó en pantalla negra). La evidencia de mecanismo (reloj `rdtsc/divisor` + 127 xrefs) es fuerte **pero no es causalidad**: no se midió el ciclo de caminata y el síntoma persiste con el parche aplicado.
- Medición directa del drift A/V (evento común a lo largo del tiempo): no hay evento común limpio en las escenas alcanzables.
- **Regression en Ryzen 7950X (PRÓXIMO):** TSC host ≈4,500 GHz → factor esperado **1,500×** en el bug (no 1,338×). Si el factor medido en Ryzen es ≈1,5× (y el parche lo pone a 1,0×), el mecanismo TSC queda confirmado de forma decisiva (el factor varía con el TSC, no es constante).

**No repetir como hechos:** «es 2×» (medido: 1,338× en FX, 1,5× esperado en Ryzen); «VBlank es la causa» (descartada para la cortina); «el factor es constante entre equipos» (debe variar con el TSC).

## 1. Síntoma y entorno

- El juego inicia en Linux y puede llegar al menú y al gameplay.
- Animaciones y/o simulación parecen avanzar demasiado deprisa. El factor no está medido; no escribir «2× confirmado» ni ajustar un factor 2 sin medir.
- El audio suena a velocidad/pitch aparentemente normales, pero queda desincronizado respecto a la imagen. Aún no se ha establecido si el desfase crece con el tiempo o es casi constante.
- El usuario probó VSync ON/OFF: no cambia el síntoma.
- Reproducido por observación en dos equipos Arch Linux:
  - AMD FX-8350 + NVIDIA GTX 1060 3 GB.
  - AMD Ryzen 9 7950X + NVIDIA RTX 3090 24 GB.
- El usuario aclara que «el doble» es una estimación visual, no una medición con cronómetro.

**Pregunta de caracterización pendiente:** medir si el A/V drift crece progresivamente (posible diferencia de frecuencia entre relojes) o si hay un offset casi fijo (posible latencia de cola/búfer).

## 2. Evidencia directa del repositorio y de los logs

### 2.1 Inventario de `analysis/tlou/attempts/`

En el tar.gz hay **57 `boot.log`**:

- 30 arranques con `VideoOut: vblank 480 Hz, frame limit 30 FPS`.
- 27 arranques con `VideoOut: vblank 480 Hz, frame limit 60 FPS`.
- **Ninguno** tiene `vblank 60 Hz` (ni otro valor explícito).
- Por tanto, los logs no comparan VBlank 480 contra 60 Hz.

En sesiones estables a 30 FPS se ven normalmente `Frame stats: ~30 FPS` y `Guest flip rate: ~30/s submitted ... ~30/s shown`. Esto prueba que el límite/presentación de la sesión estaba a 30, no que la simulación interna se actualizara exactamente 30 veces/s.

Los logs contienen además tres pruebas con argumentos guest `forcefps`:

- `boot-1795112349162170045`: `forcefps 30`.
- `boot-1795112475455143921`: `forcefps 60`.
- `boot-1795114966476289314`: `forcefps 30`.

Los logs muestran `Guest arguments` y `game argv[1/2]`, es decir, los argumentos llegaron a `main`. **No hay en esos logs una confirmación de que el parser aceptara/aplicara el comando ni una medida de la frecuencia de game updates.** Los FPS/flip siguen limitados a ~30, por lo que esos datos no bastan para evaluar `forcefps`.

### 2.2 VBlank no es lo mismo que presentar frames

Archivos:

- `tools/probe_tlou.py:648-652`
- `gow3_pc/gpu/shim/core/emulator_settings.h:32-43`
- `gow3_pc/gpu/shadps4/core/libraries/videoout/driver.cpp:1004-1048,1072-1083,1206-1248`
- `gow3_pc/gpu/shadps4/core/libraries/videoout/video_out.cpp:104-128,400-409`

Comportamiento relevante:

1. `probe_tlou.py` establece `GOW3_VBLANK_HZ=0` por defecto.
2. `GetVblankFrequency()` interpreta 0 como **480 Hz**; un valor positivo se usa literalmente.
3. `PresentThread()` obtiene el periodo base de VBlank (1/frecuencia). `frame_limit` determina cuándo acepta/presenta una solicitud de flip.
4. En cada ciclo VBlank, `driver.cpp` dispara los eventos registrados en `vblank_events`, incrementa `vblank_status.count` y notifica `vblank_cv` (líneas 1231-1248), independientemente de si ese ciclo presenta un frame nuevo.
5. `sceVideoOutAddVblankEvent()` registra eventos VBlank en equeue; `sceVideoOutWaitVblank()` espera a que cambie `vblank_status.count`.

Por tanto, con `vblank=480 Hz` y `frame_limit=30 FPS`, el guest puede recibir 480 notificaciones/despertares VBlank por segundo mientras solo se presentan ~30 imágenes. Si el motor utiliza VBlank para avanzar animación, física, un scheduler o un contador de tiempo por tick, puede avanzar mucho más deprisa que la presentación. **Esta es una hipótesis fuerte y directamente comprobable; aún no está probada.**

Prueba de contexto ya anotada por el propio proyecto: `tools/probe_tlou.py:648-652` documenta que una sesión del usuario el 24-10-2026 con VBlank a 60 Hz dio 30–34 FPS en zonas pesadas y velocidad de juego normal. El mismo comentario describe 480 Hz como modo de presentar en cuanto se completa un frame. Repetir esa prueba con el build y escena actuales.

### 2.3 El límite de presentación está separado de VBlank

- `gow3_pc/gpu/shim/gow3_settings.h:68-76`: POSIX define `fps_limit` inicial en 30; Windows en 60. El comentario supone que el motor de este título es frame-based a 30, pero hay que validar esa premisa con el juego ejecutándose.
- `gow3_pc/gpu/shim/gow3_graphics.h:77-85`: `EffectiveFps()` y `PresentLimit()` son helpers de límite gráfico; no demuestran que se controle el timestep del guest.
- `driver.cpp:1031-1048`: el hilo de presentación consulta el límite y muestra `VideoOut: vblank N Hz, frame limit M FPS`.
- El menú puede cambiar el límite en vivo salvo que `GOW3_FPS_LIMIT` fuerce un límite fijo (`driver.cpp:1033-1042`).

**No confundir:** `Frame stats`, `Guest flip rate` y `FLIP QUEUE` describen presentación/espera de flips; no cuentan todos los pasos internos de animación o física.

### 2.4 Equeue/FLIP QUEUE

`gow3_pc/gpu/shadps4/core/libraries/kernel/equeue.cpp:45-84,649-676` instrumenta llamadas a `sceKernelWaitEqueue()` cuando `GOW3_FRAME_STATS` está activo.

En sesiones estables a 30 FPS, los logs muestran `FLIP QUEUE` alrededor de 30 waits/s y 30 events/s, aproximadamente 1 evento por wait. Esto debilita la hipótesis concreta de que el main loop esté drenando dos eventos FLIP por cada despertar o que la cola principal esté despertando 60 veces/s mientras presenta 30.

Pero NO descarta:

- varios game updates dentro de un mismo wake/flip;
- otro thread de simulación independiente;
- actualizaciones impulsadas por VBlank/equeue distinto de `FLIP QUEUE`;
- un timestep incorrecto aunque el loop espere 30 veces/s.

`GPU NdJob Sync Queue` a cientos/miles de waits/s es sincronización de trabajo GPU; no interpretarlo como frecuencia de simulación.

### 2.5 Clocks del runtime y `GOW3_CLOCK_TRACE`

Archivo: `gow3_pc/src/runtime_kernel.c`.

- `host_clock()` mapea ids PS4 a relojes host (`:103-113`). id 4 es `CLOCK_MONOTONIC`; id 15 (`PROCTIME`) también es monotónico; id 2 es `CLOCK_PROCESS_CPUTIME_ID`.
- `clock_read()`/`clock_tick()` (`:115-160`) cuentan llamadas a hooks de reloj cada 5 s.
- `process_time()` (`:175-180`) devuelve tiempo transcurrido en microsegundos.
- `process_time_counter()` (`:181-190`) devuelve nanosegundos monotónicos en Linux; su frecuencia implícita es 1 GHz.
- En Linux `read_tsc()` (`:193-200`) cae a `process_time_counter()`; la frecuencia comunicada por `tsc_frequency()` es 1 GHz.
- `GOW3_TIME_SCALE`/`scale_timespec()` (`:84-99`) escala clocks HLE, no el paso de presentación ni cualquier `RDTSC` x86 ejecutado directamente por el ELF.

Hay cuatro logs con `GOW3_CLOCK_TRACE` (ids de carpeta):

- `boot-1795109857702246843`
- `boot-1795110262976713659`
- `boot-1795174483078562422`
- `boot-1795185364848827160`

Las tasas de lectura de ids 4, 15, 32 y 33 varían bastante; no equivalen automáticamente a Hz de animación. Los ids 32/33 se incrementan al llamar a las funciones internas de contador/TSC, incluidas llamadas desde el runtime (por ejemplo seguimiento de páginas GPU y timestamps); no cuentan instrucciones `RDTSC` nativas del ELF. `GOW3_CLOCK_TRACE` **no es un contador de game updates**.

`Guest clock check` en `driver.cpp` mide cuánto avanzan `sceKernelGetProcessTime()` y `sceKernelReadTsc()` en una ventana de pared. En runs normales da aproximadamente tiempo real. Eso reduce la probabilidad de que la implementación HLE monotónica esté corriendo a 2×, pero no descarta clocks no interceptados, timestep fijo, VBlank, ni lecturas raw del TSC.

Un log (`boot-1795110262976713659`) muestra que `GOW3_TIME_SCALE=2` reduce los clocks HLE a ~2.5 s por ventana de 5 s. El cambio de periodo observado con el antiguo analizador de luma del menú no fue concluyente. No convertir este test en una solución: escala relojes HLE pero no sincroniza automáticamente toda la lógica ni modifica un `RDTSC` inline.

### 2.6 RDTSC nativo / dos dominios de reloj

Archivo principal: `gow3_pc/src/runtime_kernel.c:47-82,181-200`.

- `GOW3_NATIVE_TSC` se lee/calibra en `remember_start()` únicamente dentro de `#ifdef _WIN32` (`:56-82`).
- El launcher fuerza `GOW3_NATIVE_TSC=1` (`launcher/tlou_launcher.pyw:2175-2183`) y `probe_tlou.py` también lo pone por defecto (`:666`), pero en el build Linux ese valor no activa la ruta `__rdtsc()` HLE: el código POSIX devuelve el contador monotónico sintético y 1 GHz.
- Esto afecta a las funciones HLE `sceKernelReadTsc`/`sceKernelGetTscFrequency`; **no intercepta la instrucción x86 `RDTSC` incrustada en el ELF del juego**.

Medición del usuario (RDTSC host comparado con `CLOCK_MONOTONIC_RAW`, espera ~2 s):

- FX-8350: 8.027.151.839 ticks / 2,000085 s = **4.013.404.902 Hz**.
- Ryzen 9 7950X: 9.000.341.100 ticks / 2,000067 s = **4.500.018.994 Hz**.

Estas mediciones son de la máquina host; por sí solas no prueban que TLOU use esa lectura para la simulación. La diferencia entre ambos es ~12,1 %, así que no asumir que explica una aceleración idéntica ni que el error visual sea exactamente 2×.

Se desensambló aparte `tlou_eboot_1.11.elf` (no está incluido en el tar.gz de código/logs; debe proporcionarse al LLM si necesita repetir el análisis): se contaron **435 instrucciones `RDTSC`**. Una rutina alrededor de `0xD2F0-0xD35F` ejecuta `RDTSC` en `0xD312`, resta un valor anterior y divide la diferencia por un global al que se accede en `0x1544488`. Esto demuestra que hay uso de RDTSC raw, pero **no demuestra que esa rutina gobierne animaciones**. Hay que rastrear callers, todas las referencias/escrituras del global y la ruta desde esa rutina a animación/física.

El comentario en `runtime_kernel.c:191-192` recuerda un problema real de TSC raw desincronizado entre cores en otro host (Xeon E5-2680 v4, diferencia observada de 3,9 s al migrar). No activar indiscriminadamente `__rdtsc()` en Linux ni asumir que el TSC raw está sincronizado en todos los equipos. *(21-11: la ruta nativa llegó a Linux gateada por `GOW3_NATIVE_TSC=1`, con aviso de CPUID si el TSC no es invariante; §11.)*

### 2.7 Audio

Archivo: `gow3_pc/src/runtime_audio.c:54-62,140-153,184-253,261-269`.

- `now_ns()` y `sleep_until()` usan `CLOCK_MONOTONIC`/`clock_nanosleep`.
- SDL se abre a 48 kHz (`:144-150`). El periodo se calcula como `frames * 1e9 / 48000` (`:192-194`). Para 1024 frames son 21,333 ms.
- `audio_outputs()` espera una sola vez por todos los puertos (`:261-269`); la entrega/pacing del audio está separada del ritmo de presentación.
- `GOW3_AUDIO_STATS` mide número de búferes, intervalo máximo entre llamadas, mínimo de bytes encolados y starvation (`:208-223`). No mide timestamps de contenido ni el desfase audiovisual de extremo a extremo.

Solo **un** log de los 57 activa esas estadísticas: `boot-1795093130933902348`. Esa sesión empezó en `vblank 480 Hz, frame limit 60 FPS`; después el log indica seguimiento del refresco de pantalla (~59,95 Hz). No corresponde a una corrida verificada a 30 FPS con el bug. En 37 ventanas por puerto:

- esperado: 234 búferes/5 s; media observada: 234,32 para ambos puertos;
- puerto 0: max-gap mediano 40,05 ms, máximo 435,22 ms; 2 ventanas con `starved=1`;
- puerto 17: max-gap mediano 28,52 ms, máximo 421,33 ms; 2 ventanas con `starved=1`.

La cadencia media es nominal en esa sesión; aparecen stalls puntuales. Esto no demuestra que el audio esté perfectamente sincronizado, ni que esos stalls causen la velocidad visual. La experiencia del usuario es que el audio suena normal pero pierde sincronización.

`driver.cpp:1085-1090` contiene una nota histórica: películas/cutscenes reproducidas a 480 Hz se aceleraban y desincronizaban con su audio; por eso las movies se muestran a 60 Hz. Es evidencia de que el VBlank puede afectar el A/V en el port, pero el comentario se refiere a vídeo pregrabado, no prueba que sea la causa del gameplay.

## 3. Hipótesis actuales, por prioridad

### H1 — La simulación/animación consume VBlank a 480 Hz (DESCARTADA para la cortina del título — §0.5)

> **Actualización 20-11:** A/B E1 (vb480) vs E2 (vb60) con mismo TSC/fps: cortina del título idéntica (autocorr 8,9 s en ambos). VBlank no controla la velocidad de la cortina. Sigue pendiente evaluarla para gameplay; la causa de la aceleración de gameplay NO está identificada (la causalidad TSC no está demostrada, §0.5).

Mecanismo: el juego registra `sceVideoOutAddVblankEvent()` o usa `sceVideoOutWaitVblank()`. Como el runtime genera 480 VBlanks/s aunque presente solo 30 FPS, puede avanzar lógica/temporizadores 480 veces/s, o interpretar mal los deltas de VBlank. A 60 Hz, el número de eventos quedaría en el ritmo esperado.

Evidencia: ruta de código `driver.cpp`/`video_out.cpp`; valor predeterminado 480; comentario de `probe_tlou.py` sobre la sesión normal a 60 Hz. Ninguno de los 57 logs prueba 60 Hz.

### H2 — Timestep/game-frame interno incorrecto o doble actualización (ALTA; no probada)

El ELF contiene nombres de símbolos/cadenas como `FrameSpawnerJob`, `Single Game Frame`, `Double Game Frame`, `Single Render Frame`, `PhysicsUpdate`, `GameFramePreRenderUpdate`, `GameFramePostRenderUpdate`, `NpcPostRenderUpdateAfterHavokStep`, `Target Framerate`, `Force Fixed Framerate`, `Minimum Framerate`, `forcefps`.

Son pistas de una arquitectura que distingue game/update frames y render frames; las cadenas no bastan para determinar el timestep real. Posibles fallos: dos pasos de simulación por flip; game-frame a 60 con render a 30; dt fijo incorrecto (1/60 frente a 1/30); VBlank event que dispara el `FrameSpawner`; usar número de VBlank donde se esperaba número de game frames.

### H3 — TSC raw del ELF dividido por un divisor no calibrado (**CAUSA RAÍZ CONFIRMADA y arreglada 21-11 — §11**; con el mecanismo corregido: el divisor en runtime es la frecuencia que la HLE reporta, 1e9 en Linux, no el 3.000.000 de imagen)

> **Revisión 20-11 (tras el testeo del usuario):** el mecanismo está demostrado estáticamente: el reloj de ms del juego es `rdtsc / divisor` con divisor en `0x153D958` = 3.000.000 ticks/ms (3,0 GHz, PS4), **nunca calibrado por el juego** (IDA: sin escrituras; 127 xrefs en 107 funciones), `RDTSC` nativo (host) → reloj del juego a 1,338× en FX-8350 (TSC 4,013 GHz), ≈1,5× en Ryzen 7950X (TSC ≈4,500 GHz). El parche `probe.c::calibrate_guest_tsc` (`GOW3_TSC_PATCH`) reescribe el divisor al TSC host y **se aplica correctamente según el log** (divisor 3000000 → ~4013372-4013388). **Pero NO está demostrado que esa discrepancia cause la aceleración de las animaciones: con el parche aplicado (reloj a 1,0× según log) el usuario sigue viendo el juego rápido.** H3 **no** es una solución validada; **no** declarar resuelto. Queda por determinar: (a) qué funciones concretas (animación, física, scheduler) consumen ese reloj entre las 127 referencias — NO asumir que todas sean el timestep principal —, y (b) si la aceleración percibida procede de otra fuente de tiempo. (Nota: la dirección de rutina `0xD312` del informe original era de otro build; en este ELF el divisor es `0x153D958`.)

Mecanismo original (texto anterior, conservado): si el ELF ejecuta `RDTSC` raw pero divide por una frecuencia hardcodeada/calibrada incorrectamente, mezcla dominios incompatibles. Eso es lo demostrado estáticamente; que explique el síntoma observado **no** está demostrado.

### H4 — Otro clock/timer HLE o unidad incorrecta (MEDIA-BAJA)

Revisar `clock_gettime`, `sceKernelGetProcessTime`, `sceKernelGetProcessTimeCounter`, `sceKernelReadTsc`, `sceKernelGetTscFrequency`, `nanosleep`, `usleep`, `clock_nanosleep` y conversiones de tick/us/ns. id 2 (`CLOCK_PROCESS_CPUTIME_ID`) merece inspección si el ELF lo consulta; la traza aportada no da una frecuencia de simulación. También comprobar diferencias entre FreeBSD/PS4 y Linux en los identificadores `clock_id`.

### H5 — Audio: cola/latencia o relojes A/V distintos (MEDIA para el desfase; baja para la animación)

Audio host-paced a 48 kHz, juego visual posiblemente impulsado por VBlank/sim. Determinar si offset A/V es creciente o fijo. Una cola de SDL/PipeWire puede añadir latencia; no explica por sí misma que el juego visual vaya rápido.

### H6 — Presentación/refresh/GPU como causa única (BAJA)

El cap a 30 es real en los logs y VSync ON/OFF no cambia el síntoma según el usuario. Aun así, la presentación afecta cuándo recibe eventos el juego en algunas rutas; por eso se debe mantener controlada en el A/B de VBlank, no descartarla en términos absolutos.

## 4. Hipótesis debilitadas, descartadas o que no deben repetirse como hechos

- **«Es exactamente 2×»:** no hay medición; el usuario solo estima que es el doble o quizá más.
- **«El audio también corre rápido»:** corregido por el usuario; audio suena a ritmo normal y queda fuera de sincronía.
- **«Capar presentación a 30 arregla la lógica»:** no; el usuario sigue viendo animaciones aceleradas a 30 FPS.
- **«El flip principal está a 60 mientras se muestran 30»:** logs estables muestran ~30 guest flips/s y ~30 mostrados/s.
- **«El FLIP QUEUE tiene 2 eventos por wake»:** en ventanas estables observadas suele ser ~30 waits/s, ~30 events/s, 1 por wait. No descarta varios updates internos por wake.
- **«GOW3_CLOCK_TRACE mide los ticks de simulación»:** falso. Cuenta llamadas a API/hooks HLE y contadores runtime; no cuenta RDTSC raw ni cada update.
- **«Los clocks HLE a tiempo real descartan cualquier fallo temporal»:** demasiado fuerte. Reducen probabilidad de que el simple `CLOCK_MONOTONIC` HLE esté a 2×, pero no descartan VBlank, dt, raw TSC, clock no observado o unidades incorrectas.
- **«GOW3_TIME_SCALE=2 demuestra que no es timing»:** no. Solo escala los relojes HLE y la vieja prueba del menú midió una señal de luma ambigua; no prueba el timestep de animación.
- **«La luma total del menú demuestra periodo normal»:** no. El pulso del texto puede dominar la señal y no representar el movimiento de la cortina.
- **«`forcefps` está aplicado»:** logs solo prueban que argumentos llegan a argv; hay que verificar el parser/efecto.
- **«GOW3_ENGINE_FPS es un control funcional de TLOU»:** búsqueda en el árbol actual encuentra `engine_fps`/`applied_engine_fps` en `gow3_settings.h` y `EffectiveFps()` en `gow3_graphics.h`, pero no una ruta activa que lea `GOW3_ENGINE_FPS` y escriba un global del ELF TLOU. No asumir que ese ajuste controla el motor.

## 5. Plan de pruebas priorizado

### Prueba 1 — A/B VBlank 480 vs 60, cap de presentación idéntico (hacer primero)

Usar la misma máquina, build, partida/escena, ruta y estado VSync. Cerrar el juego entre ejecuciones. Mantener `fps_limit` fijo en 30. `GOW3_FPS_LIMIT=30` evita que el menú cambie el límite durante el A/B.

Antes, comprobar lo que realmente hereda el launcher:

```bash
GOW3_VBLANK_HZ=60 GOW3_FPS_LIMIT=30 python3 tools/run_game.py --print
```

Verificar que el JSON de `--print` muestra ambos valores. `launcher/tlou_launcher.pyw:2175-2183` copia el entorno y luego aplica overrides; un override persistido puede reemplazar la variable exportada. Ajustar ese override si hace falta.

Baseline 480 Hz:

```bash
GOW3_VBLANK_HZ=0 GOW3_FPS_LIMIT=30 GOW3_FRAME_STATS=1 python3 tools/run_game.py --seconds 120
```

Prueba 60 Hz:

```bash
GOW3_VBLANK_HZ=60 GOW3_FPS_LIMIT=30 GOW3_FRAME_STATS=1 python3 tools/run_game.py --seconds 120
```

Confirmar al inicio de cada `boot.log`:

```text
VideoOut: vblank 480 Hz, frame limit 30 FPS
```

frente a:

```text
VideoOut: vblank 60 Hz, frame limit 30 FPS
```

Comparar velocidad visual de la misma animación y audio-sync. No cambiar más variables. Repetir A/B al menos dos veces.

**Interpretación:**

- Si 60 Hz normaliza el movimiento y 480 Hz lo acelera, H1 queda fuertemente confirmada. Diseñar fix en la frecuencia/eventos VBlank o en cómo el juego consume esos eventos; no imponer un multiplicador de animación.
- Si ambos se ven igual, H1 pierde peso; continuar con instrumentación de updates y TSC.
- Si 60 Hz introduce judder/caídas pero no corrige velocidad, separar pacing de lógica.

Después, como control secundario, probar `GOW3_VBLANK_HZ=120` (mismo límite 30) para ver si la velocidad depende linealmente o por umbral de VBlank. No empezar por valores arbitrarios; 60 Hz es el control principal.

### Prueba 2 — Medir A/V drift, no solo escuchar

Usar una escena con un sonido/evento visual repetible (pasos, golpe, disparo, movimiento de boca o cutscene). Grabar vídeo y audio juntos desde el principio y tras 1–2 minutos.

- Offset similar al principio y al final: latencia fija/cola de audio probable.
- Offset que crece/decrece con el tiempo: clocks o tasa de producción/consumo distinta.
- Audio a tono/tempo normal y el movimiento visual más rápido: confirma la percepción, pero medir un evento repetible antes de cuantificar el factor.

Ejecutar además:

```bash
GOW3_AUDIO_STATS=1 GOW3_VBLANK_HZ=60 GOW3_FPS_LIMIT=30 python3 tools/run_game.py --seconds 120
```

y equivalente a VBlank 480. Registrar `buffers/5s`, `max gap`, `min queued`, `starved`. No usar estas estadísticas como sustituto de la medición A/V.

### Prueba 3 — Instrumentar VBlank y game updates

Añadir contadores agregados cada 5 s (no imprimir por evento) en:

- `sceVideoOutWaitVblank()` (`video_out.cpp:400-409`): llamadas, despertares, tiempo bloqueado, por thread si es viable.
- `sceVideoOutAddVblankEvent()` y `driver.cpp:1231-1248`: eventos VBlank disparados y callbacks/equeue que se despiertan.
- `sceVideoOutGetVblankStatus()`/lecturas de `vblank_status.count` (`video_out.cpp`, buscar función exacta): cuántas veces consulta el guest.
- `sceKernelWaitEqueue()` por nombre de cola (`equeue.cpp:649-676`): ya hay instrumentación, ampliarla para identificar eventos VBlank, no solo `FLIP QUEUE`.

Objetivo: conocer si el guest recibe 480 VBlank/s y qué thread/cola reacciona a ellos. Identificar el nombre de la equeue del evento VideoOut.

### Prueba 4 — Contar actualizaciones de simulación por segundo y por flip

En el ELF/disassembly localizar callers y puntos de entrada asociados a:

- `FrameSpawnerJob`
- `Single Game Frame`, `Double Game Frame`, `Single Render Frame`
- `GameFramePreRenderUpdate`, `GameFramePostRenderUpdate`
- `PhysicsUpdate`, `NpcPostRenderUpdateAfterHavokStep`

Poner instrumentación en los hooks/funciones alcanzables (o breakpoints/log counters del debugger) y contar cada categoría por segundo real host y por flip. Registrar dt recibido/derivado, acumulador, número de substeps y thread.

No concluir que las cadenas demuestran frecuencia. Hay que inspeccionar flujo de control y llamadas.

### Prueba 5 — Verificar `forcefps`

Ya existen tres logs con `GOW3_GUEST_ARGS="forcefps 30"` o `"forcefps 60"`, pero solo confirman argv. El launcher/probe pasa cada palabra como `--guest-arg` (`tools/probe_tlou.py:712-722`).

- Inspeccionar parser `t1ps4 <levelname> ... forcefps <number>` en el ELF: orden exacto de parámetros, cuándo se llama, si `forcefps` requiere un comando/level inicial.
- Añadir log/hook o leer la global modificada para probar que el valor realmente se aplicó.
- Comparar `forcefps 30` y `forcefps 60` con VBlank 60 y frame cap30; mantener el resto idéntico.
- No declarar resultado por `Frame stats`/Guest flips: son presentación, no game update rate.

### Prueba 6 — Trazar RDTSC raw y el global de frecuencia

Si se analiza `tlou_eboot_1.11.elf`:

1. Analizar las 435 instrucciones RDTSC; identificar las que están en rutas de animación, scheduler, physics, timer o frame spawner.
2. Seguir todos los xrefs al global `0x1544488`, localizar sus escrituras/inicialización y su valor en runtime.
3. Analizar callers de la rutina `0xD2F0`/`RDTSC@0xD312` y su retorno/uso.
4. Comprobar si el global procede de una API de frecuencia (`sceKernelGetTscFrequency`, equivalente) o de una constante/calibración.
5. Comparar raw RDTSC con HLE `sceKernelReadTsc` y la frecuencia comunicada; detectar mezcla de unidades.

El ELF no está incluido en el tar.gz; el binario usado en el análisis previo se llama `tlou_eboot_1.11.elf`. Si el LLM no tiene ese ELF, solicitarlo antes de afirmar xrefs o direcciones.

### Prueba 7 — Captura cuantitativa de movimiento

Herramientas existentes:

- `tools/exp_anim_speed.py`: captura el menú cada 100 ms y usa movimiento/diferencias de imagen; baja a 160×90. El autocorrelation puede encontrar un armónico o una textura/efecto y no basta por sí sola.
- `tools/port_anim_speed.py`: separa señal del pulso UI y borde de cortina; usa tiempos de archivos para no asumir framerate constante. Preferible al análisis de luma total, pero validar visualmente el punto rastreado.
- `tools/ref_anim_speed.py`: referencia de vídeo; sus valores (pulso 2,50 s, cortina 8,9 s según el script) solo valen si la versión/escena/recorte coinciden.
- `tools/play_capture.py`: captura gameplay a ~4 frames/s (`:96-105`), demasiado baja para medir con precisión animaciones rápidas; modificar para capturar con mayor frecuencia o usar grabación de vídeo sincronizada. No usar 4 FPS como única medición.

Medir un evento/animación repetible en la misma escena de referencia y calcular factor como `duración_referencia / duración_linux`. No asumir que la escena del menú corresponde al ritmo de física.

## 6. Guía para implementar un arreglo sin introducir otro bug

- No fijar `GOW3_TIME_SCALE=2` como arreglo; distorsiona solo relojes HLE y no soluciona VBlank ni raw RDTSC.
- No cambiar `tsc_frequency()` a 4,0/4,5 GHz sin demostrar qué contador lee el consumidor. Si se cambia fuente/frecuencia, deben ser coherentes y documentadas.
- No activar `__rdtsc()` en Linux de forma global sin comprobar sincronización/invarianza entre cores; el código ya documenta un caso de migración entre cores que produjo saltos de 3,9 s.
- No asumir que `GOW3_ENGINE_FPS` funciona: la búsqueda del árbol actual no encuentra implementación activa de ese env var; `engine_fps`/`applied_engine_fps` aparecen como campos (`gow3_settings.h:89-98`) y `EffectiveFps()` está definido en `gow3_graphics.h:77-80`, pero no hay evidencia de que controlen el eboot de TLOU.
- No copiar directamente mecanismos de otro port (p. ej. SpartanPort/God of War III) sin identificar la global y semántica reales del ELF TLOU.
- No modificar el pacing del audio para compensar una animación acelerada. Primero corregir el reloj/update visual y después medir la sincronización A/V.
- Mantener el arreglo mínimo, con feature flag y POSIX-only si es una peculiaridad del runtime Linux; probar también que Windows conserva su ruta actual.
- Añadir tests/logs que comparen VBlank, game updates, flips y audio con reloj host monotónico; retirar o aclarar comentarios de diagnóstico antiguos cuando ya no describan el comportamiento.

## 7. Criterios de aceptación

1. Misma escena reproducible, movimiento a velocidad equivalente a la referencia (factor medido, idealmente dentro de ±2–3 %; no usar estimación visual como métrica final).
2. El resultado no depende accidentalmente de si la GPU/CPU es el FX-8350/GTX 1060 o el 7950X/RTX 3090.
3. Presentación permanece estable al límite elegido; no confundir FPS mostrados con game updates.
4. Audio con pitch/tempo natural y A/V sync medido; especificar si hay offset fijo aceptable o drift acumulativo.
5. Sin saltos de reloj al migrar threads, temporizadores atascados, físicas inestables ni regresiones de VBlank/eventos.
6. Regression en 480/60 VBlank y caps 30/60; Windows sin cambios involuntarios.

## 8. Referencias rápidas (archivos del repo)

- `tools/probe_tlou.py:648-652,666-667,712-722` — defaults VBlank/TSC/stats y argumentos guest.
- `launcher/tlou_launcher.pyw:2175-2183` — construcción del entorno y posible override de variables.
- `tools/run_game.py` — ejecuta el entorno del launcher; `--print` muestra variables efectivas.
- `gow3_pc/gpu/shim/core/emulator_settings.h:32-51` — conversión de `GOW3_VBLANK_HZ=0` a 480 y frame limit.
- `gow3_pc/gpu/shim/gow3_settings.h:65-98` — `fps_limit`, `engine_fps`, `applied_engine_fps`.
- `gow3_pc/gpu/shim/gow3_graphics.h:77-85` — `EffectiveFps` / `PresentLimit`.
- `gow3_pc/gpu/shadps4/core/libraries/videoout/driver.cpp:627-677,768-790,905,1004-1083,1085-1090,1206-1248` — stats, flips, VBlank loop, eventos, comentarios históricos A/V.
- `gow3_pc/gpu/shadps4/core/libraries/videoout/video_out.cpp:104-128,400-409` — registro de evento y espera VBlank.
- `gow3_pc/gpu/shadps4/core/libraries/kernel/equeue.cpp:45-84,649-676` — instrumentación de esperas equeue.
- `gow3_pc/src/runtime_kernel.c:47-82,84-160,175-225,510-512` — HLE clocks, TSC, mapping, sleep y exports.
- `gow3_pc/src/runtime_audio.c:54-62,140-153,184-269` — pacing SDL/audio y estadísticas.
- `tools/exp_anim_speed.py`, `tools/port_anim_speed.py`, `tools/ref_anim_speed.py`, `tools/play_capture.py` — medición/captura de movimiento; ver limitaciones en §5.
- `analysis/tlou/attempts/*/boot.log` — 57 ejecuciones; todas VBlank 480 Hz.
- ELF aparte: `tlou_eboot_1.11.elf` — no incluido en el tar.gz; 435 `RDTSC` contadas, rutina en torno a `0xD312` pendiente de análisis de callers/global.

## 9. Instrucción para el LLM que reciba este informe

Trabaja sobre el árbol real del repo y verifica los números/líneas antes de editar. La Prueba 1 (VBlank A/B) ya se ejecutó (E1/E2, §0.5: descartada para la cortina del título). Siguiente: seguir la estrategia de §10 (revisión 20-11, tras comprobar que el síntoma persiste con el parche TSC aplicado). No propongas un parche basado solo en las cadenas del ELF, los contadores de `GOW3_CLOCK_TRACE` o el TSC medido en host. Si encuentras un candidato, añade instrumentación mínima que demuestre la frecuencia del update/timestep afectado, demuestra causalidad con un A/B y solo entonces implementa el cambio. No inventes resultados de pruebas que todavía no se hayan ejecutado.

## 10. Estrategia de investigación (revisión 20-11-2026, tras el testeo del usuario)

Contexto: el síntoma (animaciones demasiado rápidas) **persiste con el parche TSC aplicado**. El mecanismo TSC es real (reloj `rdtsc/divisor` a 1,338× en FX con el divisor original; parche aplicado según log) pero su causalidad NO está demostrada. La prioridad es encontrar y demostrar la causa raíz del síntoma observado, no validar la hipótesis anterior. No declarar resuelto; no tratar H3 como solución validada.

1. **Cortina del menú (subsistema de referencia):** determinar si pertenece al reproductor multimedia (timestamps, PTS, timebase), a una secuencia de vídeo o al motor de animación. Inspeccionar timestamps, PTS, timebases, relojes y conversiones temporales de la ruta correspondiente. La cortina Linux (8,9 s en E1, con el bug) coincide con la referencia PS4 (8,9 s, `tools/ref_anim_speed.py`) → es una animación a velocidad correcta; identificar su reloj da un contraste directo con el subsistema acelerado.
2. **Gameplay: escena reproducible de movimiento libre.** No usar los fades del QTE ni el ritmo del audio del clicker como métricas de velocidad. Medir una animación repetible (p. ej. ciclo de caminata) en la misma escena con el mismo input.
3. **IDA: rastrear los contadores de tiempo.** Callers, xrefs y escrituras de los contadores de tiempo (divisor `0x153D958`, inicio `unk_1B76778`, y otros relojes: HLE process time, vblank, timers). Determinar qué funciones concretas controlan animación y física. NO asumir que las 127 referencias al divisor corresponden todas al timestep principal.
4. **Instrumentación de las actualizaciones:** cada 5 s, usando `CLOCK_MONOTONIC_RAW` del host, registrar el número de actualizaciones de simulación/animación/física y el delta time efectivo.
5. **Verificación por consumidor:** comprobar que el reloj corregido avanza realmente a 1,338× (bug) vs 1,00× (fix) para cada candidato, midiendo valores en runtime. Si el reloj corregido avanza a 1× y las animaciones siguen aceleradas, abandonar esa ruta como explicación de ese subsistema y buscar otra fuente temporal.
6. **A/B cuantitativo:** misma escena, mismos inputs, mismo ejecutable. Comparar parche TSC activado/desactivado (`GOW3_TSC_PATCH=1/0`) y registrar resultados cuantitativos (periodos, ratios).

Restricciones: sin multiplicadores arbitrarios de velocidad; sin modificar el audio para compensar la velocidad visual; sin repetir experimentos sin una nueva hipótesis; sin más cambios especulativos al runtime; mantener los cambios preexistentes del usuario.

## 11. CAUSA RAÍZ DEMOSTRADA Y ARREGLO (sesión 21-11-2026, FX-8350)

### 11.1 Mecanismo completo (verificado en IDA contra `eboot_ida.elf.i64` + runtime + A/B)

1. **Reloj maestro:** `sub_67C0` es el tick principal por frame. Calcula
   `dt = (rdtsc_inline - unk_1B76778[origin]) / qword_153D958[divisor]`,
   escribe `dword_15560F8` (dt del frame, **segundos**) y entrega el dt como
   `double` a
   la update maestra (vtable `qword_156A480` + 0x24); también registra los
   subsistemas (`sub_C6AF50(sub_37E0/sub_2600/sub_28F0/sub_2CA0/sub_54F0/
   sub_5B50, …)`). El frame limiter (espera hasta el tiempo objetivo de frame)
   usa el mismo reloj.
2. **El divisor SÍ se calibra, una vez, en el init:** `sub_C86A70` (único
   caller: `sub_10A60` @ `0x10BC5`) ejecuta
   `divisor = sceKernelGetTscFrequency(); origin = rdtsc;`. La escritura es
   indirecta (`mov [rbx], rax`), por lo que IDA no la indexa como xref a
   `0x153D958` — de ahí el error de la sesión 20-11 («sin escrituras»). El
   import se identificó por el slot del thunk: `sub_10879E0` salta a
   `[0x1514118]`; el reloc del boot file para ese slot es el **import #448**
   = NID `1j3S3n-tTW4#libkernel` = `sceKernelGetTscFrequency` (tabla
   `gow3_pc/src/import_names.inc`).
3. **La HLE Linux devolvía 1e9:** `runtime_tsc_frequency()` =
   `native_tsc_hz ? native_tsc_hz : 1000000000`; la calibración nativa
   (`GOW3_NATIVE_TSC`) solo existía `#ifdef _WIN32`. Por tanto, en Linux:
   `divisor = 1e9`, y el `rdtsc` inline corre a la frecuencia del host
   (**4.013.358.889 Hz** medido en este FX-8350) → **dt = 4,0134× el real**.
   El valor 3.000.000 que tiene la imagen en `0x153D958` es pisado en el init
   y nunca llega a usarse.
4. **Resultado:** todas las animaciones, la física y la lógica (consumen el dt
   del tick maestro) avanzaban a **4,0134×** el tiempo real en este equipo
   (≈4,5× en el Ryzen 7950X). El audio (SDL, pacing host 48 kHz) no usa ese
   reloj: por eso sonaba a 1× y quedaba desincronizado (drift creciente ≈
   3× el tiempo, consistente con el síntoma reportado).
5. **Por qué el parche anterior (imagen) era un no-op:** `calibrate_guest_tsc()`
   reescribía `0x153D958` en la imagen, pero el init la pisa con el 1e9 de la
   HLE. El A/B 20-11 (parche ON/OFF) quedó idéntico por eso: div=1e9 en ambas.

### 11.2 Correcciones a la revisión 20-11 (qué se entendió mal)

- ❌ «El juego nunca calibra el divisor» → lo calibra en el init con
  `sceKernelGetTscFrequency` (§11.1.2).
- ❌ «Con el divisor original el reloj va a 1,338×» → el divisor en runtime es
  1e9 (de la HLE), no 3.000.000; el factor real del bug es **4,0134×** en este
  FX (4.013 GHz / 1 GHz). (1,338× = 4.013/3,0 solo aplicaría si el divisor
  usado fuera el de imagen, que nunca lo es en Linux.)
- ❌ «E1 vs E3 demuestra que la cortina no depende del divisor TSC» → E1/E3 no
  diferían en runtime (div=1e9 en ambas): el A/B era inválido. No descarta
  nada (y de hecho la cortina del título, a 8,9 s como en PS4, no parece
  simulation-driven, pero eso ya no es relevante para el bug).
- ❌ «H3 no es una solución validada / la causa no está identificada» → H3 es
  la causa, con el mecanismo de §11.1 (frecuencia HLE, no divisor de imagen).
- Unidades: `dword_15560F8` (dt por frame) está en **segundos** (0,0333 =
  30 FPS real; 0,1338 = 4,0134×), no en ms. El «guest_ms=+20.1» del probe
  era 20,1 s de avance del reloj `(rdtsc−origin)/divisor` por 5 s reales
  (campo calculado por el probe, no el global `dword_1556100`, que queda en
  ≈0 en esas escenas: su semántica real no está verificada).
- El factor no es un «~2×» constante: varía con la frecuencia del TSC host
  (÷1e9). En el FX-8350 es 4,0134×; en el 7950X ≈4,5×.

### 11.3 A/B definitivo (mismo ejecutable, misma escena, mismos inputs)

- Protocolo: `analysis/tlou/speed-experiments/timing_gameplay.py` (TLou profile
  completo; Prólogo; caminar 40 s con ly=0). `GOW3_TSC_PATCH=0` en ambas. Solo
  cambia `GOW3_NATIVE_TSC` (override temporal en `launcher.json` para A,
  restaurado). Ejecutable: build 21-11 con el fix (`gow3_pc/out/gow3-probe`).
- Datos: `/tmp/tgA0/` (A: bug) y `/tmp/tgA3/` (B: fix) — `boot.log` con las
  líneas TIMING; meta (sha256 del binario, commit) en `meta.txt`.

| Métrica (probe de timing, ventana 5 s, CLOCK_MONOTONIC_RAW) | A: bug (`GOW3_NATIVE_TSC=0`) | B: fix (`GOW3_NATIVE_TSC=1`) |
|---|---|---|
| `div` (divisor que el juego escribe) | 1.000.000.000 | 4.013.358.889 |
| dt por frame (`dword_15560F8`, s) | 0.1320–0.1467 (mediana ≈0.1338) | 0.0304–0.0387 (mediana ≈0.0333) |
| Avance del reloj de juego por 5 s reales (campo `guest_ms` del probe: (rdtsc−origin)/div) | **+20.1 s** | **+5.0 s** |
| Frames por 5 s | 150 (30.0/s) | 150 (30.0/s) |
| Ratio B/A | — | **4.0134 = 4.013.358.889 / 1e9** |

El tiempo de simulación pasa de 4,0134× a 1,000× exactamente. **Causa raíz
demostrada; arreglo validado cuantitativamente.**

### 11.4 El arreglo (código)

`gow3_pc/src/runtime_kernel.c` (cambio mínimo, espejo del camino Windows):

- `remember_start()`: si `GOW3_NATIVE_TSC=1`, mide el TSC nativo en ambas
  plataformas (Linux: `__rdtsc()` contra `CLOCK_MONOTONIC`, 100 ms; Windows:
  contra QPC, como antes) y avisa (CPUID 80000007H:EDX[8]) si el TSC no es
  invariante. `GOW3_TIME_SCALE` sigue aplicándose sobre la frecuencia medida.
- `read_tsc()` / `process_time_counter()` / `tsc_frequency()` /
  `process_time_frequency()`: con `native_tsc_hz>0` devuelven el par
  rdtsc nativo/Hz (sin el `#ifdef _WIN32`). Con el flag apagado, el
  comportamiento Linux es el de antes (contador monotónico, 1 GHz).
- `tools/probe_tlou.py` ya hacía `env.setdefault("GOW3_NATIVE_TSC", "1")` y el
  launcher fuerza `"1"` por defecto en este host (opción «TSC nativo»,
  `tlou_launcher.pyw` ~:2182) — **el fix queda activo en la sesión normal del
  usuario sin tocar nada más**.
- `probe.c::calibrate_guest_tsc()` (parche de imagen) se mantiene por respeto
  a los cambios preexistentes, pero queda documentado como ineficaz (el init
  del juego pisa el divisor); `GOW3_TSC_PATCH=0` lo desactiva.

### 11.5 Pendiente

1. **Verificación visual del usuario:** sesión normal (launcher → Play) para
   confirmar que animaciones/física van a velocidad normal. Si quedara alguna
   duda, `GOW3_TIMING_LOG=1` da el dt exacto por frame (debe ser ≈33,3 ms a
   30 FPS).
2. Regresión en Ryzen 7950X (esperado: `div` ≈4,5e9, dt ≈33,3 ms; bug ≈4,5×).
3. Decisión con el usuario: mantener `GOW3_NATIVE_TSC` gateado (estado actual;
   el launcher lo pone a 1 en este host) o hacerlo por defecto ON en el
   runtime, o autodetectar TSC invariante.
