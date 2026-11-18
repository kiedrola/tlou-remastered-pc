# Task 10 — Window and Resource Icons, and the Overlay Font (Phase 2)

> **Status: COMPLETED AND VERIFIED.** Both things the roadmap asks to verify were verified on
> this machine (Arch Linux, AMD FX-8350, GTX 1060 3 GB, Python 3.14.10, Pillow 13.0.0, a
> Wayland session plus Xvfb): **(1) the window icon** — the runtime loads a BMP with
> `SDL_LoadBMP` (`gow3_pc/gpu/shim/window.cpp`, `GOW3_WINDOW_ICON`); on Linux nothing set that
> variable before, so the game's window had no icon in the X11 taskbar, while on Windows the
> window keeps the exe's own resource icon and the code path is untouched; and **(2) the
> overlay font** — `GOW3_FONT_PATH` is a *build-time* input only: `DejaVuSans.ttf`
> (759,676 bytes) is embedded into `libgow3gpu.so` at compile time (`.incbin`,
> `gow3_font_ttf`/`gow3_font_ttf_end`), so there is no runtime font path to fail. Two small,
> additive, platform-guarded changes make the Linux side actually work: `tools/probe_tlou.py`
> now converts the game's own `sce_sys/icon0.png` into the 256×256 RGB BMP and exports it as
> `GOW3_WINDOW_ICON`, and the launcher's `set_icon()` no longer calls `iconbitmap` (a
> Windows-only Tk option that raises `TclError` on X11/Wayland) — it converts the `.ico` to an
> in-memory PNG with Pillow and uses `iconphoto`. Decisive evidence: with the probe booting
> the real game under X11 (Xvfb), a live X11 tree dump caught the game's own window — title
> *The Last of Us™ Remastered — PC port by Ravencaio*, class `gow3-probe` — carrying
> `_NET_WM_ICON` with exactly one **256×256** icon whose center pixel matches the game's own
> `icon0.png`. The launcher's window carries it too (256×256 from the `.ico`, 512×512 from the
> game PNG). Windows byte-identical: the exact icon block extracted from the committed source
> and executed under a simulated `sys.platform` passes 5/5 (on `win32` it is a no-op that
> leaves the environment byte-identical). A boot gap found on the way is documented at the
> end (this CPU cannot run the guest: the MOVBE emulation table is Windows-only).

## What the task asked

> Verify cross-platform window/resource icons (the BMP loaded with `SDL_LoadBMP` vs. Windows
> resources) and the overlay font path (`GOW3_FONT_PATH` in `CMakeLists.txt`).

## Findings from reading the code

1. **The game window's icon** (`gow3_pc/gpu/shim/window.cpp`, `WindowSDL` constructor):
   `getenv("GOW3_WINDOW_ICON")` → `SDL_LoadBMP` → `SDL_SetWindowIcon` → `SDL_DestroySurface`.
   The block is *not* platform-guarded: on Windows simply nothing sets the variable, so the
   window keeps the icon baked into the exe resource (the `.rc`), and that is the correct
   behavior there. On Linux, before this task, nothing set it either — the window came up
   with no `_NET_WM_ICON`, i.e. a blank/default icon in the X11 taskbar. On Wayland there is
   no window-icon protocol at all; the compositor's app icon applies.
2. **The overlay font** (`gow3_pc/gpu/CMakeLists.txt`): `GOW3_FONT_PATH` points at
   `DejaVuSans.ttf` *at build time*; the `.ttf` is embedded with `.incbin` and exposed as
   `gow3_font_ttf`/`gow3_font_ttf_end` in `libgow3gpu.so`. The overlay reads the font from
   those symbols — there is no `fopen` of the font at runtime, so the Linux port has no
   font-path dependency to break (no "missing font" failure mode to guard).

## The change

Two files changed (+46/−2), both additive and platform-guarded; the Windows behavior is
byte-identical:

1. **`tools/probe_tlou.py`** (+31)
   - New `window_icon(game, out)`: converts the game's own `sce_sys/icon0.png` into
     `analysis/tlou/window-icon.bmp` — **256×256 RGB** (the BMP is the runtime's built-in
     icon format; 256 stays far from X11's 1 MiB per-property limit, see below — the taskbar
     scales it to its slot). Returns `None` without the picture or without Pillow: the window
     just has no icon, the boot is not affected.
   - In `main()`, only when `args.boot and sys.platform != "win32"`:
     `env.setdefault("GOW3_WINDOW_ICON", str(icon))` — a preset value from the environment
     is respected. On Windows the function is never called: the exe keeps its resource icon.
