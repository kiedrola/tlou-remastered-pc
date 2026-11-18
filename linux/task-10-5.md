# Task 10.5 — Red-zone / ISA-emulation Table on POSIX (Phase 2)

> **Status: COMPLETED AND VERIFIED.** The red-zone/ISA table is now loaded, applied, and
> diagnosed on POSIX, exactly as on Windows. One file changed (`gow3_pc/src/probe.c`,
> +26/−22): the `#ifdef _WIN32` gate that turned `--red-zone` into a no-op with the message
> "Red zone: Windows-only protection ignored on POSIX" is gone (the loader, the stub-offset
> check, and the applier were already portable C, and they are), and the POSIX signal handler
> gains the same two diagnostics the Windows vectored handler has — `note_red_zone_fault`
> (the `GOW3_RED_ZONE_STATS=1` counter) and fault-site recognition via
> `red_zone_site_of_stub`. Decisive evidence on this CPU (AMD FX-8350, pre-Ryzen, no MOVBE):
> the first boot now prints **"Red zone: 6528 instructions protected in 5990 patches (29
> through relays), 0 patches skipped"** (154 MOVBE instructions emulated by code) and the
> guest, which before this task died at ~1 s with `Guest fault (signal 4)` — a raw MOVBE
> decoding as SIGILL — now **runs the game for 300 s**: all TTY subsystems initialize
> (build 2863488), the world switches to `core`, the render loop is stable (293 draws / 104
> dispatches per 5 s report), 49,152 GPU page-tracking faults are handled, and the new POSIX
> stats line works: "Red zone: 41 of 49152 guest faults came from protected instructions".
> Windows stays byte-for-byte identical (the Windows `#ifdef _WIN32` path is untouched; proof
> below). This unlocks running the game on any x86-64 CPU (pre-Haswell-2013 / pre-Ryzen) and
> was required before Task 11, whose first full boot exercises the applied table.

## What the task asked

> Apply the red-zone/ISA table on POSIX: lift the `#ifdef _WIN32` gate around
> `red_zone_load` (the stub-offset/page-size check is platform-independent; `red_zone_apply`
> and the `protect()` of the stub area are already shared code and are correct under
> `mmap`/`mprotect`, since the image is one contiguous RW mapping below 2 GiB and the stubs
> sit right after it, within the rel32 jump range). Add `note_red_zone_fault` and stub
> recognition via `red_zone_site_of_stub` to the POSIX signal handler for diagnostics parity.
> The protection is needed on Linux for the same reason it exists on Windows: the kernel
> pushes its signal frame below RSP on every GPU page-tracking fault, corrupting the red zone
> the guest code uses as scratch. Windows stays byte-for-byte identical: only the gate moves,
> the `#ifdef _WIN32` path is untouched. Verify: on the FX-8350 the first boot prints "Red
> zone: N instructions protected…" instead of "ignored" and passes the ~1 s SIGILL; on a
> modern CPU (the Task 11 machine) a full-boot regression with the table applied (red-zone
> protection only, no ISA replacements). **Do it before Task 11**, so the first full boot
> already exercises the applied table.

## Findings from reading the code

1. **`gow3_pc/src/red_zone.c` + `red_zone.h` are 100% portable C** (already compiled and
   linked on POSIX): `red_zone_load` reads the `G3RZTAB1` table from disk,
   `red_zone_matches` checks the table against the loaded image (per-site byte compare),
   `red_zone_apply` writes the 5-byte `jmp rel32` patches into the image and the emulation
   stubs after it, and `red_zone_site_of_stub` maps a stub-area RIP back to the protected
   site. No Windows API anywhere — the only platform dependency is memory it is given.
2. **The gate in `main()`** (`probe.c`): `red_zone_load` ran only inside `#ifdef _WIN32`; on
   POSIX the `#else` printed "Red zone: Windows-only protection ignored on POSIX" and
   `red_zone` stayed `NULL`, so `red_zone_apply` was a no-op and the stub area was never
   reserved.
3. **The memory model makes the stubs reachable on POSIX unchanged.** `runtime_low_map`
   allocates one contiguous RW mapping below 2 GiB sized `round_page(image) +
   round_page(stubs)`; the patch `jmp`s are rel32 from the image into the stub block right
   after it, and `protect()` (mprotect on POSIX) applies the stub-area flags. Nothing in the
   apply path needed to change.
