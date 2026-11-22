# Tarea 16 — Paquete para Linux: AppImage grueso sin el juego (Fase 4)

> **Estado: HECHO y verificado en la máquina 1 (FX-8350 + GTX 1060).** AppImage grueso de
> **157 MB** (`dist/TLOU-Remastered-x86_64.AppImage`), sin el juego, entrada por el launcher,
> con 165 librerías del sistema embebidas y los ICD Mesa AMD/Intel: el launcher arranca desde
> la AppImage (detecta el driver NVIDIA del huésped) y el juego **arrancó y corrió 180 s a
> 30 FPS** (draw pipe vivo en `boot.log`) vía `--play --seconds 180`, con los datos en
> `~/.local/share/tlou-remastered`. Faltan: la verificación en la máquina 2 (Ryzen 7950X +
> RTX 3090) y, algún día, una GPU AMD/Intel para probar el embebido Mesa.

## Decisiones (2026-11-21)

1. **AppImage "grueso".** Empaqueta todas las librerías de las que depende el runtime y el
   Python embebido (SDL3, el loader Vulkan, `libav*`, `libstdc++`, libtk/libtcl y las que
   enlaza Pillow), copiadas al AppDir y con RPATH ajustado. El objetivo es que la AppImage
   corra sin más que el kernel y los drivers; no depender del Python ni de las librerías de
   la distro del que la lance.
2. **Sin el juego, obligatoriamente.** La copia del usuario (CUSA00552 + update 1.11,
   ~56 GB aquí) no va en la AppImage; el launcher ya tiene el diálogo para elegir la carpeta
   del juego (`game_folder` en `launcher.json`) y `probe_tlou.py` la recibe por `--game`.
   `tools/package_check.py` (que rechaza ficheros de juego) se mantiene como control.
3. **El punto de entrada es el launcher** (el `.desktop` apunta al `TLOU-Launcher` de la
   AppDir), para que el usuario ajuste sus variables (resolución, FPS, upscaler, carpetas,
   optimizaciones) antes de empezar a jugar. `TLOU-Game` queda como arrancador directo para
   cuando todo esté a gusto.

## Lo que se encontró

El upstream es un mono-repo: el **port de Uncharted** (commit `6036c2a`, ancestro de esta
rama por el merge de 0.4.0) ya resolvió un AppImage "grueso" con `nix bundle` +
`ralismark/nix-appimage` (`gow3_pc/packaging/appimage.sh`, `default.nix`,
`gow3-entry.c`), launcher GTK4/libadwaita con su closure Nix, drivers Mesa embebidos
(radeon/intel) más el ICD NVIDIA del huésped, y los datos en `GOW3_DATA_DIR`
(`~/.local/share/gow3`). Tras el merge, en el árbol de TLOU solo quedó el test
`gow3_pc/tests/test_packaged_vulkan.py` (roto: su módulo `gow3_pc/launcher/gow3_vulkan.py`
no existía aquí).

No se reutiliza la ruta Nix (nix no está instalado en ninguna de las dos máquinas, y el
launcher de TLOU es Tkinter, no GTK4): se empaqueta con `linuxdeploy`/`appimagetool`
sobre el paquete de la Tarea 7. Se **sí** reutiliza, y se adapta, el módulo
`gow3_pc/launcher/gow3_vulkan.py`: ante el punto de entrada de la AppImage, detecta el ICD
NVIDIA del huésped (con su caché de symlinks en `GOW3_DATA_DIR/vulkan/nvidia/<clave>/` para
que el driver dlopen sus librerías de shader), y si no lo hay (GPU AMD/Intel) cae en los
ICDs Mesa que vayan embebidos en la AppImage. Es decir, la AppImage puede cubrir también
las GPU AMD/Intel que hoy no se han probado (limitación de la Tarea 15).

## Implementado (esta sesión)

- `tools/gow3_data.py` (nuevo): la raíz de los datos del jugador: `GOW3_DATA_DIR` si está
  fijada (la AppImage's AppRun la exporta a `~/.local/share/tlou-remastered`, o la que el
  jugador elija), `analysis/` en un checkout o paquete extraído. El layout bajo la raíz no
  cambia: `tlou/` (logs, preparación del boot, grabaciones) y `tlou-user/` (`tlou.ini`,
  `launcher.json`, saves, `cache/`). Solo la variable la mueve (sin detección de AppImage:
  esta sesión de desarrollo corre dentro de la AppImage de Pi, y la detección habría movido
  los datos del checkout a otra parte sin pedirlo).
