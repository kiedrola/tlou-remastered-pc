# Tarea 6 — Guarda Windows-only de la Inyección del PATH de MSYS2 en `tools/probe_tlou.py` (Fase 2)

> **Estado: COMPLETADA Y VERIFICADA.** La inyección del PATH de MSYS2 de
> `tools/probe_tlou.py` (`main()`) ahora está protegida por `sys.platform == "win32"`:
> en Windows el entorno del proceso hijo recibe exactamente el mismo `PATH` de antes
> (probado ejecutando las sentencias exactas del archivo antiguo y del nuevo bajo
> Windows simulado — idénticas byte a byte en los tres casos de `MSYS2_ROOT`/`PATH`), y
> en Linux `env["PATH"]` se deja intacto. Verificado de tres formas en Arch Linux:
> ejecución antes/después a nivel de sentencias, captura en vivo del entorno que
> construye el `main()` real de la herramienta (`PATH` intacto, `gow3-probe` nativo
> seleccionado), y una ejecución end-to-end completa de `--check-imports` con la copia
> real de `CUSA00552` 01.00 + update 01.11 (`Imports: 0 missing`, `exit_code: 0`).

## El cambio

`tools/probe_tlou.py`, `main()` — la preparación del entorno justo antes de lanzar el
runtime (la única referencia a MSYS2 del archivo):

```python
    # The Windows runtime links against MSYS2 CLANG64's own DLLs (libstdc++ and friends),
    # which the process finds through PATH.
    if sys.platform == "win32":
        msys = Path(os.environ.get("MSYS2_ROOT", "C:/msys64"))
        env["PATH"] = str(msys / "clang64/bin") + os.pathsep + env.get("PATH", "")
```

antes:

```python
    msys = Path(os.environ.get("MSYS2_ROOT", "C:/msys64"))
    env["PATH"] = str(msys / "clang64/bin") + os.pathsep + env.get("PATH", "")
```

Las dos sentencias son **textualmente inalteradas** (solo indentadas bajo la guarda);
se añadió una línea de guarda y un comentario breve. `msys` es una local de `main()`
usada solo por esas dos líneas, así que nada más del archivo se ve afectado.

## Por qué existe la inyección — y por qué solo Windows la necesita

- **Windows (MSYS2 CLANG64):** el runtime se compila con el toolchain de MSYS2 y enlaza
  las DLL de runtime propias de ese toolchain (`libstdc++.dll`, `libgcc_s_seh-1.dll`,
  `libwinpthread-1.dll`, ...): `tools/package_windows.sh` recoge exactamente esas
  (`ldd gow3_pc/out/gow3-probe.exe | awk '/clang64/'`) al empaquetar, y
  `gow3_pc/packaging/windows/README.md` documenta la dependencia de CLANG64. Al
  ejecutarse desde un árbol de código (lo que hace `probe_tlou.py`), el proceso las
  encuentra a través de los directorios de `PATH`, así que la herramienta antepone
  `$MSYS2_ROOT/clang64/bin` — la misma convención de override `MSYS2_ROOT` que
  `gow3_pc/msys2-env.sh` (por defecto `C:/msys64`).
- **Linux:** `gow3-probe` es un ELF nativo enlazado contra librerías de la distro
  (`libstdc++` de GCC, SDL3, el loader de Vulkan, `libav*`; rama POSIX de `build.sh`)
  con RPATH `$ORIGIN/gpu` más la ruta absoluta de `out/gpu`
  (`-Wl,-rpath,'$ORIGIN/gpu'` y `-Wl,-rpath,"$PWD/out/gpu"`).
  `ldd gow3_pc/out/gow3-probe` no muestra **ninguna** dependencia de MSYS2/clang64, así
  que no hay nada que buscar en `clang64/bin`. El código sin guarda ejecutaba las dos
  líneas igual y antepuso `C:/msys64/clang64/bin` — en Linux `Path("C:/msys64")` no
  tiene unidad, así que es una entrada *relativa*: nunca podría resolverse, e incluso
  podía eclipsar búsquedas reales si existiera algún día una carpeta llamada `C:` bajo
  el directorio de trabajo. Inocua en la práctica (la ejecución end-to-end de la Tarea 5
  lanzó el runtime sin problemas con el PATH ensuciado) pero incorrecta, y era la última
  suposición de Windows dentro de este archivo.

## Entorno de verificación (Arch Linux)