4. **The POSIX handler lacked both diagnostics the Windows handler has.**
   `vectored_fault` (`#ifdef _WIN32`) calls `note_red_zone_fault(rip)` on every handled GPU
   fault (feeding the `GOW3_RED_ZONE_STATS=1` counter) and its address description
   recognizes stub RIPS via `red_zone_site_of_stub`. The POSIX `fault()` handler printed
   only the raw guest offset — a stub fault would look like a mystery fault in the stub area.
   The handler change also reuses the `rip` it had to fetch anyway, and notes in the comment
   why the protection matters here: the kernel's signal frame lands below `rsp` on the way,
   in the bytes the stubs keep out of the guest's reach.
5. **The table was already being generated correctly on POSIX** (Task 10 found this):
   `red_zone_table.py` with `--isa auto` emits the protection table *and* the software
   replacements for the missing instruction sets (`cpu_features.py` sees this CPU lacks MOVBE;
   the game uses 154 `movbe`/`lzcnt`-era instructions in the 1.11 image) — 154 replacements,
   0 failures. Only the loader refused to use it.
6. **Why the protection exists (same on both platforms):** the GPU library tracks guest
   memory by write-protecting pages and resolving the fault in-process. On every such fault
   the OS pushes its signal frame *below RSP*; the guest code treats the page below its stack
   as scratch (the "red zone"), so the fault frame would corrupt it. The red zone rewrites
   the handful of instructions that touch those bytes into stubs that park the scratch
   elsewhere. On a CPU without MOVBE the same table additionally emulates those instructions
   — the boot gap Task 10 hit is exactly this.

## The change

One file changed: **`gow3_pc/src/probe.c`** (+26/−22). Three hunks:

1. **`main()` — the gate is lifted.** The `#ifdef _WIN32 … #else … #endif` around
   `red_zone_load` + the stub-offset/page-size check is removed; both platforms now run the
   same statements (the `#else` branch, a print, is gone). The check stays: a table whose
   `code_offset` does not match `round_page(image_size)` is refused with the existing message
   ("protection unavailable (stub offset does not match this page size)").
2. **`note_red_zone_fault` moves to shared code.** It was defined inside the `#ifdef _WIN32`
   block (only `vectored_fault` used it); it now sits in the shared section next to
   `guest_exit`. Same body, one definition on both platforms; the POSIX handler calls it on
   every handled GPU fault, so `GOW3_RED_ZONE_STATS=1` works identically on Linux.
3. **The POSIX `fault()` handler gains diagnostics parity.** `rip` is fetched once at the
   top (it was fetched at the end); `red_zone_site_of_stub` is consulted before printing, so
   a fault inside a stub is reported as
   `Guest fault (signal N) at guest offset 0x… (red-zone stub), address …` (the protected
   site, matching the Windows address description); a fault that is not in the guest is
   handled exactly as before. The GPU-fault path now reports into the red-zone stats counter
   before returning.

No other file needed changing: `red_zone.c`/`red_zone.h` were already portable, the tool
(`probe_tlou.py`) already passes `--red-zone` and builds the table with `--isa auto` on every
platform, and the applier/`protect()` path is shared code.

## Windows compatibility (proof)

No MinGW/MSYS2 toolchain is available on this machine, so the proof is by guard analysis,
as in previous tasks:

| Changed region | Platform | Impact |
| --- | --- | --- |
| `note_red_zone_fault` moved from `#ifdef _WIN32` to shared section | Windows | Compiles once, at a different line, with an identical body using only shared symbols (`red_zone`, `image`, `getenv`, `printf`, `PRIu64`, `__atomic_*`). One definition before and after. |
| `note_red_zone_fault` (same) | POSIX | New symbol (previously absent). |
| `fault()` handler edits | Windows | None — the whole function is inside `#ifndef _WIN32`. |
| `fault()` handler edits | POSIX | Diagnostics only: one early fetch of `rip`, one extra branch on `red_zone` (NULL-checked), one call into the existing counter. Control flow for unhandled faults is unchanged. |
| `red_zone_load` gate removal in `main()` | Windows | The statements inside the old `#ifdef _WIN32` are unchanged — exactly what Windows compiled before. |
| `red_zone_load` gate removal in `main()` | POSIX | New behavior: the table is loaded and applied (previously skipped with a note). |

