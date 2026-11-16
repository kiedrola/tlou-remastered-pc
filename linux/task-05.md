# Task 5 — OS-Dependent Runtime Binary in `tools/probe_tlou.py` (Phase 2)

> **Status: COMPLETED AND VERIFIED.** `game_executable()` now selects the runtime binary by
> OS — `gow3-probe.exe` on Windows (`sys.platform == "win32"`), `gow3-probe` on POSIX.
> Verified three ways on Arch Linux: unit checks of the function (real platform, simulated
> Windows, missing-binary error), a before/after proof with the change stashed, and a live
> end-to-end run with a real `CUSA00552` 01.00 copy + 01.11 update: the tool prepared the
> game, built the patch set, and launched the native `gow3-probe`, which completed
> `--check-imports` with `Imports: 0 missing` and exit code 0.

## The change

`tools/probe_tlou.py`, `game_executable()` (the only reference to the binary in the file;
its only consumer is the boot command, `command = [str(game_executable()), ...]`, fed to
`subprocess.Popen`):

```python
def game_executable():
    name = "gow3-probe.exe" if sys.platform == "win32" else "gow3-probe"
    built = ROOT / "gow3_pc/out" / name
    if not built.is_file():
        raise SystemExit(f"Build the runtime first (gow3_pc/build.sh): {built} is missing")
    return built
```

One added line; the existence check and the `SystemExit` message are unchanged.

## Verification environment (Arch Linux)

Same machine as Tasks 1–4: x86_64, Python 3.14.8, runtime built by Task 1
(`gow3_pc/out/gow3-probe`, 1.7 MB ELF, `ldd` clean, `--vulkan-only` PASS).

