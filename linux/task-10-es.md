# Tarea 10 — Iconos de ventana y de recursos, y la fuente del overlay (Fase 2)

> **Estado: COMPLETADA Y VERIFICADA.** Las dos cosas que pide la ruta se verificaron en este
> equipo (Arch Linux, AMD FX-8350, GTX 1060 3 GB, Python 3.14.10, Pillow 13.0.0, una sesión
> Wayland más Xvfb): **(1) el icono de la ventana** — el runtime carga un BMP con
> `SDL_LoadBMP` (`gow3_pc/gpu/shim/window.cpp`, `GOW3_WINDOW_ICON`); en Linux, antes, nada
> establecía esa variable, así que la ventana del juego no tenía icono en la barra de tareas
> de X11, mientras que en Windows la ventana conserva el icono de recurso propio del
> ejecutable y la rama de código queda intacta; y **(2) la fuente del overlay** —
> `GOW3_FONT_PATH` es solo una entrada de *compilación*: `DejaVuSans.ttf` (759,676 bytes) se
> incrusta en `libgow3gpu.so` en la compilación (`.incbin`,
> `gow3_font_ttf`/`gow3_font_ttf_end`), por lo que no existe ninguna ruta de fuente en tiempo
> de ejecución que pueda fallar. Dos cambios pequeños, aditivos y protegidos por plataforma,
> hacen que la rama de Linux funcione de verdad: `tools/probe_tlou.py` convierte ahora el
> `sce_sys/icon0.png` propio del juego en un BMP RGB de 256×256 y lo exporta como
> `GOW3_WINDOW_ICON`, y `set_icon()` del launcher ya no llama a `iconbitmap` (una opción de
> Tk exclusiva de Windows que lanza `TclError` en X11/Wayland): convierte el `.ico` a PNG en
> memoria con Pillow y usa `iconphoto`. Evidencia decisiva: con el probe arrancando el juego
> real bajo X11 (Xvfb), un volcado en vivo del árbol X11 atrapó la propia ventana del juego —
> título *The Last of Us™ Remastered — PC port by Ravencaio*, clase `gow3-probe` — con
> `_NET_WM_ICON` de exactamente un icono de **256×256** cuyo píxel central coincide con el
> `icon0.png` propio del juego. El ventana del launcher también lo lleva (256×256 desde el
> `.ico`, 512×512 desde el PNG del juego). Windows byte-a-byte idéntico: el bloque exacto de
> icono extraído del código comprometido y ejecutado bajo un `sys.platform` simulado pasa
> 5/5 (en `win32` es un no-op que deja el entorno byte-a-byte idéntico). Una brecha de
> arranque hallada de paso se documenta al final (este CPU no puede ejecutar el invitado: la
> tabla de emulación MOVBE es exclusiva de Windows).

## Lo que pedía la tarea

> Verificar los iconos de ventana/recursos entre plataformas (el BMP cargado con
> `SDL_LoadBMP` frente a los recursos de Windows) y la ruta de la fuente del overlay
> (`GOW3_FONT_PATH` en `CMakeLists.txt`).

## Hallazgos de la lectura del código

1. **El icono de la ventana del juego** (`gow3_pc/gpu/shim/window.cpp`, constructor
   `WindowSDL`): `getenv("GOW3_WINDOW_ICON")` → `SDL_LoadBMP` → `SDL_SetWindowIcon` →
   `SDL_DestroySurface`. El bloque *no* está protegido por plataforma: en Windows simplemente
   nada establece la variable, de modo que la ventana conserva el icono incrustado en el
   recurso del ejecutable (el `.rc`), y ese es el comportamiento correcto allí. En Linux,
   antes de esta tarea, tampoco lo establecía nada: la ventana aparecía sin `_NET_WM_ICON`,
   es decir, con el icono vacío/por defecto en la barra de tareas de X11. En Wayland no
   existe ningún protocolo de icono de ventana; aplica el icono de aplicación del compositor.
2. **La fuente del overlay** (`gow3_pc/gpu/CMakeLists.txt`): `GOW3_FONT_PATH` apunta a
   `DejaVuSans.ttf` *en tiempo de compilación*; el `.ttf` se incrusta con `.incbin` y queda
   expuesto como `gow3_font_ttf`/`gow3_font_ttf_end` en `libgow3gpu.so`. El overlay lee la
   fuente de esos símbolos — no hay ningún `fopen` de la fuente en tiempo de ejecución, así
   que el port a Linux no tiene ninguna dependencia de ruta de fuente que romper (no existe
   un modo de fallo de «fuente ausente» que proteger).