2. **`launcher/tlou_launcher.pyw`** (+15/−2, `set_icon`)
   - `self.iconbitmap(default=str(ICON))` is now inside `if sys.platform == "win32"`:
     `iconbitmap` is a Windows-only Tk option; on X11/Wayland it raises `TclError`, so with an
     `icon.ico` present the old code crashed the launcher on Linux.
   - Non-Windows: Pillow reads the `.ico`, re-encodes it as PNG **in memory**, and Tk takes
     it with `tk.PhotoImage(data=...)` + `iconphoto(True, ...)`. Any failure falls through to
     the pre-existing path (the game's `icon0.png` as `PhotoImage`), which already worked.

## Verification (Arch, FX-8350, GTX 1060, Python 3.14.10, Pillow 13.0.0, Xvfb :97)

1. **The game's own window, on X11 — the decisive test.** The probe was booted with the real
   game (CUSA00552-1.11, update merged), `GOW3_WINDOW_ICON=analysis/tlou/window-icon.bmp` and
   `SDL_VIDEODRIVER=x11` forced (see driver ordering below), on Xvfb :97. A small X11 tree
   dumper (`XQueryTree` + `XGetWindowProperty`, `/tmp/task10/tree_snap.cpp`) polled the root
   window and caught the live game window:
   ```
   0x40002f name="The Last of Us™ Remastered — PC port by Ravencaio" class="gow3-probe"
       _NET_WM_ICON: 1 icons, first 256x256, nitems=65538
       center pixel ARGB: 0xfffffffffff5f5f5
   ```
   `nitems=65538` is exactly `2 + 256·256` (width, height, ARGB pixels). The center pixel
   compared with the game's own `icon0.png` (512×512 resized to 256×256): `(245,245,245)` vs
   `(246,246,246)` — a 1/255 difference from the LANCZOS resampling: **the icon in the X11
   taskbar is the game's own icon**.
2. **SDL's driver ordering (why the test needed care).** On this machine the user session is
   Wayland, and SDL3 prefers the Wayland driver: without forcing `SDL_VIDEODRIVER=x11`, the
   game's window is created on the user's compositor (`/run/user/1000/wayland-1`) and an X11
   scanner sees nothing — the first two scan runs "found no window" for exactly this reason.
   The Wayland branch of `window.cpp` is exercised the same way: an `icon_test` binary using
   the same API sequence as the runtime (`SDL_InitSubSystem` → `SDL_CreateWindow` →
   `SDL_LoadBMP` → `SDL_SetWindowIcon`) runs on the user's Wayland session without failing
   (Wayland has no icon protocol to introspect; the compositor applies its app icon).
3. **`SDL_LoadBMP`'s format discipline.** Pointing `GOW3_WINDOW_ICON` at a PNG (instead of
   the BMP) returns `NULL` from `SDL_LoadBMP`: the window comes up with no icon and nothing
   crashes — BMP is the one format the runtime loads, and the code is already safe against
   any other format.
4. **X11's 1 MiB per-property limit.** A 512×512 icon stores 1,048,584 bytes in
   `_NET_WM_ICON` (observed on the launcher's game-PNG path) — at X11's per-property limit;
   256×256 stores 262,144 bytes. That is why the game icon is generated at 256×256, not 512.
5. **The launcher's icon (Xvfb).** The old code path with an `icon.ico` present raised
   `TclError` on Linux (the launcher would crash before drawing). With the new code the
   launcher's window carries `_NET_WM_ICON`: **256×256** when `launcher/assets/icon.ico` is
   present (Pillow ico→PNG), **512×512** without it (the pre-existing game-PNG path); the
   launcher exits 0 in both cases.
6. **Windows byte-identical.** `/tmp/task10/win32_identity_test.py` extracts the *exact*
   icon block from the committed `probe_tlou.py` source (it tests the shipped code, not a
   copy) and executes it under a simulated `sys.platform`: **5/5 pass** — `win32`+boot: no-op
   and the environment stays byte-identical; `win32` without boot: no-op; Linux+boot: the BMP
   is created (196,662 bytes) and the env key set; Linux with a preset `GOW3_WINDOW_ICON`:
   `setdefault` respects it; Linux without `icon0.png`: graceful no-op.
7. **The font.** `nm -S` on the built `libgow3gpu.so`: `gow3_font_ttf`…`gow3_font_ttf_end`
   span **759,676 bytes — exactly the size of `DejaVuSans.ttf`**, and the bytes extracted
   from the `.so` are identical to the TTF (byte-for-byte comparison). The font is a
   build-time input only: on Linux there is no runtime path to verify or to fail.

## Gaps found on the way (out of scope for this task, documented for Task 11)

1. **This CPU cannot run the guest (MOVBE).** The FX-8350 predates Ryzen and lacks MOVBE.
   The runtime's CPU check (`gow3_pc/src/probe.c`) stops with a clean message on such CPUs
   ("Intel Haswell 2013 or newer, or AMD Ryzen or newer, is needed"); the probe chain sets
   `GOW3_CPU_CHECK=0` because the missing sets are "emulated now" — but the emulation (the
   red-zone/ISA table; 157 MOVBE replacements for this game) is applied **Windows-only**
   ("Red zone: Windows-only protection ignored on POSIX"). Result: the window is created,
   the icon is set (verified above — the window outlives the crash), and about a second later
   the guest hits a raw MOVBE → SIGILL (occasionally a SIGSEGV elsewhere; non-deterministic).
   A full boot (Task 11) cannot complete on this CPU; on a Haswell-2013+/Ryzen CPU the check
   passes natively and no table is needed. Nothing icon- or font-related: both were verified
   on the live window before the crash.
2. **Xlib quirks (for the test tools).** `XQueryTree` and `XFetchName` return `1` on success,
   not `Success` (0); `XInternAtom(display, "_NET_WM_ICON", True)` yields a `BadAtom` when
   the atom is not yet known — `False` is the right flag.

## Files

- Changed: `launcher/tlou_launcher.pyw`, `tools/probe_tlou.py` (+46/−2).
- Generated (gitignored): `analysis/tlou/window-icon.bmp` (196,662 bytes, 256×256 RGB).
- Test artifacts (not committed, in `/tmp/task10/`): `icon_test.cpp` (SDL3 icon API on X11
  and Wayland), `tk_icon_test.py` (launcher icon paths), `win32_identity_test.py` (the
  byte-identity harness), `tree_snap.cpp` (X11 tree/icon dumper), `pillow_check.py` (Pillow
  13.0.0 BMP/ico round-trip). `iced_x86` 1.22.0 was placed in the project's gitignored
  `gow3_pc/out/pydeps` (the red-zone tool's dependency) — no system change.