- `launcher/tlou_launcher.pyw`: `USER`/`INI`/`SWITCHES`/`LOG`/`RUN_LOG` (y el nuevo
  `GAME_RUN_LOG`) salen de `gow3_data.data_root(ROOT)`; igual `GOW3_DUMP_DIR` del perfil.
- `tools/probe_tlou.py`: `out` y `user` (y los `GOW3_GPU_USER_DIR`/`GOW3_CONFIG`/
  `GOW3_GPU_CACHE_DIR` que derivan) salen de la misma raíz; textos de ayuda actualizados.
- `tools/run_game.py`: los logs de respaldo y el `report_failure` usan la raíz.
- `TLOU-Game` y `launcher/TLOU-Launcher`: el log sin terminal va a `$GOW3_DATA_DIR/tlou/`
  (o `analysis/tlou/`), con respaldo en `/tmp` si no hay nada escribible.
- `gow3_pc/launcher/gow3_vulkan.py`: restaurado de `6036c2a` (docstring Tkinter; directorio
  de datos por defecto `tlou-remastered`).
- `tools/package_linux.sh`: el paquete incluye ahora `tools/gow3_data.py` y
  `gow3_pc/launcher/gow3_vulkan.py`.
- `gow3_pc/tests/test_data_dir.py` (nuevo, 3 tests): el default `analysis/`, la variable
  (y su expansión de `~`), y que launcher y probe concuerdan (todos sus paths salen de la
  misma raíz). `python3 -m unittest discover -s gow3_pc/tests`:
  89 tests, 3 errores — los tres preexistentes desde el merge de Uncharted (`test_mods.py`
  espera `gow3_pc/run.py`, `test_save_backup.py` el módulo `save_backup`,
  `test_upscaler_assets.py` `gow3_assets`; ninguno existe en el árbol de TLOU). Antes de
  estos cambios: 76 tests, 4 errores (los mismos tres + `test_packaged_vulkan`).

## Implementado (2026-11-21, tarde) — el empaquetado

- `gow3_pc/packaging/appimage/` (nueva): `build_appimage.sh` (paquete → AppDir →
  linuxdeploy → `dist/TLOU-Remastered-x86_64.AppImage`), `AppRun`,
  `TLOU-Remastered-x86_64.desktop`, `bundle_libs.py` y `make_icon.py`.
- **El paso "grueso" sin patchelf:** `bundle_libs.py` hace el cierre `ldd` de todos los ELF
  del AppDir (runtime, los `.so` de la librería GPU, el Python embebido y sus extensiones
  tkinter/Pillow) más los drivers Mesa radeon/intel (y sus dependencias, libdrm y compañía),
  y lo copia todo una vez a `lib/` — 165 librerías, 419 MiB. No se parche RPATH: `AppRun`
  pone `lib/` en `LD_LIBRARY_PATH` y el loader del huésped y las librerías de glibc **no se
  embebien** (un glibc mezclado con un loader ajeno no cargaría). `AppRun` exporta además
  `GOW3_DATA_DIR` (default `~/.local/share/tlou-remastered`) y
  `GOW3_BUNDLED_VK_DRIVER_FILES` (los ICD Mesa embebidos, para cuando el huésped no tenga
  NVIDIA) y hace `exec` del launcher **a través de `gow3_pc/launcher/gow3_vulkan.py`**;
  `--play` salta la ventana del launcher y arranca el juego.
- **El icono** (`make_icon.py`): del `sce_sys/icon0.png` de la propia copia del jugador
  (`TLOU_APPIMAGE_ICON_GAME`, o el `game_folder` guardado del launcher, o el lugar habitual
  de este port); arte neutro generado si no hay juego, para una AppImage pública.
- **El arranque escribe en los datos, no en el paquete:** `probe_tlou.boot_view` movido de
  `ROOT/analysis/views/` a la raíz de datos (`analysis/views/` en el checkout,
  `$GOW3_DATA_DIR/views/` en la AppImage) — era el último camino de escritura local al
  paquete (el `eboot.elf`, el red-zone y los artifacts ya vivían en `tlou/`).