## El cambio

Dos archivos modificados (+46/−2), ambos aditivos y protegidos por plataforma; el
comportamiento de Windows es byte-a-byte idéntico:

1. **`tools/probe_tlou.py`** (+31)
   - Nueva `window_icon(game, out)`: convierte el `sce_sys/icon0.png` propio del juego en
     `analysis/tlou/window-icon.bmp` — **256×256 RGB** (el BMP es el formato de icono
     integrado del runtime; 256 se queda lejos del límite de 1 MiB por propiedad de X11,
     ver abajo — la barra de tareas lo escala a su hueco). Devuelve `None` sin la imagen o
     sin Pillow: la ventana simplemente no tiene icono; el arranque no se ve afectado.
   - En `main()`, solo cuando `args.boot and sys.platform != "win32"`:
     `env.setdefault("GOW3_WINDOW_ICON", str(icon))` — un valor presestado desde el entorno
     se respeta. En Windows la función nunca se llama: el ejecutable conserva su icono de
     recurso.
2. **`launcher/tlou_launcher.pyw`** (+15/−2, `set_icon`)
   - `self.iconbitmap(default=str(ICON))` queda ahora dentro de `if sys.platform == "win32"`:
     `iconbitmap` es una opción de Tk exclusiva de Windows; en X11/Wayland lanza `TclError`,
     así que con un `icon.ico` presente, el código antiguo hacía caer el launcher en Linux.
   - No-Windows: Pillow lee el `.ico`, lo re-codifica como PNG **en memoria**, y Tk lo toma
     con `tk.PhotoImage(data=...)` + `iconphoto(True, ...)`. Cualquier fallo cae a la rama
     preexistente (el `icon0.png` del juego como `PhotoImage`), que ya funcionaba.

## Verificación (Arch, FX-8350, GTX 1060, Python 3.14.10, Pillow 13.0.0, Xvfb :97)

1. **La propia ventana del juego, en X11 — la prueba decisiva.** El probe se arrancó con el
   juego real (CUSA00552-1.11, actualización fusionada), `GOW3_WINDOW_ICON=analysis/tlou/window-icon.bmp`
   y `SDL_VIDEODRIVER=x11` forzado (ver el orden de drivers abajo), en Xvfb :97. Un volcador
   pequeño del árbol X11 (`XQueryTree` + `XGetWindowProperty`,
   `/tmp/task10/tree_snap.cpp`) sondeó la ventana raíz y atrapó la ventana del juego en vivo:
   ```
   0x40002f name="The Last of Us™ Remastered — PC port by Ravencaio" class="gow3-probe"
       _NET_WM_ICON: 1 icons, first 256x256, nitems=65538
       center pixel ARGB: 0xfffffffffff5f5f5
   ```
   `nitems=65538` es exactamente `2 + 256·256` (ancho, alto, píxeles ARGB). El píxel central
   comparado con el `icon0.png` propio del juego (512×512 reescalonado a 256×256):
   `(245,245,245)` frente a `(246,246,246)` — una diferencia de 1/255 por el re-muestreo
   LANCZOS: **el icono de la barra de tareas de X11 es el icono propio del juego**.
2. **El orden de drivers de SDL (por qué la prueba exigía cuidado).** En este equipo la
   sesión del usuario es Wayland, y SDL3 prefiere el driver Wayland: sin forzar
   `SDL_VIDEODRIVER=x11`, la ventana del juego se crea en el compositor del usuario
   (`/run/user/1000/wayland-1`) y un sondeo X11 no ve nada — las dos primeras pasadas de
   sondeo «no encontraron ventana» por exactamente esa razón. La rama Wayland de
   `window.cpp` se ejerce igual: un binario `icon_test` que usa la misma secuencia de API
   que el runtime (`SDL_InitSubSystem` → `SDL_CreateWindow` → `SDL_LoadBMP` →
   `SDL_SetWindowIcon`) corre en la sesión Wayland del usuario sin fallar (Wayland no tiene
   protocolo de icono que inspeccionar; el compositor aplica su icono de aplicación).
3. **La disciplina de formato de `SDL_LoadBMP`.** Apuntar `GOW3_WINDOW_ICON` a un PNG (en
   vez del BMP) hace que `SDL_LoadBMP` devuelva `NULL`: la ventana aparece sin icono y nada
   se cae — el BMP es el único formato que el runtime carga, y el código ya es seguro frente
   a cualquier otro formato.
