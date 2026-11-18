# Task 9 — The Launcher on Linux Python: Key Remapping and SDL3 Discovery (Phase 2)

> **Status: COMPLETED AND VERIFIED.** The Tkinter launcher runs on the Linux Python (the
> `.pyw` is passed to `python3` by `TLOU-Launcher`, Task 8; the `tkinter`/Pillow imports are
> present in the system and the bundled Python) and now works *correctly* on the two things
> that had no Linux counterpart: **keyboard remapping** and **controller discovery**.
> Remapping was the real problem — the launcher previously had no Linux key path at all
> (`key_from_event` was pure Windows `MapVirtualKeyW`), and a naive character-based one would
> be layout-dependent (the test machine is Spanish, where `ñ`/`ç`/dead-accents type accented
> characters while the game's SDL3 names the *physical* key). The launcher now replicates
> SDL3's own mapping algorithm (its X11 and Wayland drivers both build the key layout from
> `xfree86_scancode_table2`, indexed by hardware key code, with a keysym override for
> non-character keys), using four tables generated from SDL3 3.4.20's own source, so a bind
> made in the launcher resolves in the game to the same physical key on any layout, and
> matches a bind made on Windows. The Controller tab loads the distro's `libSDL3.so.0`
> (the package does not ship SDL3; it is the distro's, the same library the game runtime
> loads). Verified on Arch Linux: a unit harness against the machine's live Spanish-layout
> X11 keycodes (`0 failures`, every name resolving through the real `SDL_GetScancodeFromName`),
> and a two-display Xvfb end-to-end test that injects real XTEST key events into a live Tk
> window (display `:91`) and a live SDL3 window (display `:92`) and compares the launcher's
> name with SDL3's name for every key: **51/51 match, `BAD: 0`**. Windows byte-identical
> (the only file changed is `launcher/tlou_launcher.pyw`, +166/−13, all additive and
> platform-guarded).

## The change

One repository file changed, `launcher/tlou_launcher.pyw` (+166/−13):

1. **Four embedded mapping tables** (static Python literals, generated — see *How the tables
   were made*). The game resolves `key_map` entries with `SDL_GetScancodeFromName`
   (`gow3_pc/src/runtime_pad.c`), so every value is an SDL scancode name:
   - **`LINUX_XFREE86_2`** (175 entries) — SDL3's `xfree86_scancode_table2`, the table its
     X11 *and* Wayland drivers use to build the key layout: index = X11 key code − 8 (Tk's
     `event.keycode` on the X11 backend) = Linux input key code → SDL name. This is the
     physical position: `25: "W"`, `49: "`"` (the key that is `` ` ``/`ñ`/masculine
     depending on layout), `125: "Keypad ="`.
   - **`LINUX_KEYSYM_NAMES`** (220 entries) — the X keysym name Tk reports for keys with no
     character of their own (arrows, F keys, keypad, modifiers, media), read at group 0 /
     level 0 — the same level SDL3 reads — → SDL name: `"Next": "PageDown"`,
     `"KP_Divide": "Keypad /"`, `"XF86AudioMute": "Mute"`.
   - **`LINUX_CHAR_NAMES`** (77 entries) — the character a key types → SDL name, the fallback
     when a Tk event carries no usable key code: `" ": "Space"`, `"é"`'s physical key is not
     here (it binds by position), but characters SDL can resolve by name are.
   - **`LINUX_CHAR_SC`** (53 scancode names) — SDL3's "this scancode's keysym is a character"
     set, needed to apply SDL's override rule below.
2. **`_linux_backend()`** — `'wayland'` when `WAYLAND_DISPLAY` is set and `DISPLAY` is not,
   else `'x11'` (a Tk on XWayland is still the X11 backend). It only decides whether Tk's
   `event.keycode` is an X11 code (Linux input code + 8) or a Linux input code (native
   Wayland), i.e. whether to subtract 8. The launcher has no single global Tk root the helper
   can query, so the backend comes from the environment.