- **`TLOU_GAME=<carpeta>`** (y el error claro en su vez): el default del `game_folder` en
  un checkout es "el juego junto al repo", que en la AppImage salía `/tmp/CUSA00552` (la
  mount point del runtime). La variable documentada en `AppRun` + `probe_tlou.py --game` y
  el launcher la aceptan, y `probe_tlou.py` falla con un mensaje legible (no un
  traceback) si la carpeta no existe. El launcher sigue siendo el sitio donde el jugador
  elige la carpeta (se guarda en `GOW3_DATA_DIR/tlou-user/launcher.json`).
- `tools/package_check.py`: modo solo-chequeo (`<carpeta> -`, sin zip) para el payload de la
  AppImage; `build_appimage.sh` lo pasa sobre los 1051 ficheros del AppDir (sin fichero de
  juego, sin ruta de esta máquina).
- **El linuxdeploy de Arch es el 1-alpha por plugins** (no el 2.x de antes): no entiende
  `--appimage-type 2`; la invocación es `linuxdeploy --appdir <AppDir> -o appimage`, el
  icono lo quiere en `usr/share/icons/hicolor/256x256/apps/` y escribe la AppImage con el
  `Name` del `.desktop` en el directorio actual (`The_Last_of_Us_Remastered-x86_64.AppImage`),
  que el script mueve a `dist/TLOU-Remastered-x86_64.AppImage`.

### Verificado en la máquina 1 (FX-8350, GTX 1060 3 GB, `GOW3_LOW_VRAM=1`)

- `TLOU-Remastered-x86_64.AppImage --close-after 8`: el launcher abre y cierra, `exit 0`;
  `gow3_vulkan.py` elige el driver NVIDIA del huésped (y dice que el Mesa embebido también
  está); `~/.local/share/tlou-remastered/{tlou,tlou-user,views,vulkan}/` creados.
- `TLOU_GAME=<juego> ...AppImage --play --seconds 180`: boot completo (update 1.11, red-zone
  de 538 funciones, emulación ISA de MOVBE para el FX) y **180,5 s de juego vivo**
  (`"timed_out": true`, draw pipe a 8277 draws/s y FLIP QUEUE a 30/s en `boot.log`).
- `--play --seconds 60` (artifacts ya cacheados): 65,7 s en total (5 s de preparación + 60 s
  de juego). `exit 124` es la señal de éxito por diseño: `probe_tlou.py` devuelve 124 cuando
  el juego sobrevive al límite de tiempo (misma convención que `timeout`).

## Bug de usuario: rectángulos negros intermitentes en la AppImage (2026-11-21, noche)

El jugador veía en la AppImage "rectángulos negros intermitentes que van y vienen" y no al
jugar desde el checkout con las mismas opciones. Causa: la AppImage usa su propia raíz de
datos (`~/.local/share/tlou-remastered/tlou-user/cache/`) y su caché de shaders estaba fría
(5.307 entradas) frente a la del checkout, caliente de días de tests (11.138). Con
`background_shaders` activado (la opción por defecto) los draws cuyo pipeline se está
compilando en segundo plano **se saltan** hasta que termina la compilación: objetos que
aparecen tarde o en negro un instante, y que van y vienen a medida que cada escena introduce
shaders nuevos. La caché del checkout, mucho más completa, apenas compilaba nada nuevo, por
eso no se veía.

Verificado estáticamente (máquina 1, GTX 1060, driver NVIDIA 580.196.07 idéntico en ambos
lados):

- `profile.bin` (el `Shader::Profile` de 64 bytes que vigila la caché, `CheckCacheProfile`)
  es **idéntico** en las dos carpetas: la caché copiada no se aparta por "de otra GPU".
- El otro supuesto candidato, `quarter_depth` (activa en la AppImage, desactivada en el
  checkout: las opciones no eran exactamente las mismas), solo transforma **un** shader
  (`Cs_DownsampleQuarterDepth`, `0x42cb16a5`, `quarter_depth_pass.cpp`) y su variante se
  guarda con nombre propio (`…_qd2.spv`): sin riesgo de entradas caducas en el merge, y su
  efecto es el contorno de las sombras de contacto, no agujeros negros.
- Fix aplicado localmente: la caché caliente del checkout se copió a la raíz de datos de la
  AppImage con `cp -rn` (11.359 ficheros, ~160 MB; lo que ya estaba se conservó).

**Pendiente (decisión de producto):** un usuario nuevo descarga la AppImage sin caché y su
primeras sesiones serán frías (rectángulos mientras se calienta; cada partida lo reduce).
Opciones: (a) empaquetar un semilla de la caché (las ~1.000 pipeline de `warmup-order.bin`
más sus `.spv`/`.key`, ~15-20 MB) que la AppRun copie a los datos en el primer arranque;
(b) solo documentar que en los primeros minutos puede haber objetos que aparezcan tarde;
(c) `background_shaders` desactivado por defecto en usuarios nuevos (travadas en lugar de
negros). La caché completa no es empaquetable (cambia por GPU y por versión del juego).

