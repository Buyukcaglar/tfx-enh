# TFX Enhanced — user guide

TFX Enhanced runs the DOS game TFX natively on Windows from your own copy
of the game. This guide covers getting started, the keys the program adds,
the settings, and the command line. For flying the aircraft and the game's
own keys, use the game's manual: they are exactly as under DOS.

## 1. Before you start

You need:

- **Windows 10 or 11, 64-bit.**
- **Your installed TFX folder**, version 2.0 (Ocean's CD edition, with its
  patch), the folder you ran `TFX.BAT` from under DOS. The files that
  matter are `TFX.DAT`, `DID.DAT` and `START.EXE`, plus `TUNES\`,
  `MISSIONS\MISS830.MIS`, `WAYBIN\COL`, `WAYTXT\COL` and `TFX.CFG`.
- **Optional, for the CD music:** the TFX CD as a **CUE/BIN image** (a
  `.cue` sheet with its `.bin` file). Without it the game plays its
  synthesized music only.

TFX Enhanced never changes, moves or deletes any of these files; it only
reads them.

## 2. Installing

1. Download `tfx.exe` from the project's latest release.
2. Put it in a folder of its own that you can write to, for example
   `C:\Games\TFX Enhanced\`. The program keeps its copies of the game and
   your saves next to itself (see section 4).
3. Run it. There is no installer and nothing is written to the registry.

Windows SmartScreen may warn about an unknown publisher the first time: the
program is not code-signed. Choose *More info → Run anyway* if you trust
the download.

**Through a launcher.** [RecompLauncher](https://recomplauncher.com) can
install the program for you: you pick your TFX folder once on the game's
page there, and Play starts `tfx.exe --game "your TFX folder"` (section 9),
so the folder question of section 3 does not appear. Your saves are in
`SAVE\` in the folder the launcher installed the program into. The launcher
does not contain or download the game.

## 3. The first start

1. **Choose your TFX folder.** The program asks for the folder of your
   installed TFX (it skips the question when `tfx.exe` sits in that folder
   itself). It checks the files and tells you if the folder is not the
   supported version:
   - *incomplete* (a required file is missing): choose another folder;
   - *another edition* (the files differ): you may start anyway, at your
     own risk, or choose another folder.
2. **Choose your CD image (optional).** If there is no `.cue` sheet in the
   game folder, the program asks for one. Pick the TFX CD's `.cue`, or
   *Cancel* to play without the CD music.
3. **One-time copies.** The program packs the game's files into
   `GAME\TFX.ZIP` (under a second) and converts the CD image into
   `GAME\TFX.CHD` (a progress window shows this; about 20 seconds on a
   modern PC). The CHD is about 30 % smaller than the BIN.
4. The game starts with its intro.

From then on, the program starts straight from `GAME\`; you no longer need
the original folder or the CD image (keep them anyway as your originals).

## 4. Where things are kept

Beside `tfx.exe`:

| Folder / file | Contents |
| --- | --- |
| `GAME\TFX.ZIP` | the game's files, copied from your TFX folder |
| `GAME\TFX.CHD` | your CD image, converted (a standard MAME CHD, readable by other tools) |
| `SAVE\` | everything the game writes: pilots (`TFXSAV.*`), high scores, recordings, `TFX.CFG` |
| `tfx.ini` | the folder you chose last |

If the program's folder cannot be written (for example under
`C:\Program Files`), the same folders are used in `%LOCALAPPDATA%\TFX`
instead.

To **move** the program to another PC, copy its whole folder: `GAME` and
`SAVE` go with it. To **redo** the copies (for example after changing your
TFX installation), start it once with `--game "your TFX folder"` (and
`--cd "your image.cue"`); it packs again only when the files differ.

## 5. Keys the program adds

| Key | Action |
| --- | --- |
| **Alt+Enter** | fullscreen on / off |
| **Ctrl+Shift+G** | enhanced 3D on / off (a note at the top right names the mode for 2 seconds) |
| **Ctrl+Shift+C** | the settings, over the paused game |
| **Alt+F4** | quit (as closing the window) |
| click the title bar's icon (or right-click the title bar) | the window menu, with **About…** |

Every other key goes to the game as on a PC keyboard under DOS, including
Alt and F10 (so Alt+Space does not open the window menu), the keypad and Pause. The mouse and the first joystick
(Windows game controller) work as the DOS mouse and the PC game port's
joystick. When the window loses focus, all held keys are released.

## 6. The picture

- The game's picture is the original 320 × 200. By default it is shown at
  the 4:3 shape of a VGA monitor, as on a PC in the 1990s, so its pixels are
  slightly taller than wide. Some artwork (for example the spinning Digital
  Image Design logo in the intro) was drawn for square pixels and looks
  taller than round; that is how it looked on a VGA monitor too.
  `--scale square` shows square pixels; `--scale integer` whole-number
  zoom only; `--scale stretch` fills the window.
- **Enhanced 3D** draws the 3D world (terrain, aircraft, objects) at the
  window's own resolution. The cockpit, HUD, menus and pictures stay the
  original's. Turn it on with Ctrl+Shift+G, or start with `--enhanced`.

## 7. Settings (Ctrl+Shift+C)

The settings window replaces the DOS setup program `CONFIG.EXE`, with its
choices and rules. Open it from the game with **Ctrl+Shift+C** (the game
pauses and goes quiet), or start the program with `--settings` to open it
alone.

| Row | Choices |
| --- | --- |
| CONTROL | KEYBOARD, JOYSTICK |
| DETAIL | LOW, MEDIUM, HIGH |
| MODEL | ARCADE, SIMPLE, REALISTIC, MILITARY SPEC |
| SOUNDFX | OFF, SOUND BLASTER, LAPC1, SB16 (QSOUND/CSP), GUS |
| MUSIC | OFF; **SB/CD** (FM music, and the CD's tracks in flight); **LAPC1** (Roland MT-32 / LAPC-I); **ADLIB**; **SCC1** (Roland SCC-1 / General Standard) |
| STICK | NORMAL, FLIGHTSTICK PRO, THRUSTMASTER |
| LANG | ENGLISH, FRANCAIS, DEUTSCH, ITALIA, ESPANA |

The buttons lead to the joystick calibration, the Sound Blaster settings,
a system examination and the game's credits. **SAVE** stores your choices in
`SAVE\TFX.CFG` and restarts the game with them; **QUIT** returns to the game
unchanged.

**Roland music** (LAPC1 or SCC1) is sent as MIDI to a Windows MIDI device:
by default Windows' own *Microsoft GS Wavetable Synth*. For the real MT-32
sound, connect an MT-32 or run an MT-32 emulator that provides a MIDI
device, and choose it with `--midi N` (N: the device's number, 0 the
first).

## 8. Light and dark mode

The program's windows (title bar, window menu, settings, message boxes,
About) follow Windows' light or dark app mode
(*Settings → Personalization → Colors*), and switch at once when you change
it. `--theme light` or `--theme dark` forces one. The game's picture is
never affected.

## 9. Command line

```
tfx.exe [options]
```

| Option | Meaning |
| --- | --- |
| `--game DIR` | your TFX folder (packs it into `GAME\TFX.ZIP` again when it differs) |
| `--cd SHEET.CUE` | your CD image (converts it into `GAME\TFX.CHD` again when it is another disc) |
| `--choose` | choose the TFX folder again now |
| `--store DIR` | keep the saves in DIR instead of `SAVE` |
| `--no-pack` | use the folder and CD image as given; leave `GAME` alone |
| `--check` | check a folder (`--game DIR`) and print its report: exit 0 supported, 1 another edition, 2 incomplete |
| `--settings` | open the settings alone, without the game |
| `--fullscreen` | start fullscreen |
| `--scale aspect\|integer\|stretch\|square` | the picture's shape (default `aspect`, 4:3) |
| `--enhanced` | start with enhanced 3D |
| `--theme system\|light\|dark` | the windows' colour mode (default: Windows') |
| `--midi N` | the MIDI output device for the Roland music |
| `--no-audio` | no sound |
| `--import FILE` | put a file the game writes (a pilot, `TFX.CFG`, a recording, the high scores) into the saves, after checking the game would read it; the game does not start |
| `--export DIR` | copy the saves to DIR; the game does not start |
| `--log FILE` | write a log of the run (useful for problem reports) |

Further options for testing and automation (`--auto`, `--hidden`,
`--virtual-clock`, `--frame-log`, `--sound-log`, `--wav`, `--trace-files`,
`--pack`) are described at the top of `src/win/main.c`.

## 10. Troubleshooting

- **"The folder is not the supported baseline"**: the report lists each
  file and how it differs. Run `tfx.exe --check --game "your folder"` from
  a command prompt to see it as text.
- **No CD music**: the MUSIC row must be **SB/CD**, and `GAME\TFX.CHD` must
  exist (start once with `--cd "your image.cue"`). The CD tracks play in
  flight, as in the original.
- **No Roland music**: check that a MIDI output device exists; `--log`
  writes "no MIDI output opens" when none does.
- **Everything else**: start with `--log tfx-log.txt` and include the log
  when reporting a problem.

## 11. About and licence

The window menu's *About…* shows the version, the project link and the licence.
TFX Enhanced's own code is under the MIT licence. TFX, its program and its
data belong to their owners and are not part of this software.
