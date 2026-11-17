# Task 8 — The Launcher's Linux Entry Points: `TLOU-Launcher`, `TLOU-Game`, `build-linux.sh` (Phase 2)

> **Status: COMPLETED AND VERIFIED.** The Linux counterparts of the Windows starters
> `TLOU Launcher.exe` / `TLOU Game.exe` exist and are the package's entry points:
> `launcher/TLOU-Launcher` (opens the launcher) and `TLOU-Game` (starts the game directly
> with the launcher's saved settings), plus `launcher/build-linux.sh`, which installs them
> — no compiler, no `windres`. Verified on Arch Linux through the full `TLOU-Game` chain
> with the real `CUSA00552` 01.00 + 01.11 update (starter → `tools/run_game.py` →
> `probe_tlou.py --boot` → native runtime: the `tlou.ini` settings applied, the 1.11 update
> overlaid, ISA emulation on, and the only failure SDL's "No available video device" — the
> headless end of the line, not the starter's), every error path, the no-terminal (double-click)
> path, `apply_update.py`'s POSIX `wait_for_exit` on the real process topology, and a fresh
> extraction of the 879-file package running `./TLOU-Game` on its bundled Python, and the
> launcher's **window** through the new starter under Xvfb (installed for this task:
> `sudo pacman -S xorg-server-xvfb`): a live `The Last of Us Remastered` Tk window, self-closed
> with `--close-after`, exit 0 — from the repository and from a fresh extraction on the bundled
> Python. The one thing this machine still cannot verify — a real in-game `--boot` — is blocked
> by the CPU (no MOVBE): under Xvfb the run gets past SDL's window and Vulkan (on the real
> NVIDIA GPU), executes the guest through PlayGo's 64 chunks, and faults there — Task 5/7's
> fault, unchanged.

## The change

Three new files:

1. **`launcher/TLOU-Launcher`** (executable bash script) — the Linux counterpart of
   `TLOU Launcher.exe` (`launcher/tlou-launcher.c`, built on Windows by `build-starters.sh`).
   It opens the launcher (`tlou_launcher.pyw`). In the repository it sits beside
   `tlou_launcher.pyw` (in `launcher/`); in a release package it sits at the top, over
   `launcher/` and `python/`. It finds its Python: a package ships one
   (`python/bin/python3`, with Tkinter and Pillow), a repository checkout uses the system's
   `python3`; it refuses to run without Tkinter with a per-distro install hint; extra
   arguments pass on (`--close-after 5`, tests). From a terminal it `exec`s the launcher
   (output and failures go to the terminal); without one (double-clicked in a file manager)
   it tees the output to `analysis/tlou/launcher-start.log` beside the player data and keeps
   the exit code — the log is the only trace of a launch that never reached the game, as the
   game's own run does (`analysis/tlou/launcher-run.log` is the game launch's).
2. **`TLOU-Game`** (executable bash script, repository top / package top) — the Linux
   counterpart of `TLOU Game.exe` (`launcher/tlou-game.c`). It starts the game without
   opening the launcher: it runs `tools/run_game.py`, which reads the launcher's settings
   (`analysis/tlou-user/launcher.json`) with the launcher's own code and starts the runtime
   (`tools/probe_tlou.py`). Its Python search mirrors the C starter's: `python/bin/python3`
   beside it (the package), then `dist/TLOU-Remastered-linux/python/bin/python3` (a
   repository that has built a package, the mirror of the C starter's
   `dist\TLOU-Remastered-windows` check), then the system `python3`. Same Tkinter check
   (the launcher code it reads imports tkinter), same terminal/no-terminal behavior, logging
   to `analysis/tlou/game-start.log` without a terminal.
3. **`launcher/build-linux.sh`** (executable bash script) — the Linux counterpart of
   `build-starters.sh`: it does **not** compile anything and uses no `windres`. The C
   starters exist to dodge antivirus heuristics on unsigned Windows binaries (issues #88,
   #112, #148), which does not apply to a Linux script — the interpreter is the Python a
   package ships or the system's. It restores the executable bit (a checkout made elsewhere
   can have lost it), checks both scripts' syntax with `bash -n` (a broken starter would
   fail in the player's face, not in the build), and reports what a repository checkout's
   Python needs (a note, not a failure — a packaged copy bundles its own).