Misma máquina que las Tareas 1–5: x86_64, Python 3.14.8, AMD FX-8350 (sin MOVBE — se
reutiliza el entorno `GOW3_CPU_CHECK=0`/`TLOU_ISA_REWRITE=off` de la Tarea 5, que solo
importa porque en modo check-imports no se ejecuta código invitado), NVIDIA GTX 1060,
runtime compilado por la Tarea 1. Juego real:
`/roms-ps4/Roms/PS4/The Last of Us Remastered/CUSA00552-1.00` (APP_VER 01.00) +
`CUSA00552-patch-1.11` (APP_VER 01.11), alcanzado a través del symlink `../CUSA00552`
(el `--game` por defecto de la herramienta), con `TLOU_UPDATE` apuntando a la carpeta
del update (la carpeta no se llama `<juego>-patch`, ver notas de la Tarea 5).

## Verificación

1. **Prueba antes/después a nivel de sentencias (código exacto, no una copia reescrita).**
   Las dos sentencias se extrajeron textualmente de `git show HEAD:tools/probe_tlou.py`
   (antes) y del árbol de trabajo (después, incluyendo la línea de guarda), con una
   afirmación de que el cuerpo guardado es textualmente idéntico a las dos sentencias
   antiguas. Después se ejecutaron ambos fragmentos con un namespace `os`/`sys`
   controlado:
   - Windows simulado (`sys.platform = "win32"`): antiguo vs nuevo producen un
     `env["PATH"]` **idéntico byte a byte** en los tres casos — `MSYS2_ROOT` por
     defecto (se antepone `C:/msys64/clang64/bin`), `MSYS2_ROOT=/d/msys2` personalizado
     (se antepone `/d/msys2/clang64/bin`), y `PATH` sin definir en el entorno del padre
     (`C:/msys64/clang64/bin:`, el borde ya existente del separador final).
   - Linux simulado (`sys.platform = "linux"`): el antiguo antepone la inexistente
     `C:/msys64/clang64/bin` (ensuciamiento); el nuevo deja `env["PATH"]` intacto.
2. **Captura en vivo del entorno que construye la herramienta.** Se ejecutó el
   `main()` real de la herramienta (la fase de preparación — `prepare.py`,
   `link_modules.py`, `content_profile.py`, etapa red-zone/patches — de verdad) con
   `subprocess.Popen` interceptado solo para el lanzamiento del runtime, registrando el
   `command`/`env` exactos que se pasarían:
   - Linux, `MSYS2_ROOT` por defecto: `env["PATH"]` es **idéntico** al `PATH`
     original del proceso (`path_untouched: true`); `command[0]` es el nativo
     `<repo>/gow3_pc/out/gow3-probe` (la fix de la Tarea 5).
   - Linux, `MSYS2_ROOT=/c/msys64`: igual — la variable ahora simplemente se ignora en
     POSIX.
   - Windows simulado en esta máquina no puede pasar de `game_executable()` (no hay
     `gow3-probe.exe` aquí; el `SystemExit` nombra exactamente la ruta de antes), así
     que el lado Windows de la prueba de identidad es el punto 1, que ejecuta las
     sentencias reales de ambas versiones.
3. **Ejecución end-to-end completa con el juego real** (modo por defecto =
   `--check-imports`, que prepara, aplica parches y lanza el runtime pero nunca
   decodifica código invitado):

   ```
   GOW3_CPU_CHECK=0 TLOU_UPDATE=<.../CUSA00552-patch-1.11> TLOU_ISA_REWRITE=off \
       python3 tools/probe_tlou.py
   ```

   - Resultado: `Imports: 0 missing`, `exit_code: 0`, `elapsed_seconds: 0.916`. Ventana
     SDL3 creada (Windowed, 1632x918), GTX 1060 + presenter Vulkan listos, todos los
     parches aplicados (`frame-timeout`, `job-sleep`, `pro-shadows`,
     `ambient-shadows-half`, `all-languages`, `flashlight`, ...) — el mismo
     comportamiento que la ejecución de la Tarea 5, ahora con un `PATH` no ensuciado.
     Esto prueba que el runtime nativo realmente no necesita nada de `clang64/bin`.

## Decisiones técnicas

1. **Patrón de guarda: `sys.platform == "win32"`** — el patrón que prescribe la regla
   innegociable de Windows del roadmap para Python, y el que la Tarea 5 ya estableció
   en este mismo archivo (`game_executable()`). El Python de MSYS2 también reporta
   `win32`, así que el flujo de desarrollo de MSYS2 (que necesita la entrada
   `clang64/bin` para las DLL del runtime) conserva la inyección exactamente como antes.
