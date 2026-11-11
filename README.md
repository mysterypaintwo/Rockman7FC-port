# Rockman 7 FC — Windows, macOS & Linux port

Modernizes **Rockman 7 FC** (by Mend) on all three platforms with the same
features everywhere:

* one settings window: window size, language, soundtrack, volume, every
  keyboard key and every gamepad button
* window sizes beyond the engine's 1×–3× (any integer size that fits your
  screen, plus integer-scaled fullscreen), with crisp pixels
* Japanese and English builds side by side, and either release's soundtrack
  with either language
* live BGM volume, SFX volume, full key and gamepad remapping

The game itself is unmodified. Everything is done by a small replacement
`pxtone.dll` that the game loads on its own, plus the settings program
`Rockman7FC.exe`. On macOS and Linux both run through Wine.

---

## Install

You need the original game folders: the Japanese release `final2`
(`rm7fc.exe`) and/or the English release `mm7fc` (`mm7fc.exe`). Either one is
enough; with both you can switch language and soundtrack freely.

| | Run | Installs to |
|---|---|---|
| **macOS** | `macos/Install Rockman 7 FC.command` (double-click) | `~/Library/Application Support/Rockman7FC`, apps in `/Applications` |
| **Linux** | `linux/install-linux.sh [final2] [mm7fc]` | `~/.local/share/rockman7fc`, `rockman7fc` + `rockman7fc-settings` |
| **Windows** | drag the game folder(s) onto `windows/install-windows.cmd` | `%LOCALAPPDATA%\Rockman7FC`, Start menu + desktop shortcuts |

The installers look for `final2` / `mm7fc` next to the port and in
Downloads, and otherwise ask. The macOS one also takes
`--game <final2> --english <mm7fc>`. Re-running an installer updates the port
and keeps your settings. Anything missing can also be added later with
**Settings → Add release…**.

**Wine** (macOS/Linux): `brew install --cask --no-quarantine gcenx/wine/wine-stable`
on macOS, your distribution's `wine` (32-bit support) on Linux. The Linux
installer can also download a portable build.

---

## Settings

Open **Rockman 7 FC Settings** (macOS app / `rockman7fc-settings` / Start
menu). Changes save immediately; **Play** starts the game.

* **Window size**: every integer scale that fits your monitors, plus
  **Fullscreen (Integer)** (exact pixels, black bars) and **Fullscreen
  (Stretched)** (as large as the screen allows, original 8:7 shape kept).
  1×–3× use the engine's own scaling; everything else is drawn by the port
  from the 1× picture, screen-shake effects included. Quit fullscreen with
  Cmd+Q on macOS or Alt+F4 elsewhere.
* **Language**: which build runs (Japanese text or English text). The subtitle
  shows that release's version (`ver.final2`) and year.
* **Soundtrack**: *Original* (Japanese release, 2008) or *Updated* (English
  release, 2017: re-done music, re-recorded effects, newer sound engine).
  Works with either language.
* **BGM / SFX**: on/off and 0–100. Drag a slider, scroll on it, or
  double-click the percentage to type a value. BGM volume changes while the
  game runs; SFX apply on the next start.
* **Controls → Keyboard / Gamepad**: click a slot, then press a key or button.
  Each action has a main and an alternate slot. Esc cancels, Delete clears.
  A key used elsewhere moves to the new action. **Defaults** resets that tab.
  The controller dropdown picks which controller the game listens to (or any).
* **macOS/Linux · Wine**: desktop window, renderer (GDI fixes black screens),
  gamescope fullscreen (Linux).
* **Appearance**: dark/light theme, accent color, zoom (defaults to your
  display scaling).
* **Restore defaults** (top right, click twice): resets all game settings,
  keeps the appearance.

All of it is stored in one file, `port.ini`, next to `Rockman7FC.exe`.

### Default controls

| Action | Keyboard | Gamepad (Xbox layout) |
|---|---|---|
| Move | Arrows | D-pad, left stick |
| Jump | Z | A |
| Shoot / confirm | X | X, B |
| Previous / next weapon | A / S | LB / RB |
| Weapon menu | Q | Start |
| Reset | R | Back |

---

## Release differences (checked file by file)

| | Japanese `final2` | English `mm7fc` |
|---|---|---|
| Executable | `rm7fc.exe`, 2008 | `mm7fc.exe`, 2017: same engine build, only the text differs (2,081 bytes) |
| Level data `stg/`, palettes `pc/` | identical | identical |
| Music `bgm/` | 2007–2008 | all 34 tracks revised (up to 2013), plus 5 unused extra files |
| Sound effects `se/` | 16-bit stereo | all 77 re-recorded, 16-bit mono (up to 2015) |
| `pxtone.dll` | 2007 | 2009, two extra exports |
| `readme.txt` | "ver.final2" | same text, unchanged |