Six existing files changed, all additive and platform-guarded (Windows byte-identical):

- **`gow3_pc/build.sh`** — the POSIX branch now calls `bash ../launcher/build-linux.sh`
  right after the runtime build, the way the Windows branch calls `build-starters.sh`.
- **`tools/run_game.py`** — `game_running()` keeps the Windows `tasklist` branch verbatim
  and adds a POSIX branch: `pgrep -f gow3-probe` — the launcher's own check (Task 7), so
  the starter and the launcher agree on what "already running" means.
- **`tools/apply_update.py`** — `wait_for_exit()` keeps the Windows `ctypes`/
  `WaitForSingleObject` branch verbatim and adds a POSIX branch: a gone process no longer
  answers a signal-0 check (`os.kill(pid, 0)`; `ProcessLookupError` → gone,
  `PermissionError` → there but owned by another user, polled to the deadline). The
  post-update restart launches `TLOU-Launcher` on POSIX and `TLOU Launcher.exe` on Windows.
- **`tools/package_linux.sh`** — now stages `tools/run_game.py` (the `TLOU-Game` starter's
  script, as `TLOU Game.exe`'s on Windows) and the two starters at the package top, with
  the executable bit.
- **`tools/package_check.py`** — the private-path scan now covers `TLOU-Launcher` and
  `TLOU-Game` (it already covered the two Windows `.exe` starters).
- **`launcher/updater.py`** — the Linux "what a package must hold" list now includes
  `TLOU-Launcher`, `TLOU-Game` and `tools/run_game.py` (an update that lacked a starter
  would install a package the player can no longer open).
- **`gow3_pc/packaging/linux/README-player.txt`** — start with `./TLOU-Launcher`
  (or `./TLOU-Game` without the launcher), plus a `chmod +x` line for extractors that drop
  the executable bit (PT + EN).

Why scripts and not C starters? The C starters are a Windows-specific workaround
(antivirus false positives on small unsigned executables that only `CreateProcess`); on
Linux an executable script is the ordinary thing, it needs no compiler or `windres`
(the task's requirement), and the heavy lifting (settings, environment, command line) was
already portable Python — `run_game.py` builds the exact command and environment the
launcher's own Play button would, using the launcher's own code.

## Verification environment (Arch Linux)

Same machine as Tasks 1–7: x86_64, Python 3.14.8 (system, with Tkinter and Pillow), AMD
FX-8350 (no MOVBE — ISA emulation replaces its 154 uses), NVIDIA GTX 1060. No desktop session
is usable by this agent (`:0` exists but its X authority is private to sddm), so window-level
verification ran under Xvfb (installed for this task: `sudo pacman -S xorg-server-xvfb`,
private display `:99`); the rest is verified headlessly. Real game: `CUSA00552-1.00` + `CUSA00552-patch-1.11` through the
workspace symlinks (`TLOU_UPDATE` points at the update folder), player data in
`analysis/tlou-user/` written with the launcher's own `write_ini`/JSON structure
(`output_res=1920x1080`, `fps_limit=30`, `system_language=3`, the game folder in
`launcher.json`).

## Verification (all on this machine)

1. **Install.** `bash gow3_pc/build.sh` (POSIX branch) builds the runtime and then runs
   `build-linux.sh`: `Starters: .../launcher/TLOU-Launcher and .../TLOU-Game (both
   executable)`. Both are mode 755 and pass `bash -n`.
2. **The starter speaks the launcher's language.** `./TLOU-Game --print` prints the exact
   command/environment/folders/switches `run_game.py` would run — built by the launcher's
   own code (imports, environment, `--game` from `launcher.json`'s saved folder, all
   present). With the system Python removed from the `PATH`, it transparently falls back to
   a package built in the repository (`dist/TLOU-Remastered-linux/python/bin/python3`) —
   the C starter's `dist\TLOU-Remastered-windows` mirror, verified live.
3. **The full chain, real game.** `TLOU_UPDATE=... ./TLOU-Game --seconds 0` ran starter →
   `run_game.py` → `probe_tlou.py --boot --seconds 0 --game ...` → `out/gow3-probe`: the
   update overlaid (46 files over the game), the `tlou.ini` settings applied
   (`Resolution: the game draws at 1920x1080`, `system_language=3` accepted with no
   warning, frame-timeout/pro-shadows/ambient patches on), MOVBE ISA emulation active
   (154 instructions replaced), and the run stopped at `Failed to initialize SDL video:
   No available video device` — the headless end of the line. The runtime's output goes to
   the game log as on Windows (`launcher-run.log` / `boot.log`), and the runtime's exit
   code propagates through the whole chain.
4. **Error paths, each with a clear message and a nonzero exit:**
   - a `gow3-probe` process present on the process table → `The game is already running.
     Close it before starting another one.` (exit 2) — `pgrep` agrees with the launcher;
   - `tlou_launcher.pyw` missing → `TLOU-Launcher: tlou_launcher.pyw was not found ...`;
   - no `python3` anywhere (stripped `PATH`, no bundled copy) → `TLOU-Launcher: Python 3
     was not found ...` (exit 1);
   - a Python without Tkinter → `TLOU-Launcher: ... has no Tkinter. Install it (Arch:
     sudo pacman -S tk; ...) and try again.` (exit 1).
5. **The no-terminal path (double-clicked).** With stderr on a pipe (not a tty), both
   starters tee their output to `analysis/tlou/launcher-start.log` / `game-start.log`
   (created with its folders) and exit with the child's code: `TLOU-Game --print` exit 0
   with the full JSON in the log; `TLOU-Launcher` with no usable display exit 1 with the
   `TclError` in the log. Verified both from the repository and from a fresh extraction.
6. **`wait_for_exit` (POSIX), real topology.** The updater is *not* the launcher's parent,
   so an exited launcher is reaped by its real parent (shell/init) and never zombies:
   verified with a double-forked process re-parented to PID 1 — a live process returns
   `False` at the deadline; one that dies at 2 s returns `True` at 1.7 s; a pid that is
   already gone returns `True` immediately.
7. **Update plumbing.** `updater.py`'s Linux requirements now list `TLOU-Launcher`,
   `TLOU-Game` and `tools/run_game.py` (imported and checked at runtime); the post-update
   restart points at `TLOU-Launcher` on POSIX.
8. **The package.** `tools/package_linux.sh` (gate included) builds
   `dist/TLOU-Remastered-linux.zip`: **879 files, 17 MiB** (Task 7's 876 + the three new
   ones), `tools/package_check.py` reports no game files and no private paths — the
   starters are now inside the private-path scan. The starters keep their mode 755 inside
   the zip (checked by the stored unix attributes, `0o100755`). From a fresh extraction,
   `./TLOU-Game --print` runs on the package's **bundled** Python, and `./TLOU-Launcher`
   fails headlessly exactly as in the repository — with its `TclError` logged to
   `analysis/tlou/launcher-start.log` inside the package.
9. **The window, under Xvfb.** With `DISPLAY=:99` (`Xvfb :99`), `./launcher/TLOU-Launcher
   --close-after 8` opened a real window — `xwininfo -root -tree` during the run shows
   `0x200078 "The Last of Us Remastered": ("tk" "Tk") 1180x690` — and the launcher closed
   itself, exit 0. This agent's shell is not a tty, so the no-terminal branch also ran: the
   log was created (empty — a successful launch prints nothing) beside the player data. The
   same from a fresh extraction, on the package's bundled Python: exit 0, log created inside
   the package.
10. **`TLOU-Game` opens no launcher window.** Under Xvfb the run created the runtime's own
    SDL window (`Window: Windowed, 1632x918` in the game log — not a Tk window), initialized
    Vulkan on the real NVIDIA GPU, ran the guest through PlayGo (64 chunks installed), and
    faulted with SIGILL at guest offset 0xcafaa3 — the CPU's MOVBE limitation (Tasks 5/7),
    now one step deeper than the headless SDL failure, with nothing the starter does in
    between.

## Technical decisions

- **Scripts, not C starters.** The task allows "a lightweight binary **or executable
  script**". The C starters are a workaround for Windows antivirus heuristics; on Linux
  the script is the ordinary thing, needs no toolchain (`build-linux.sh` compiles nothing,
  uses no `windres`), and the one behavior worth keeping — "a double-clicked launch that
  fails leaves a trace" — is a few lines of bash (`[[ -t 2 ]]`, `tee -a`,
  `${PIPESTATUS[0]}`).
- **The starters own only three things:** finding their Python (bundled → repository
  distro build → system), proving Tkinter exists (one `python -c` each), and terminal vs
  no-terminal. Everything else — the settings, the environment, the command line, the
  already-running check — stays in `run_game.py`/`probe_tlou.py`, so there is one place
  that builds the game's launch, and the starter's `--print` mode is that place shown.
- **`TLOU-Game` sits at the repository top** (as it will at the package top), while
  `TLOU-Launcher` sits in `launcher/` in the repository and moves to the top in the
  package (mirroring the Windows layout, where both `.exe`s sit at the top). The scripts
  find their targets in both layouts, so a copy of either one works from its folder.
- **`pgrep -f gow3-probe`** matches the launcher's own `game_running()` (Task 7) rather
  than a process name, because the runtime is launched as `gow3-probe` with arguments;
  the two entry points must agree on "already running".
- **`wait_for_exit` via signal 0**, not `waitpid`: the updater is not the launcher's
  parent, so `waitpid` would refuse it; a gone process stops answering signal 0. Zombies
  cannot linger in the real topology (the launcher's own parent reaps it).

## Windows compatibility guarantee (proof)

- No Windows file was touched: `tlou-launcher.c`, `tlou-game.c`, `build-starters.sh`,
  `tlou-starter.rc/.manifest`, `package_windows.sh` are unmodified (checked by
  `git status`).
- The four shared Python/shell edits are platform-guarded and the Windows branches are the
  old code verbatim: `run_game.py` `game_running()` (the `tasklist` body is unchanged, now
  inside `if sys.platform == "win32":`), `apply_update.py` `wait_for_exit()` (the
  `ctypes.windll` body unchanged; the POSIX branch only runs when `ctypes.windll` does not
  exist), its restart (`"TLOU Launcher.exe" if sys.platform == "win32" else ...`), and
  `updater.py` (`REQUIRED`/`ASSET` inside the existing `if/else` — the Windows tuple is
  byte-identical).
- `package_linux.sh` and `package_check.py` are themselves the Linux gate: running them
  does not touch the Windows package, and `build.sh`'s change is inside its POSIX branch
  (the Windows branch is unchanged).

## Risks / observations for the following tasks

- **A real in-game `--boot` remains CPU-blocked** (no MOVBE; Tasks 5/7): under Xvfb the
  chain is verified up to the guest fault (SIGILL at guest offset 0xcafaa3, after SDL's
  window, Vulkan, and PlayGo's 64 chunks) — the fault is the CPU's, not the starter's. A
  CPU with MOVBE (and a desktop's real display, rather than Xvfb) is what a player would
  need to play; on this machine the starter is verified end-to-end up to that point.
- **The window was verified under Xvfb, not a desktop session** (this agent has no usable
  session: `:0`'s authority is private to sddm). Everything the starter does is display-
  agnostic (it only finds a Python, checks Tkinter, and `exec`s or tees); the only X-level
  risk left is cosmetic (window-manager decorations, icon), not functional.
- **`pgrep -f` matches command lines:** a shell whose own command line contains the
  literal string `gow3-probe` (test harnesses) is counted as "the game is running".
  The player's flow never has that string in any ancestor command line; same caveat as
  the launcher's own check (Task 7).
- **The executable bit lives in the zip:** `unzip` (and most tools) restore it — verified;
  an extractor that drops it is covered by the README's `chmod +x` line. The package does
  not ship `build-linux.sh` (a build tool, as `build-starters.sh` is not in the Windows
  package).
- **Task 9+ note:** the in-package `TLOU-Launcher`/`TLOU-Game` resolve the game folder and
  the update through `launcher.json`/`TLOU_UPDATE` exactly as the launcher does; any task
  that changes those files' formats must update `run_game.py` (and is covered by
  `--print`).