2. **Guardar las dos líneas, no solo la de `PATH`.** `msys` existe solo para alimentar
   la línea de PATH; dejarla sin guarda seguiría calculando una ruta (relativa, en
   Linux) sin usar. Guardar el par mantiene el diff mínimo y la intención clara.
3. **Comentario añadido** explicando *por qué* existe la inyección (el runtime de
   Windows enlaza las DLL propias de MSYS2), en el estilo de comentarios densos "el por
   qué" del archivo — para que un lector futuro no la borre como código muerto, ni
   "arregle" el lado Linux de vuelta.
4. **Alcance limitado a las dos líneas**, según la evidencia del roadmap. Las demás
   herramientas solo-Windows (`stall_watch.py`, `sample_threads.py`, `run_game.py`, ...
   — `llvm-nm.exe`/`llvm-symbolizer.exe`/`tasklist`) no se tocaron; son herramientas
   diagnósticas de tareas posteriores (7/8/9/13/15).

## Garantía de compatibilidad con Windows (prueba)

| Cambio | Efecto en Windows |
|---|---|
| Nueva guarda `if sys.platform == "win32":` alrededor de las dos sentencias | Verdadero para cada Python de Windows (CPython nativo y Python de MSYS2 por igual), así que el cuerpo se ejecuta en la misma llamada, en el mismo orden, con las mismas variables locales que antes. |
| Las dos sentencias bajo la guarda | Textualmente idénticas a las sentencias antiguas (probado extrayendo ambas de los archivos y afirmando la igualdad). Ejecutadas bajo `win32` simulado, el `env["PATH"]` resultante es idéntico byte a byte al resultado antiguo para `MSYS2_ROOT` por defecto, `MSYS2_ROOT` personalizado y `PATH` sin definir. |
| La línea de comentario | Sin efecto en runtime. |
| Cualquier otra cosa del archivo | Intocada (`git diff`: 3 líneas añadidas, 2 líneas reindentadas). |

En resumen: en Windows el entorno del hijo, el valor de `PATH`, la búsqueda de DLL del
runtime y cualquier otro comportamiento son exactamente como antes.

## Riesgos / observaciones para las tareas siguientes

- **Tarea 7 (`package_linux.sh`):** el runtime Linux empaquetado debe incluir solo lo
  que informan `ldd gow3_pc/out/gow3-probe` + `ldd out/gpu/libgow3gpu.so` (`.so` de la
  distro); con esta guarda nada en el flujo de arranque referencia `C:/msys64` ya, así
  que no hay ningún componente de MSYS2 que incluir o documentar.
- **Tareas 8/9 (runner Linux / launcher Tkinter):** el futuro runner Linux que
  reproduzca el entorno de `probe_tlou.py` **no debe** añadir una entrada
  `clang64/bin` (este archivo ahora es la implementación de referencia). En Windows el
  flujo equivalente **debe conservarla**: `launcher/tlou_launcher.pyw:1863` ya resuelve
  `$MSYS2_ROOT/clang64/bin/SDL3.dll`, y el flujo del starter exe depende de las mismas
  DLL del toolchain — al adaptar `build-starters.sh`/el launcher, esa rama de Windows
  debe permanecer byte a byte.
- **La variable `MSYS2_ROOT` en Linux ahora es un no-op silencioso** en esta
  herramienta (como debe ser). Si algún usuario Linux tuviera un toolchain de cruce
  MinGW, no podría activarlo en nada — eso nunca fue el propósito de la variable;
  documenta el layout de Windows.
- **El end-to-end con Windows simulado es imposible en esta máquina** (no hay
  `gow3-probe.exe`), así que la prueba de Windows es a nivel de sentencias. El cambio
  son dos líneas bajo una guarda de plataforma cuyo valor es constante por plataforma,
  por lo que la identidad a nivel de sentencias es suficiente; una máquina Windows
  debería hacer una última ejecución de sanity en la Tarea 18/CI si se añade alguna.
- **Los artefactos de verificación están gitignored:** la ejecución end-to-end escribió
  en `analysis/tlou` (logs, JSON de resultado, boot view, parches) y
  `analysis/tlou-user` — cubierto por `.gitignore` (`/analysis/*`). `git status` solo
  muestra modificados los archivos de esta tarea.