## Bug de usuario: la AppImage no arrancaba en Bazzite (2026-11-22)

El jugador lanzó `TLOU-Remastered-x86_64.AppImage` en Bazzite (Fedora inmutable) y el
launcher murió:

```
Vulkan: bundled AMD/Intel drivers; no accessible NVIDIA ICD found
_tkinter.TclError: Can't find a usable init.tcl in the following directories:
  /usr/lib/tcl8.6 /tmp/.mount_.../python/lib/tcl8.6 ... /tmp/.mount_.../tcl8.6.16/library
```

Tres fallos reales del empaquetado, los dos primeros enmascarados en Arch (la máquina de
build), donde los caminos de esta máquina sí existen:

1. **Los datos de Tcl/Tk no iban embebidos.** El AppImage traía `libtcl8.6.so`/`libtk8.6.so`
   (cierre ldd) pero no los datos (`init.tcl`, `tk.tcl`, ...): Tcl los busca en el camino
   compilado en `libtcl` al hacer el build (`/usr/lib/tcl8.6`, el de esta máquina). En
   Bazzite ese camino no existe (Fedora: `/usr/lib64`, y el base inmutable ni trae tcl-tk)
   → el launcher moría en `Tk()`. En Arch pasaba porque el camino existe.
2. **`libXss.so.1` no iba embebido** (165 librerías, debía ir en 166): `bundle_libs.py` corrió
   `ldd` en el entorno del build, y este corría dentro de la AppImage de Pi: `LD_LIBRARY_PATH`
   contaminada, `libXss.so.1` se resolvía al mount de Pi (`/tmp/.mount_Pi-.../usr/lib/...`),
   "fuera de los directorios del sistema" → se omitía con WARNING. En Bazzite había libXss de
   sistema y no se notó; en un host minimal moriría al cargar `_tkinter`.
3. **La detección del ICD NVIDIA era rígida** (`*nvidia*.json` por nombre): funcionaba con el
   layout de Bazzite (`/usr/share/vulkan/icd.d/nvidia_icd.x86_64.json` con `library_path`
   absoluto `/usr/lib64/libGLX_nvidia.so.0`, spec de NVIDIA/rpmfusion), pero solo si el driver
   del huésped está instalado. Endurecido por si acaso.

### Fix

- `bundle_libs.py`: `ldd` corre con `env -i` (resolución limpia, inmune a un build desde
  sandbox); nueva `tcl_data()` copia los datos de los paquetes `tcl`/`tk` del huésped a
  `share/tcl/{tcl8.6,tk8.6}/` (sin los symlinks de conveniencia de otros paquetes, p. ej.
  `graphviz -> /usr/lib/...`, colgando aquí). `libXss.so.1` vuelve al cierre: 166 librerías.
- `AppRun`: exporta `TCL_LIBRARY`/`TK_LIBRARY` hacia `share/tcl/*` (version-agnóstico: busca
  `init.tcl`/`tk.tcl` en cada subdirectorio).