Nothing in the Windows `#ifdef _WIN32` block was edited; the removed `#else` branch is POSIX
code; the moved function references no platform-specific symbols.

## Verification (Arch, FX-8350, GTX 1060 3 GB, Python 3.14.10, Xvfb :97)

1. **Build:** `gow3_pc/build.sh --test` — clean build, all 10 test binaries pass
   (`set -euo pipefail` would have stopped on the first failure).
2. **First boot, standard tool** (`probe_tlou.py --boot`, 01.00 base eboot, no update found
   through the resolved game path): the table is regenerated for this image (157 MOVBE
   replacements: 22 loads, 135 stores) and **applied**:
   `Red zone: 6506 instructions protected in 6001 patches (28 through relays), 0 patches
   skipped`. The guest, which previously died at ~1.1 s with `Guest fault (signal 4)` at a
   MOVBE, now runs until the 10 s runtime watchdog (exit 142, SIGALRM) with live thread
   stacks inside guest code (e.g. `image-relative=0xbae8e3`).
3. **Standard boot, canonical game** (01.00 base + 01.11 update via `TLOU_UPDATE`):
   `Red zone: 6528 instructions protected in 5990 patches (29 through short-jump relays) over
   538 red-zone functions … (table kept)` — the cached table (154 MOVBE replacements: 20
   loads, 134 stores, 0 failures) is reused and applied. No `Guest fault (signal …)` line
   anywhere in the log.
4. **300 s run, watchdog disabled** (same tool chain; the runtime command is the one written
   to `boot.json` plus `--timeout 0`, intercepted in `subprocess.Popen` so the prepare/env/
   command come from the real code): the guest survives the full 300 s and is killed only by
   the tool's own limit (`timed_out: true, exit_code: -9`). Along the way: every TTY
   subsystem initializes (`TTY init: …` through `cutscene`, `build 2863488 (Sep 30 2020)`),
   `Switching world: from to core`, all PRX modules load, and the render loop is steady
   (5 s reports: 293 draws / 104 dispatches / 194 submissions, 82 graphics + 32 compute
   pipelines, no pending compiles after warm-up, video memory climbing as assets stream:
   449 MiB / 145 images). The new POSIX stats counter works:
   `Red zone: 41 of 49152 guest faults came from protected instructions` — 49,152 GPU
   page-tracking faults resolved in-process, 41 of them with the RIP in a protected
   instruction (the kernel signal-frame case the red zone exists for).
5. **Before/after on this CPU:** before — window up, icon set, ~1.1 s later
   `Guest fault (signal 4) at guest offset 0xcafaa3` (SIGILL on a MOVBE; Task 10 documented
   the gap). After — 300 s of stable guest execution, no faults, MOVBE emulated in code.

The second verification criterion of the task (full-boot regression on a modern CPU, red-zone
protection without ISA replacements) is deferred to the Task 11 machine, as the task itself
states.

## Gaps found on the way (out of scope for this task, documented for Task 11)

1. **The POSIX watchdog kills every session at 10 s.** `probe.c` installs
   `alarm(timeout_seconds)` (default 10) with a handler that dumps the thread stacks and
   `_exit(128+SIGALRM)`; on Windows the branch compiles `(void)timeout_seconds` — no
   watchdog at all. `probe_tlou.py --boot` never passes `--timeout`, so on Linux a normal
   boot cannot outlive 10 s (this task's 300 s run needed `--timeout 0`). Task 11's full
   boot needs a decision: have the tool pass `--timeout 0`, make the watchdog
   self-extending after guest entry, or both. This predates this task (it was already in
   place when Task 10 hit the MOVBE wall) and is orthogonal to it.

## Files

- Changed: `gow3_pc/src/probe.c` (+26/−22).
- Generated (gitignored, `analysis/tlou/`): `red-zone.bin` (467,518 bytes, the 1.11-image
  table: 6528 protected instructions in 5990 patches + 154 MOVBE emulation stubs),
  `red-zone-isa.txt`, the refreshed `boot.bin`/`boot-linked.bin`/`boot.log`/`boot.json`.
- Test artifact (not committed, in `/tmp/task105/`): `run.py` — the `subprocess.Popen`
  intercept that appends `--timeout 0` to the runtime command while everything else (prepare,
  env, command) comes from the real `probe_tlou.main()`.