3. **`_linux_key_name(keysym, keycode)`** — the mapping itself, replicating SDL3's
   `X11_InitKeyboard` for real key events:
   - position name = `LINUX_XFREE86_2[keycode − 8]` (the key's physical home);
   - keysym name = `LINUX_KEYSYM_NAMES[keysym]` for named keys (single characters map by
     position/character, never by name — that is what makes binds layout-independent);
   - when both exist and agree: the position name; when one is missing: the other, else the
     character fallback; when they **disagree**: the keysym name wins — except for character
     keys (`LINUX_CHAR_SC`) and the eleven fixed scancodes SDL3 does not let a keysym
     reassign (Escape, CapsLock, Numlock, L/R Shift, L/R Ctrl, L/R Alt, L/R GUI) — exactly
     SDL3's remappability rule, the one that makes `F24` bind as `F24` where the position
     table says `ModeSwitch`.
4. **`key_from_event()`** — the Windows branch is the old code verbatim (`KEYSYM_NAMES`
   first, then `MapVirtualKeyW` → scan-code table); on Linux it delegates to
   `_linux_key_name`.
5. **`sdl_candidates()`** — on non-Windows it also yields the loader name `"libSDL3.so.0"`,
   the distro's SDL3 resolved against the system (the Linux package does not ship SDL3;
   `licenses/THIRD-PARTY.txt` documents it as the distro's, and it is the same library the
   runtime loads, so the Controller tab reads the pad exactly as the game will).
6. **`PadReader.library()`** — the "skip it if not a file" guard now applies only on Windows
   (a loader name like `libSDL3.so.0` is not a file, but the dynamic linker resolves it);
   docstrings updated.
7. **`open_log()`** — the POSIX `xdg-open` is wrapped in `try/except OSError`: a bare
   console without `xdg-open` must not crash the launcher when opening the log.
8. **Controller-tab message** — the "SDL3 not found" text now says `SDL3.dll` on Windows,
   `libSDL3.so.0` on Linux.

### How the tables were made

Not by hand and not by an ad-hoc Tk/X11 name dictionary — both were tried first and both
failed: character-based mapping is layout-dependent, and Tk's X keysym names are not SDL's
name space (Tk reports `"Next"`, SDL says `"PageDown"`; Tk's `"Menu"` keysym is SDL's
`SDL_SCANCODE_APPLICATION`, which SDL3 *renames* to `"Menu"` at video init on X11, Wayland
*and* Windows). The tables are generated from SDL3 3.4.20's own mapping code: a standalone C
program embeds `src/events/SDL_keysym_to_scancode.c`, `src/events/scancodes_xfree86.h` and
`scancodes_linux.h` from the official 3.4.20 tarball (the installed version) and runs SDL3's
real `SDL_GetScancodeFromKeySym` path against the machine's X keymap, plus the raw
`xfree86_scancode_table2`. Two of the library's quirks had to be replicated, not called:

- **`SDL_Init()` returns a C `bool`** (nonzero = success) — the initial `!= 0` *failure*
  check was inverted and the generator "worked" on nothing.
- **`XStringToKeysym` (libX11 1.8.14) segfaults on this machine**, so the character→keysym
  step uses a local implementation of its documented stable behavior (7-bit ASCII = the
  value, `0x80`–`0xFF` = `0x01000000|c`); and SDL3's driver-level rename
  `SDL_SCANCODE_APPLICATION → "Menu"` is replicated in the generator, otherwise the emitted
  table says `"Application"` and the game would not resolve it.

The generator emits one JSON file; a compact script turns it into the four Python literals
now embedded in the launcher, each with a comment pointing at this report. Regenerating is a
one-step build for a new SDL3 — the tables are data, not logic.

## Verification environment (Arch Linux)

Same machine as Tasks 1–8: x86_64, AMD FX-8350, NVIDIA GTX 1060, Python 3.14.10 (system,
with Tkinter and Pillow), Tk 8.6.16 (X11 windowing), SDL3 3.4.20 (`/usr/lib/libSDL3.so.0`,
X11 + Wayland drivers), **Spanish keyboard layout**. The desktop session is XWayland/KWin
(`:0`), which does not accept synthetic X-level focus for end-to-end capture, so live
verification ran under Xvfb: display `:91` (the Tk/launcher side only) and display `:92`
(SDL3 side only), with real key events injected at the X protocol level through
`XTestFakeKeyEvent`. The launcher was imported as a module (`SourceFileLoader` —
`spec_from_file_location` returns a loaderless spec for `.pyw` files), not re-run as an app.

## Verification (all on this machine)

1. **The launcher compiles and imports on Linux Python.** `python3 -m py_compile
   launcher/tlou_launcher.pyw` is clean; the module imports on the system Python (and the
   bundled one: Task 8 verified `./TLOU-Launcher` opening a real window from a fresh
   extraction — the `.pyw` is passed to `python3`, `tkinter` and `PIL` are present).
