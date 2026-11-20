# Task 11 — Real Boot and the 30 fps Engine Speed (Phase 3)

> **Status: BOOT VERIFIED ON THE TASK 11 MACHINE (7950X / RTX 3090, Arch); FIRST PLAYTEST
> BUG FOUND AND FIXED.** The first real boot — `python3 tools/run_game.py --seconds 0`
> (the same path the `TLOU-Game` starter uses), real 1.11 `t1ps4` eboot plus its 1.11 update
> — boots cleanly to the main menu (PlayGo language prefetch), 1280×720 output, controller
> mapped (XInput→Sofa2), no device loss. The user's first playtest exposed the port's most
> visible bug: at the port's default **60 FPS limit the whole game runs at twice speed**
> (animations, physics, audio). Root cause and fix below. The remaining Task 11/12 work is a
> full play session at the corrected speed (and the optional Task 11.5: a true 60 fps mode).

## What the task asked

> Run the probe with a real copy of the game and its 1.11 update; confirm that the runtime
> boots, loads the shader cache, and enters the game.

## What was found

The game runs at 60 FPS stable and *everything moves too fast*. The suspicion was the guest
clock: on Windows the guest uses `rdtsc` (`GOW3_NATIVE_TSC`), and a wrong clock source on
Linux would scale all time-based code. It is **not** the clock:

- On POSIX `read_tsc()` / `process_time_counter()` return `CLOCK_MONOTONIC` in
  nanoseconds (`gow3_pc/src/runtime_kernel.c`) — no frequency guessing involved.
- The 1.11 `eboot.bin` contains **no raw `rdtsc` instructions** (only the 1.00 build has two,
  in a data-looking context). The Windows `GOW3_NATIVE_TSC` path is `#ifdef _WIN32` only.
- The guest's own wall clock works: the runtime's boot timing lines are sane.

The real cause is the **framerate the engine is designed for**. The TLOU Remastered engine
(`t1ps4`, PS4) is **frame-based at 30 fps**: its logic takes one fixed 1/30 step per
presented frame. Evidence in the 1.11 `eboot.bin`:

- `1/30f` floats (0x3D088888) occur **22×**, `1/60f` (0x3C23D70A) **12×** — the engine's
  per-frame constants are built around a 30 fps tick (TLOU Remastered is the "30 fps mode";
  PS4 Pro's 60 fps mode was a separate display-locked presentation — Digital Foundry:
  "1080p60 mode … locked … when console is set to 1080p output").
- The game's LISP scripts branch on **`is-locked-at-30fps?`** — behavior adapts to the mode.
- An in-game debug command exists: **`forcefps <number>`** — "force the framerate to
  `<number>`" (used with the debug console; that console is not wired in this port yet).

With the port presenting at 60 frames/s (its `fps_limit` default), the engine took 60 of
those 1/30 steps per second → **2× speed for animations, physics and audio** while the FPS
counter happily read 60. The upstream README already says so in practice: "a fixed 30 FPS
limit … makes motion clearly more even" (it was written for Windows, where 60 is the default
and players pick 30 in the menu).

## What was changed (POSIX-only; Windows untouched)

- `gow3_pc/gpu/shim/gow3_settings.h` — `fps_limit` default: `30` on POSIX, `60` on Windows
  (`#ifdef _WIN32` around the initializer). The live value still comes from the in-game
  menu / `tlou.ini`; this only changes what a fresh settings file starts with.
- `launcher/tlou_launcher.pyw` — the FPS field's default when `tlou.ini` has no `fps_limit`:
  `"30" if sys.platform != "win32" else "60"`. On `win32` the expression evaluates to the
  exact old value (verified by executing the new expression under a simulated `sys.platform`:
  Windows identical, Linux 30).
- `analysis/tlou-user/tlou.ini` — `fps_limit=60` → `fps_limit=30` (this user's settings
  file; on Windows the file is user-owned and never touched by the build).

No change was needed in the presenter: `VideoOutDriver::PresentThread` already paces
presentations at `fps_limit` when it is below the display refresh (the "unlocked + limit"
path), and the in-game menu row already shows the current limit.

## Verified

- Build: `gow3_pc/build.sh` clean (GPU library, starters executable).
- Real boot: `python3 tools/run_game.py --seconds 150` → menu reached (PlayGo prefetch),
  and the presenter now logs **`VideoOut: vblank 480 Hz, frame limit 30 FPS`** (was 60).
- Steady state (menu, after the asset-streaming burst):
  **`Frame stats: 30.0 FPS`** and **`Frame pacing: median 33.33 ms, stddev 0.05–0.34 ms,
  p99 ≤ 35.3 ms, 0 frames over 1.5x median`** — exactly 30 fps, evenly paced, ~58–88% GPU
  thread idle (headroom).
- During the menu's PlayGo streaming bursts the average dips (14–26 FPS) with p99 spikes:
  expected — a limit is a ceiling, and streaming work is CPU/page-tracking-bound
  (host copies up to 122 ms in a frame, 653 write faults/s). Same class of dips the user's
  60 fps session already showed at load.

## For the user (speed check)

Run `TLOU-Game` (or `python3 tools/run_game.py --seconds 0`) and play. Animations, walking,
gunplay and audio should now be at normal speed at a steady 30 FPS (the overlay shows the
FPS; the in-game menu's "Frame rate limit" row also shows 30). **For this game, keep the
limit at 30: any higher value makes the game run faster, not smoother** — the engine is
30 fps frame-based; 60+ is only useful for gow3-style games. If a true 60 fps mode is ever
wanted, that is Task 11.5 (force the engine itself to tick at 60: the `forcefps` debug
command / the LISP `is-locked-at-30fps?` / the debug menu's Framerate option), not a
present-side change.

## Windows safety

- `gow3_settings.h`: the `#ifdef _WIN32` branch is the exact original line
  (`std::atomic<int> fps_limit{60};`); the POSIX default is new code only.
- `tlou_launcher.pyw`: on `sys.platform == "win32"` the new expression returns the exact
  old string; executed under a simulated platform, Windows output is identical.
- `tlou.ini`: a user data file, never modified by the build on any platform.

## Open (Task 12 territory)

- A full start-to-finish play session at 30 fps (controller recorded with `GOW3_PAD_RECORD`)
  to confirm no device loss / hangs in long sessions — the original Task 12 ask.
- Optional Task 11.5: true 60 fps engine mode (see roadmap).
