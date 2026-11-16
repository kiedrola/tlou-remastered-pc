# Task 6 — Windows-Only Guard for the MSYS2 PATH Injection in `tools/probe_tlou.py` (Phase 2)

> **Status: COMPLETED AND VERIFIED.** The MSYS2 PATH injection in `tools/probe_tlou.py`
> (`main()`) is now guarded by `sys.platform == "win32"`: on Windows the child environment
> gets exactly the same `PATH` as before (proved by executing the exact statements of the
> old and new file under simulated Windows — byte-identical in all three
> `MSYS2_ROOT`/`PATH` cases), and on Linux `env["PATH"]` is left untouched. Verified
> three ways on Arch Linux: statement-level before/after execution, a live capture of the
> environment the tool's real `main()` builds (untouched `PATH`, native `gow3-probe`
> selected), and a full end-to-end `--check-imports` run with the real `CUSA00552` 01.00
> copy + 01.11 update (`Imports: 0 missing`, `exit_code: 0`).

## The change

`tools/probe_tlou.py`, `main()` — the environment setup right before the runtime launch
(the only MSYS2 reference in the file):

```python
    # The Windows runtime links against MSYS2 CLANG64's own DLLs (libstdc++ and friends),
    # which the process finds through PATH.
    if sys.platform == "win32":
        msys = Path(os.environ.get("MSYS2_ROOT", "C:/msys64"))
        env["PATH"] = str(msys / "clang64/bin") + os.pathsep + env.get("PATH", "")
```

before:

```python
    msys = Path(os.environ.get("MSYS2_ROOT", "C:/msys64"))
    env["PATH"] = str(msys / "clang64/bin") + os.pathsep + env.get("PATH", "")
```

The two statements are **textually unchanged** (only indented under the guard); one guard
line and a short comment were added. `msys` is a local of `main()` used only by those two
lines, so nothing else in the file is affected.

## Why the injection exists — and why only Windows needs it

- **Windows (MSYS2 CLANG64):** the runtime is built with MSYS2's toolchain and links
  against that toolchain's own runtime DLLs (`libstdc++.dll`, `libgcc_s_seh-1.dll`,
  `libwinpthread-1.dll`, ...): `tools/package_windows.sh` collects exactly those
  (`ldd gow3_pc/out/gow3-probe.exe | awk '/clang64/'`) when packaging, and
  `gow3_pc/packaging/windows/README.md` documents the CLANG64 dependency. Run from a
  source tree (what `probe_tlou.py` does), the process finds them through the
  `PATH` directories, so the tool prepends `$MSYS2_ROOT/clang64/bin` — the same
  `MSYS2_ROOT` override convention as `gow3_pc/msys2-env.sh` (default `C:/msys64`).
- **Linux:** `gow3-probe` is a native ELF linked against distro libraries (GCC's
  `libstdc++`, SDL3, the Vulkan loader, `libav*`; `build.sh` POSIX branch) with an RPATH
  of `$ORIGIN/gpu` plus the absolute `out/gpu` path (`-Wl,-rpath,'$ORIGIN/gpu'` and
  `-Wl,-rpath,"$PWD/out/gpu"`). `ldd gow3_pc/out/gow3-probe` shows **no** MSYS2/clang64
  dependency at all, so there is nothing to look up in `clang64/bin`. The unguarded code
  still ran the two lines and prepended `C:/msys64/clang64/bin` — on Linux
  `Path("C:/msys64")` has no drive, so that is a *relative* entry: it could never
  resolve, and it could even shadow real lookups if a folder named `C:` ever existed
  under the working directory. Harmless in practice (the Task 5 end-to-end run launched
  the runtime fine with the polluted PATH) but wrong, and it is the last Windows
  assumption inside this file.

## Verification environment (Arch Linux)