2. **Unit harness against the live Spanish layout** (`0 failures`, ~70 checks). The expected
   keycodes are the machine's real X11 ones, dumped with `XkbKeycodeToKeysym` at group 0 /
   level 0 — the same level SDL reads — so the cases double as proof of layout-independence:
   - simulated Windows (`sys.platform = "win32"`): the old behavior is reproduced exactly —
     `KEYSYM_NAMES` first, `("w", 0x11)` → `None` without `windll`, exactly two
     `sdl_candidates`, both `Path`, the Windows paths;
   - Spanish-layout traps: `("ñ", 49)` → `` "`" ``, `("é", 48)` → `"'"`, `("dead_grave", 34)`
     → `"["` (the dead key is the physical `[`), `("masculine", 49)` → `` "`" ``,
     `("ntilde", 47)` → `";"`, `("+", 21)` → `"="` (shifted `!` key: the physical `=`
     position), `("Multi_key", 49)` → `` "`" `` (an unknown named key falls back to the
     position);
   - the old provisional tables had two wrong entries, now corrected and checked:
     `("Delete", 119)` → `"Delete"` (was `"Keypad ."`), `("KP_Equal", 125)` → `"Keypad ="`
     (was `"Numlock"`);
   - the override rule: `("F24", 93)` → `"F24"` where the position table says `ModeSwitch`;
     `("ISO_Level3_Shift", 92)` → `"Right Alt"` (fixed scancode, position wins);
   - **every name the Linux path can yield, and every name in all four embedded tables,
     resolves through the real `SDL_GetScancodeFromName`** in the installed `libSDL3.so.0`,
     and `"Application"` does not appear anywhere (the `"Menu"` rename is in the tables);
   - controller side: `sdl_candidates()` ends in the distro name, `PadReader.library()`
     loads the distro SDL3, `PadReader().ready` is `True` with no pad connected (it reports
     "no pad", not "no SDL3");
   - unchanged behavior: `game_command`/`game_env`/`python_exe`/`key_label` checks pass.
3. **Two-display end-to-end: real key events, both sides, every key** — the strongest
   check short of a human:
   - display `:91`: a live Tk window with the launcher's `key_from_event` bound to
     `<KeyPress>`; display `:92`: a real SDL3 window (`SDL_Init` + `SDL_CreateWindow`);
   - the same XTEST key event is injected on both displays for 50 keys (letters, digits,
     punctuation, `F1`/`F5`/`F12`, `Return`/`Tab`/`Space`/`BackSpace`, arrows, `Home`/
     `PageUp`/`Delete`/`End`/`Insert`, the numpad incl. `KP_Equal`/`KP_Divide`/`KP_Enter`,
     both shifts, both alts, `Super_L`/`Super_R`, `Menu`, `Mute`, `Pause`);
   - the Tk side yields the launcher's name, the SDL3 side yields
     `SDL_GetScancodeName(event.scancode)` from the key event SDL3 actually received;
   - **51/51 match, `BAD: 0`** — e.g. `kc=125 tk=('KP_Equal') sdl=Keypad = launcher=Keypad
     =`, `kc=135 tk=('Menu') sdl=Menu launcher=Menu`, `kc=138 tk=('SunProps') sdl=AC
     Properties launcher=AC Properties`. Whatever physical key the player presses, the
     launcher writes the name the game will assign to that key.
4. **The same-display combined Tk+SDL capture was attempted and is unreliable** (documented,
   not a launcher bug): on a WM-less X11 display SDL3 forces X input focus to its own window
   when it maps (`!data->videodata->net_wm` branch in `SDL_x11window.c`) and keeps the
   keyboard grab until it processes a FocusOut; Tk's `focus_force()` takes an X grab that
   blocks the other side; and one of the probes crashed in libX11 — which turned out to be a
   real API change, not our code: **libX11 1.8.14's `XFree` is now `int XFree(void*)`**
   (single argument, verified against the upstream 1.8.14 source), so the classic two-arg
   call `XFree(dpy, ptr)` silently freed the *Display*. The two-display design sidesteps all
   of it (no focus contention; no XFree needed on the path that matters), and the single-
   argument `XFree` was confirmed safe in the harness.

## Technical decisions

- **SDL3 scancode names are the binding vocabulary.** The game resolves `key_map` entries
  with `SDL_GetScancodeFromName` (`runtime_pad.c`); a name the game cannot resolve is a dead
  bind, so the launcher writes exactly the names SDL3's drivers produce — on Windows (Task
  9's pre-existing `SCAN_NAMES` via `MapVirtualKeyW`) and now on Linux.