- **CPU:** AMD FX-8350 Eight-Core (Piledriver). Per `/proc/cpuinfo` it has SSE4.2, POPCNT,
  AVX, F16C, LZCNT and BMI1, but **lacks MOVBE** (see Risks — this machine cannot decode
  the game's code, which matters for Task 11, not for this one).
- **Real game:** `/roms-ps4/Roms/PS4/The Last of Us Remastered/CUSA00552-1.00`
  (APP_VER 01.00) with its update `CUSA00552-patch-1.11` (APP_VER 01.11, checked with the
  project's own `prepare.sfo`). Reached through symlinks `../CUSA00552` and
  `../CUSA00552-patch` in the workspace: the tool's default `--game` is
  `ROOT.parent / "CUSA00552"`, and `args.game.resolve()` follows the symlink to
  `CUSA00552-1.00`.

## Live verification

1. **Before/after with the change stashed** (`git stash push tools/probe_tlou.py`):
   - **Without:** the tool runs the whole preparation (update detection, boot view,
     `prepare.py`/`link_modules.py`/`content_profile.py`, red-zone/ISA stage) and then
     stops: `Build the runtime first (gow3_pc/build.sh): <repo>/gow3_pc/out/gow3-probe.exe
     is missing` — the tool cannot proceed at all on Linux.
   - **With:** it proceeds to launch the runtime (item 3). The stash was popped; the
     working tree carries exactly this one-line change plus the pre-existing
     `fsr-vulkan` submodule `-dirty` (build.sh applies the project patches there, Task 1).
2. **Unit checks** (importing the module, no game involved):
   - Linux: `game_executable()` returns `<repo>/gow3_pc/out/gow3-probe` and the file
     exists (Task 1's artifact).
   - Simulated Windows (`sys.platform` temporarily set to `"win32"`, restored in a
     `finally`): the function selects `gow3-probe.exe` — here it raises the `SystemExit`
     with exactly the pre-change path in the message, proving the Windows branch resolves
     to the identical `Path`.
   - Missing binary (`ROOT` pointed at an empty folder): `SystemExit` with the
     `Build the runtime first` message.
3. **End-to-end with the real game** — the default (no `--boot`) mode, which is exactly
   the tool's `--check-imports` flow: it prepares the game, computes the patches, builds
   the command and launches the runtime; the mode lists the PS4 imports the runtime does
   not provide and **never decodes or executes guest code**:

   ```
   GOW3_CPU_CHECK=0 TLOU_UPDATE=<.../CUSA00552-patch-1.11> TLOU_ISA_REWRITE=off \
       python3 tools/probe_tlou.py
   ```

   - The result JSON's `command[0]` is `<repo>/gow3_pc/out/gow3-probe` — the native
     binary, selected by the fixed function (with the update overlay: 46 files, 7 folders).
   - The runtime ran to completion in 1.165 s: SDL3 window created (1632x918), NVIDIA
     GTX 1060 detected, Vulkan renderer fully initialized (low-VRAM mode, pipeline cache,
     upscaler), all game patches applied (`frame-timeout`, `job-sleep`, `pro-shadows`,
     `ambient-shadows-half`, `all-languages`, `flashlight`, ...), and the mode's purpose:
     `Imports: 0 missing`. `exit_code: 0`.
   - `GOW3_CPU_CHECK=0` was needed because the runtime's own CPU check otherwise stops on
     this MOVBE-less FX-8350 (`STOP: this CPU lacks instruction sets the game's code
     uses: MOVBE ...`); it is safe in check-imports mode precisely because no guest code
     runs. See Risks.

## Technical decisions

1. **Guard pattern: `sys.platform == "win32"`** — the pattern the roadmap's non-negotiable
   Windows rule prescribes for Python, and the one already in the repository
   (`gow3_pc/scripts/cpu_features.py:77`). The repo also uses `os.name == "nt"` in
   `mods.py`/`game_update.py`, but those are Windows *path/junction* semantics, not
   platform guards; the roadmap mandates `sys.platform` here. Note MSYS2's Python also
   reports `win32`, so the MSYS2 build (which produces `gow3-probe.exe`) keeps resolving
   the same name as before.
2. **Guard the name, don't probe both names.** The two build branches are mutually
   exclusive by construction: the Windows branch of `build.sh` (lines 35–40) produces
   `out/gow3-probe.exe` and the POSIX branch (line 104) produces `out/gow3-probe`. A
   single guarded name is therefore exact, keeps the existing "is missing" error
   semantics, and cannot pick a stale artifact of the other OS.
3. **Scope kept to the task.** Only `game_executable()` changed. The MSYS2 PATH
   injection (lines 553–554) belongs to Task 6 and is untouched — the end-to-end run
   proves it is currently *harmless* on Linux (a non-existent `C:/msys64/clang64/bin` is
   simply prepended to `PATH` and the runtime launches and runs anyway).
4. **The end-to-end environment variables were the tool's documented interface, not
   workarounds:** `TLOU_UPDATE=<folder>` because `find_update` searches
   `<game name>-patch`/`-update` next to the *resolved* game path (the symlink target
   `CUSA00552-1.00`), while this machine's update folder is named
   `CUSA00552-patch-1.11` and sits next to the symlink; `TLOU_ISA_REWRITE=off` to skip
   the tool's ISA-emulation gate on a MOVBE-less CPU (the gate would hard-exit here,
   since the `iced-x86` table the gate needs is a Windows-only feature of the POSIX
   loader — see Risks).

## Windows compatibility guarantee (proof)

| Change | Effect on Windows |
|---|---|
| `game_executable()`: `name = "gow3-probe.exe" if sys.platform == "win32" else "gow3-probe"`; `built = ROOT / "gow3_pc/out" / name` | On Windows (native CPython and MSYS2 Python alike: `sys.platform == "win32"`) `name` is `"gow3-probe.exe"`, and `ROOT / "gow3_pc/out" / "gow3-probe.exe" == ROOT / "gow3_pc/out/gow3-probe.exe"` — the *identical* `Path` value as before (verified: `Path` equality and identical `str()`). The `is_file()` check, the `SystemExit` message, and every other code path in the file are unchanged. |

No other file was modified for this task.

## Risks / observations for the following tasks

- **Task 6 (MSYS2 PATH):** the unguarded injection is proven harmless in this run, but
  the `sys.platform == "win32"` guard is still pending as planned; on Linux it currently
  prepends `C:/msys64/clang64/bin` (a non-existent relative path) to `PATH`.
- **Task 11 (real boot) on *this* machine is blocked by the CPU, not by the tool.**
  The runtime's CPU check stops with `STOP: this CPU lacks instruction sets the game's
  code uses: MOVBE (Intel Haswell 2013 or newer, or AMD Ryzen or newer, is needed)`.
  The ISA-emulation path is Windows-only in the POSIX loader: `red_zone_table.py` builds
  the table (it needs `iced-x86` in `gow3_pc/out/pydeps`, currently empty here) and
  `probe.c`'s POSIX branch accepts `--red-zone` but explicitly does not apply it
  (`Red zone: Windows-only protection ignored on POSIX`). So installing `iced-x86`
  would **not** make the FX-8350 able to run the game on Linux; Task 11 on this machine
  needs either a MOVBE-capable CPU or a new POSIX ISA-emulation path. The
  `GOW3_CPU_CHECK=0` used for this verification must **not** be carried into Task 11.
  (On a MOVBE-capable CPU none of this is needed: `isa_names` is empty, the missing
  `iced-x86` only leaves `red_zone=None`, and the run proceeds.)
- **Update auto-discovery vs. symlinks (pre-existing, identical on Windows):**
  `--game` resolves symlinks and the `-patch`/`-update` suffix search runs next to the
  resolved target, so an update stored next to the symlink (as on this machine) is not
  found automatically — `TLOU_UPDATE=<folder>` (or a `<name>-patch` folder next to the
  real game) is required. Relevant for the Task 11 checklist.
- **The rest of `tools/` still assumes Windows (out of Task 5's scope):**
  `stall_watch.py`, `sample_threads.py`, `profile_rank.py`, `region_map.py`,
  `peek_any.py`, `run_game.py`, `snapshot_guest_textures.py`, `inspect_runtime_audio.py`
  and `inspect_game_audio.py` hardcode `gow3-probe.exe` and/or Windows-only utilities
  (`tasklist`, `C:/msys64/clang64/bin/llvm-nm.exe`). `tlou_replay.py` and
  `tlou_bench.py` start through `probe_tlou.py` (not the binary directly), so they
  inherit this fix; Tasks 13/15 will likely need POSIX counterparts of the diagnostic
  tools.
- **Verification artifacts are gitignored:** the end-to-end run wrote to
  `analysis/tlou` (boot view, `boot-linked.bin`, patches, logs, result JSON) and
  `analysis/tlou-user` — covered by `.gitignore` (`/analysis/*`), so the repository
  stays clean; `git status` shows only `tools/probe_tlou.py` modified for this task.