4. **El límite de 1 MiB por propiedad de X11.** Un icono 512×512 almacena 1,048,584 bytes en
   `_NET_WM_ICON` (observado en la rama del PNG del juego del launcher) — en el límite por
   propiedad de X11; 256×256 almacena 262,144 bytes. Por eso el icono del juego se genera a
   256×256, no a 512.
5. **El icono del launcher (Xvfb).** La rama antigua de código con un `icon.ico` presente
   lanzaba `TclError` en Linux (el launcher se caía antes de dibujar). Con el código nuevo la
   ventana del launcher lleva `_NET_WM_ICON`: **256×256** cuando hay
   `launcher/assets/icon.ico` (Pillow ico→PNG), **512×512** sin él (la rama preexistente del
   PNG del juego); el launcher termina con 0 en ambos casos.
6. **Windows byte-a-byte idéntico.** `/tmp/task10/win32_identity_test.py` extrae el bloque
   *exacto* de icono del `probe_tlou.py` comprometido (prueba el código publicado, no una
   copia) y lo ejecuta bajo un `sys.platform` simulado: **5/5 pasan** — `win32`+boot: no-op
   y el entorno se mantiene byte-a-byte idéntico; `win32` sin boot: no-op; Linux+boot: se
   crea el BMP (196,662 bytes) y se establece la clave del entorno; Linux con
   `GOW3_WINDOW_ICON` presestado: `setdefault` lo respeta; Linux sin `icon0.png`: no-op
   elegante.
7. **La fuente.** `nm -S` sobre el `libgow3gpu.so` construido: `gow3_font_ttf`…
   `gow3_font_ttf_end` abarcan **759,676 bytes — exactamente el tamaño de `DejaVuSans.ttf`**
   — y los bytes extraídos del `.so` son idénticos al TTF (comparación byte a byte). La
   fuente es solo una entrada de compilación: en Linux no hay ninguna ruta en tiempo de
   ejecución que verificar o que pueda fallar.

## Brechas halladas de paso (fuera del alcance de esta tarea, documentadas para la Tarea 11)

1. **Este CPU no puede ejecutar el invitado (MOVBE).** El FX-8350 es anterior a Ryzen y no
   tiene MOVBE. La comprobación de CPU del runtime (`gow3_pc/src/probe.c`) se detiene con un
   mensaje limpio en esos CPUs («Intel Haswell 2013 or newer, or AMD Ryzen or newer, is
   needed»); la cadena del probe establece `GOW3_CPU_CHECK=0` porque los conjuntos que faltan
   «se emulan ya» — pero la emulación (la tabla red-zone/ISA; 157 sustituciones de MOVBE
   para este juego) se aplica **solo en Windows** («Red zone: Windows-only protection ignored
   on POSIX»). Resultado: la ventana se crea, el icono se establece (verificado arriba — la
   ventana sobrevive al fallo) y un segundo después el invitado se topa con un MOVBE crudo →
   SIGILL (a veces un SIGSEGV en otra parte; no determinista). Un arranque completo (Tarea
   11) no puede completarse en este CPU; en un CPU Haswell-2013+/Ryzen la comprobación pasa
   de forma nativa y no hace falta tabla. Nada tiene que ver con iconos o con la fuente:
   ambas se verificaron en la ventana en vivo antes del fallo.
2. **Rarezas de Xlib (para las herramientas de prueba).** `XQueryTree` y `XFetchName`
   devuelven `1` en éxito, no `Success` (0); `XInternAtom(display, "_NET_WM_ICON", True)`
   produce un `BadAtom` cuando el átomo aún no es conocido — la bandera correcta es `False`.

## Archivos

- Modificados: `launcher/tlou_launcher.pyw`, `tools/probe_tlou.py` (+46/−2).
- Generado (ignorado por git): `analysis/tlou/window-icon.bmp` (196,662 bytes, 256×256 RGB).
- Artefactos de prueba (no comprometidos, en `/tmp/task10/`): `icon_test.cpp` (API de icono
  de SDL3 en X11 y Wayland), `tk_icon_test.py` (ramas de icono del launcher),
  `win32_identity_test.py` (el arnés de identidad byte-a-byte), `tree_snap.cpp` (volcador de
  árbol/iconos X11), `pillow_check.py` (ida y vuelta BMP/ico de Pillow 13.0.0). `iced_x86`
  1.22.0 se colocó en el `gow3_pc/out/pydeps` ignorado por git del proyecto (dependencia de
  la herramienta red-zone) — sin cambio en el sistema.