Same machine as Tasks 1–5: x86_64, Python 3.14.8, AMD FX-8350 (no MOVBE — the
`GOW3_CPU_CHECK=0`/`TLOU_ISA_REWRITE=off` environment of Task 5 is reused and only
matters because no guest code runs in check-imports mode), NVIDIA GTX 1060, runtime
built by Task 1. Real game: `/roms-ps4/Roms/PS4/The Last of Us Remastered/CUSA00552-1.00`
(APP_VER 01.00) + `CUSA00552-patch-1.11` (APP_VER 01.11), reached through the
`../CUSA00552` symlink (the tool's default `--game`), with `TLOU_UPDATE` pointing at the
update folder (the folder is not named `<game>-patch`, see Task 5's notes).

## Verification

1. **Statement-level before/after proof (exact code, not a re-typed copy).** The two
   statements were extracted verbatim from `git show HEAD:tools/probe_tlou.py` (before)
   and from the working tree (after, including the guard line), with an assertion that
   the guarded body is textually identical to the old two statements. Both snippets were
   then executed with a controlled `os`/`sys` namespace:
   - Simulated Windows (`sys.platform = "win32"`): old vs new produce the
     **byte-identical** `env["PATH"]` in all three cases — default `MSYS2_ROOT`
     (`C:/msys64/clang64/bin` prepended), custom `MSYS2_ROOT=/d/msys2`
     (`/d/msys2/clang64/bin` prepended), and `PATH` unset in the parent environment
     (`C:/msys64/clang64/bin:`, the pre-existing trailing-separator edge).
   - Simulated Linux (`sys.platform = "linux"`): old prepends the non-existent
     `C:/msys64/clang64/bin` (pollution); new leaves `env["PATH"]` untouched.
2. **Live capture of the environment the tool builds.** The tool's real `main()` was
   run (preparation stage — `prepare.py`, `link_modules.py`, `content_profile.py`,
   red-zone/patch stage — for real) with `subprocess.Popen` intercepted only for the
   runtime launch, recording the exact `command`/`env` that would be passed:
   - Linux, default `MSYS2_ROOT`: `env["PATH"]` is **identical** to the process's
     original `PATH` (`path_untouched: true`); `command[0]` is the native
     `<repo>/gow3_pc/out/gow3-probe` (Task 5's fix).
   - Linux, `MSYS2_ROOT=/c/msys64`: same — the variable is now simply ignored on POSIX.
   - Simulated Windows on this machine cannot get further than `game_executable()`
     (there is no `gow3-probe.exe` here; the `SystemExit` names exactly the pre-change
     path), so the Windows side of the identity proof is item 1, which executes the
     actual statements of both versions.
3. **Full end-to-end run with the real game** (default mode = `--check-imports`, which
   prepares, patches and launches the runtime but never decodes guest code):

   ```
   GOW3_CPU_CHECK=0 TLOU_UPDATE=<.../CUSA00552-patch-1.11> TLOU_ISA_REWRITE=off \
       python3 tools/probe_tlou.py
   ```

   - Result: `Imports: 0 missing`, `exit_code: 0`, `elapsed_seconds: 0.916`. SDL3
     window created (Windowed, 1632x918), GTX 1060 + Vulkan presenter ready, all the
     patches applied (`frame-timeout`, `job-sleep`, `pro-shadows`,
     `ambient-shadows-half`, `all-languages`, `flashlight`, ...) — the same behavior as
     Task 5's run, now with an unpolluted `PATH`. This proves the native runtime really
     does not need anything from `clang64/bin`.

## Technical decisions

1. **Guard pattern: `sys.platform == "win32"`** — the pattern the roadmap's non-negotiable
   Windows rule prescribes for Python, and the one Task 5 already established in this
   very file (`game_executable()`). MSYS2's Python also reports `win32`, so the MSYS2
   development flow (which needs the `clang64/bin` entry for the runtime's DLLs) keeps
   the injection exactly as before.
2. **Guard both lines, not only the `PATH` one.** `msys` exists solely to feed the PATH
   line; leaving it unguarded would keep computing an unused (relative, on Linux) path.
   Guarding the pair keeps the diff minimal and the intent obvious.
3. **Comment added** explaining *why* the injection exists (the Windows runtime links
   against MSYS2's own DLLs), in the file's dense "why" comment style — so a future
   reader does not delete it as dead code, or "fix" the Linux side back.
4. **Scope kept to the two lines**, per the roadmap's evidence. The remaining
   Windows-only tools (`stall_watch.py`, `sample_threads.py`, `run_game.py`, ... —
   `llvm-nm.exe`/`llvm-symbolizer.exe`/`tasklist`) were not touched; they are
   diagnostic tools for later tasks (7/8/9/13/15).

## Windows compatibility guarantee (proof)

| Change | Effect on Windows |
|---|---|
| New guard `if sys.platform == "win32":` around the two statements | True for every Windows Python (native CPython and MSYS2 Python alike), so the body runs on the same call, in the same order, with the same local variables as before. |
| The two statements under the guard | Textually identical to the old statements (proved by extracting both from the files and asserting equality). Executed under simulated `win32`, the resulting `env["PATH"]` is byte-identical to the old result for default `MSYS2_ROOT`, custom `MSYS2_ROOT`, and unset `PATH`. |
| The comment line | No runtime effect. |
| Anything else in the file | Untouched (`git diff`: 3 added lines, 2 re-indented lines). |

Net: on Windows the child environment, the `PATH` value, the runtime's DLL lookup and
every other behavior are exactly as before.

## Risks / observations for the following tasks

- **Task 7 (`package_linux.sh`):** the packaged Linux runtime must ship only what
  `ldd gow3_pc/out/gow3-probe` + `ldd out/gpu/libgow3gpu.so` report (distro `.so`s);
  with this guard nothing in the boot flow references `C:/msys64` anymore, so there is
  no MSYS2 component to bundle or document.
- **Tasks 8/9 (Linux runner / Tkinter launcher):** the future Linux runner that
  reproduces `probe_tlou.py`'s environment must **not** add a `clang64/bin` entry
  (this file is the reference implementation now). On Windows the equivalent flow must
  **keep** it: `launcher/tlou_launcher.pyw:1863` already resolves
  `$MSYS2_ROOT/clang64/bin/SDL3.dll`, and the starter exe's flow depends on the same
  toolchain DLLs — when adapting `build-starters.sh`/the launcher, that Windows branch
  must stay byte-for-byte.
- **The `MSYS2_ROOT` variable on Linux is now a silent no-op** in this tool (as it
  should be). If a Linux user ever has a MinGW cross-toolchain they could not opt into
  anything — that was never the point of the variable; it documents the Windows layout.
- **Simulated-Windows end-to-end is impossible on this machine** (no `gow3-probe.exe`),
  so the Windows proof is statement-level. The change is two lines under a platform
  guard whose value is constant per platform, which is why statement-level identity is
  sufficient; a Windows machine should still do one final sanity run in Task 18/CI if
  one is added.
- **Verification artifacts are gitignored:** the end-to-end run wrote to
  `analysis/tlou` (logs, result JSON, boot view, patches) and `analysis/tlou-user` —
  covered by `.gitignore` (`/analysis/*`). `git status` shows only this task's files
  modified.