- `gow3_pc/launcher/gow3_vulkan.py`: la detección del driver NVIDIA del huésped ya no depende
  del nombre del manifest (cualquier manifest que nombre una librería nvidia/**nvk** cuenta:
  el NVK abierto de Mesa también vale); se omiten los hermanos 32-bit (`nvidia_icd.i686.json`,
  que ordenan primero y enmascararían al de 64 bits); el mensaje de "no encontrado" dice qué
  se buscó y que falte el driver del huésped es la causa típica.
- 3 tests nuevos en `test_packaged_vulkan.py` (NVK, hermanos i686/x86_64, manifest renombrado):
  10 tests OK. Suite completa con `GOW3_CPU_CHECK=0` (la FX-8350 no trae MOVBE; `probe_tlou.py`
  la fija al arrancar el juego): sin fallos, solo los 3 errores preexistentes del merge de
  Uncharted.

### Verificado en la máquina 1 (2026-11-22)

- AppImage reconstruida (158 MB): payload con `share/tcl/{tcl8.6,tk8.6}/` y `libXss.so.1.0.0`.
- Arranque normal: `--close-after 8` → el launcher abre y cierra, `exit 0`, `gow3_vulkan.py`
  elige el driver NVIDIA del huésped.
- **Simulación de Bazzite**: en un namespace de montaje (`unshare -Urm`) con
  `/usr/lib/tcl8.6` y `/usr/lib/tk8.6` tapados (lo que Bazzite no trae):
  - sin `TCL_LIBRARY` → reproduce el `TclError: Can't find a usable init.tcl` exacto;
  - con el entorno de `AppRun` (los datos embebidos) → `tk ok 8.6.16`, y el punto de entrada
    completo (`AppRun --close-after 8`) arranca y sale `exit 0`.
- **Pendiente en Bazzite:** confirmar que el driver NVIDIA del huésped está instalado en esa
  máquina (la línea "no accessible NVIDIA ICD found" es correcta si no lo está; el juego no
  corre sin él): `ls /usr/share/vulkan/icd.d/ | grep -i nvidia` y `nvidia-smi`.

### Segundo test en Bazzite: bug de sonames (2026-11-22)

La AppImage de Tcl/Tk falló en Bazzite con `gow3-probe: error while loading shared
libraries: libZydis.so.4.1: cannot open shared object file`.

- **Causa:** `bundle_libs.py` copiaba cada librería por su *nombre de archivo* largo
  (`libZydis.so.4.1.1.0`), pero los `NEEDED` de los ELF piden el *soname*
  (`libZydis.so.4.1`), que en la máquina de build es solo un symlink del sistema. En Arch
  el symlink del host lo camuflaba; en Bazzite (sin zydis) era fatal. La misma clase de bug
  latente afectaba a ~40 librerías (`libvulkan.so.1`, `libstdc++.so.6`, `libX11.so.6`,
  `libavcodec.so.63`, ...): o bien faltaba en el host, o peor, el loader usaba la versión
  del host (p. ej. `libstdc++.so.6` antiguo frente al runtime compilado con gcc 15).
- **Fix:** `bundle_libs.py` crea ahora el symlink de soname en `lib/` para cada librería
  cuyo `DT_SONAME` difiere del nombre de archivo (154 enlaces); `lib/` pasa a resolver
  todos los `NEEDED` antes que nada del host. Nueva herramienta `tools/needed_check.py`
  (verifica que cada `NEEDED` de cada ELF del paquete se resuelva dentro del AppDir; da
  fail si no) y `build_appimage.sh` la corre como guardia del build.
- **Verificado:** 1203 `NEEDED` sobre 398 ELF, todo autocontenido; launcher `--close-after
  15` → `exit 0`; `--play --seconds 8` → el juego corre (Vulkan en la GTX 1060, sin errores
  de librerías). AppImage nueva en `dist/` (158 MB).
- **La línea "no accessible NVIDIA ICD found"** del primer test de Bazzite sigue sin ser un
  bug: en esa máquina no hay ICD de NVIDIA visible → el driver del huésped no está
  instalado o no está habilitado (`nvidia-smi`, `ls /usr/share/vulkan/icd.d/`). El juego no
  corre en esa máquina sin él, con cualquier AppImage.

### Verificado en Bazzite / PS5 (2026-11-22)

La máquina de Bazzite es un **ps5-bazzite-deck** (VivaLaVent): una PS5 jailbreak con Bazzite
(Fedora-based), GPU AMD RDNA2 (el mismo bloque que las RX 5000) y sin driver de NVIDIA.

- AppImage nueva (sonames + mensaje "using the bundled Mesa drivers") lanzada ahí: **el
  juego corre sobre el embebido Mesa 26.3.1 (`radv`)**. Sin driver de NVIDIA, sin Mesa del
  huésped: el path AMD/Intel queda verificado de verdad, y el fix de sonames se confirma en
  un huésped Fedora-based (donde el fallo de `libZydis.so.4.1` era fatal).
- Rendimiento: no baja de 35 FPS con picos más altos (el jugador, primera sesión; la línea
  `VideoOut: vblank ... frame limit N FPS` de `boot.log` dice el límite efectivo).
- Nota para el jugador: el motor del juego es 30 fps frame-based (PS4); el límite por
  defecto en Linux es 30 (launcher y menú in-game, `fps_limit` en `tlou.ini`). Por encima,
  las animaciones/física corren más rápido de lo diseñado (linux/BUG-SPEED.md).

## Lo que falta

1. El commit de la tarea 16 (y de estos fixes) sigue pendiente en el árbol.