The intro of the English build calls itself "Megaman 20th Anniversary".

---

## How it works

`Rockman7FC.exe` and `pxtone-proxy.dll` are 32-bit Windows programs (built
from `src/` with MinGW). When a release is added, its `pxtone.dll` is renamed
`pxtone_real.dll` and the proxy takes its place. When the game starts, the
proxy reads `port.ini` and:

* loads the real `pxtone.dll` of the chosen soundtrack and passes every call
  through to it unchanged
* writes the game's `config.ini` (scale, sound switches, default keys)
* rescales sound effects from pristine copies in `port/se_original/`
* redirects the game's `bgm\…` / `se\…` file opens when the soundtrack comes
  from the other release
* redirects the game's keyboard calls (`GetKeyState`, `GetKeyboardState`,
  `PeekMessageA`) for remapping, and feeds gamepad input (XInput, or the
  joystick API for other pads) through the same path
* stretches the per-frame `StretchDIBits` copy for sizes above 3× and
  fullscreen (also when the game offsets it for screen shake)
* wraps the game window so Cmd+Q (delivered by Wine as Alt+Q), Alt+F4 and
  window close always quit
* sets the BGM volume on pxtone's DirectSound output buffer (it hooks
  `DirectSoundCreate` in pxtone's own imports, so the game's SFX buffers are
  untouched). pxtone's built-in `pxtone_Tune_SetVolume` had no audible effect
  in the game, so it is only a fallback.

Without a `port.ini` the proxy changes nothing. Everything it does is logged
to `port.log` in the game folder; the macOS/Linux launcher logs to
`launcher.log`.

### macOS/Linux specifics (`launcher-core.sh`)

* private Wine prefix, with a Japanese-capable font aliased to MS Gothic so
  Japanese window titles don't show as `?????`
* sets Wine's `winebus` **DisableHidraw**: otherwise Wine hands Switch Pro /
  8BitDo / DualShock-style pads to games as raw HID, whose packet counter
  reads as phantom button presses
* desktop window, renderer and gamescope options from `port.ini`
* migrates installs from the old `settings.conf` layout automatically

### Building

```sh
brew install mingw-w64          # or: apt install gcc-mingw-w64-i686
tools/build.sh [path/to/rm7fc.exe]
```

This builds `build/pxtone-proxy.dll` and `build/Rockman7FC.exe`, generates the
icons from the game's own icon (`tools/make-icons.py`; Windows icon palettes
are BGR, and reading them as RGB is what once turned the helmet orange). The
installers in `windows/`, `linux/` and `macos/` take the binaries and icons
from `build/` and `launcher-core.sh` from the repo root, so there is one copy
of each. (In a packaged release, put those files next to the installer; it
looks there first.)

### Source layout

| Folder | What it is |
|---|---|
| `src/common/` | shared by both programs: `port.ini` actions and key names, paths, file helpers, gamepad reading |
| `src/proxy/` | `pxtone-proxy.dll`: `main.c` (start-up, config.ini), `exports.c` (forwarding to the real pxtone), `audio.c` (soundtrack, SE and BGM volume), `input.c` (key remapping, gamepad), `window.c` (quitting, display scaling) |
| `src/settings/` | `Rockman7FC.exe`: `main.c` (command line), `releases.c` (finding/adding releases, launching), `model.c` (settings ↔ `port.ini`), `draw.c` / `widgets.c` / `layout.c` (drawing the window), `window.c` (mouse and keyboard), `pad_devices.c` (controller names) |

Each folder's header (`proxy.h`, `settings.h`) lists what its files share.

---

## Troubleshooting

* **Game crashed earlier with `backtrace.txt` files**: fixed. The old proxy
  called `pxtone_Tune_SetVolume` with the wrong calling convention and
  argument type (it is `void __cdecl (float 0..1)`), which corrupted the stack
  and also muted the music.
* **Controller does nothing / presses by itself**: check the Gamepad tab's
  controller dropdown. On macOS/Linux the launcher's DisableHidraw fix applies
  on first launch; delete `prefix/.rockman7fc-hidraw` to re-apply it.
* **Black screen**: Settings → Renderer → GDI.
* **Logs**: `port.log` (in `ja/` or `en/`) and `launcher.log`.
* `[Debug] Trace=1` in `port.ini` logs the game's drawing and window calls.

---

## Credits

*Rockman 7 FC* by **Mend** (unofficial fan game, not affiliated with Capcom).
Music engine: pxtone by Daisuke "Pixel" Amaya. This port adds launchers,
settings and compatibility code only; it contains no game data.