- **Position first, keysym second, SDL's exact override rule** — not a Tk/X11 name
  dictionary. SDL3's X11 driver fingerprints the keyboard and builds `key_layout[keycode] =
  xfree86_scancode_table2[keycode − 8]` (on this machine the fingerprint distances are 0 for
  table2 and 6 for table1), then overrides with the keysym for non-character remappable
  keys; the Wayland driver uses the same table2 indexed by the Linux input code. Replicating
  the algorithm (with its eleven fixed scancodes) is what makes dead keys, accents and
  ISO/AZERTY boards behave as in-game, on both backends with one table.
- **The tables are embedded data, generated once.** The mapping code in the launcher is pure
  Python over static literals: no FFI at mapping time (no new segfault risk class), no build
  step, identical in the repository and in the package. A new SDL3 means regenerating the
  literals (the generator and this report are the recipe), not rewriting logic.
- **`_linux_backend()` from the environment**, because the launcher exposes no single global
  Tk root to the helper; the rule (`WAYLAND_DISPLAY` set and `DISPLAY` unset → Wayland)
  matches Tk's own backend choice (XWayland clients are X11).
- **The Controller tab uses the distro's SDL3** (`libSDL3.so.0`), consistent with the package
  not shipping SDL3 (Task 7) and with the runtime loading the same distro library — one pad
  stack, no version skew between "what the launcher sees" and "what the game sees".
- **`xdg-open` failures are non-fatal**: `open_log` swallows `OSError` — a missing
  `xdg-open` (bare console, minimal box) must not take the launcher down; the log's path is
  on screen either way.

## Windows compatibility guarantee (proof)

- `git status` shows exactly one changed file: `launcher/tlou_launcher.pyw` (+166/−13). No
  Windows file was touched (`tlou-launcher.c`, `tlou-game.c`, `build-starters.sh`, the
  Windows package scripts are unmodified).
- `key_from_event`: the first line (`KEYSYM_NAMES` lookup) and the whole
  `sys.platform == "win32"` branch (`MapVirtualKeyW` → `SCAN_NAMES`) are the old code
  verbatim; `_linux_key_name` is only ever reached when the platform is not Windows.
- `sdl_candidates`: the two Windows yields (`gow3_pc/out/SDL3.dll`, MSYS2's `clang64/bin/
  SDL3.dll`) are unchanged and still first; the distro yield sits inside
  `if sys.platform != "win32":`. The unit harness reproduces the Windows candidate list
  byte-for-byte under a simulated `sys.platform`.
- `PadReader.library`: the not-a-file skip is now explicitly `and sys.platform == "win32"`
  — on Windows the loop body is unchanged.
- `open_log`: the `sys.platform == "win32": os.startfile(LOG)` branch is verbatim; only the
  POSIX branch gained the `try/except OSError`.
- The new tables and helpers are dead code on Windows (never referenced: `key_from_event`
  returns before reaching them).

## Risks / observations for the following tasks

- **Native-Wayland Tk was not exercised live** (this session's Tk is XWayland/X11; Xvfb is
  X11). The Wayland branch is one line of keycode arithmetic (`no −8`, Tk's Wayland backend
  reports Linux input codes, the same indexing SDL3's Wayland driver uses) and is unit-
  checked, but a player on a pure-Wayland desktop is the first to run it; if Tk ever reports
  X11-style codes there, the symptom would be a one-keycode-wide bind mismatch, trivially
  fixable in `_linux_backend`.
- **The tables track SDL3 3.4.20** (the distro's current). If SDL3 changes
  `xfree86_scancode_table2` or the driver renames, regenerate the literals — the comment
  above the tables points at this report. The game runtime and the launcher load the *same*
  distro SDL3, so in practice they skew together.
- **A character with no key code that SDL cannot resolve stays unbindable** (e.g. a pasted
  `"é"` event): `key_from_event` returns `None`, the same as Windows for unknown keys. In
  real use Tk always sends the key code, so this only matters for synthetic events.
- **The Controller tab is verified to load and report "no pad"**, not to read a live pad
  (no pad on this machine); the Windows pad path is unchanged and was working before.
- **libX11 1.8.14's one-argument `XFree`** is a trap for any future X11 `ctypes` work in
  this project (the classic two-arg call frees the `Display`); noted here because it cost a
  debugging cycle. The launcher itself does no X11 FFI.
