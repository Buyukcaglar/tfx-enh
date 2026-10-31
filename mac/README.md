# The macOS host (M stage)

A native, universal (arm64 + x86_64) macOS application over the portable
core (`include/tfx/core.h`). It needs the user's own original game folder.
The bundle contains no original resources and no icon derived from them.

## Parts

| File | What it is |
| --- | --- |
| `files.c` | DOS paths on the user's game folder: case-insensitive per component, the `C:\TFX\` prefix and drives cut; reads look in the store first, then the originals (never written); writes go to the store (`.part`, fsync, rename; the replaced version kept in `.history`, M07); deterministic searches |
| `keymap.c` | macOS virtual key codes to set-1 scan codes (with the `0xe0` prefix) |
| `mixer.c` | the core's sample voices and music port writes played at their stamps, the CD added, into a float stereo stream |
| `opl2.c` | an OPL2 (YM3812) synthesizer at the chip's 49716 Hz (M05) |
| `midi.c` | the MPU-401's data bytes as MIDI messages (M05) |
| `music.c` | the music devices' writes at their frames: the OPL2 resampled, MIDI to a synthesizer with buffer offsets (M05) |
| `cdaudio.c` | the CD's audio from the user's CUE / BIN image, checked against the release's table of contents (M05) |
| `output.c` | Core Audio default output (48 kHz float stereo) pulling from the mixer; Apple's DLS synthesizer for the MPU-401's music |
| `session.c` | the core's host callbacks over the parts above; `TFX.EXE cd_demo`, `DID=C:\TFX`; the sound devices TFX.CFG selects, described |
| `profile.c` | the content profile (M02): the supported release (Ocean CD, 1995) by its files' sizes and SHA-256, one precise diagnostic per failure |
| `present.c` | the frame's placement (M03): 4:3 fit or whole multiples (Integer Scaling), the DAC's 6 to 8 bits |
| `gamepad.c` | a game controller as the game port's joystick (M04): sticks, trigger, d-pad and buttons as the one-shots' counts |
| `settings.c` | the settings (M06): TFX.CFG through the recovered CONFIG (defaults, System Check's rules on native measurements, its writer into the store), CONFIG's joystick read over the controller, About's text from the user's CONFIG.EXE, the sound devices' descriptions |
| `userdata.c` | the user's data (M07): the game's writable files checked with the recovered formats, imported from and exported to folders, previous versions restored |
| `settings_ui.m` | the settings window (M06, TFX > Settings…, Command-comma): CONFIG's main screen, System Check, the joystick calibration screens on the controller, the Sound Blaster's fields, About, the boot disk explained, Export, Cancel, Save |
| `app.m` | AppKit: the game folder and its diagnostics, window and layer (4:3 letterbox, nearest filtering), keys, mouse, the controller, menus, full screen, the enhanced 3D mode (Control-Shift-G), the support information, the log, the 1 ms pump |
| `headless.c` | `tfx_headless`: the session on a virtual clock, with a hash of every frame (two slices or machines giving the same hashes run the game the same), input at frames or milliseconds (M08) |
| `Info.plist.in` | the bundle's property list (identifier `local.tfx.native`, macOS 11 or later) |

`tests/test_mac.c` (ctest `mac`) tests files, keys and mixer timing on
synthetic folders, the profile's diagnostics (and, with
`TFX_ORIGINAL_DIR`, the real installation), the frame's placement and
the controller's mapping; `tests/test_music.c` (ctest `music`) the OPL2's
thresholds, the music's scheduling, the mixer's stamps and the CD image;
`tests/test_settings.c` (ctest `settings`) TFX.CFG's load, first run,
save and reload, and a controller calibrated end to end;
`tests/test_userdata.c` (ctest `userdata`) the saved files' checks,
versions, restore, import and export.

## Building

Pinned tools (M01, 30 October 2026):

| Tool | Version |
| --- | --- |
| macOS (build machine) | 26.6.2, arm64 |
| Command Line Tools | 27.0.0 (`/Library/Developer/CommandLineTools`) |
| macOS SDK | 27.0 |
| Compiler | Apple clang 21.0.0 (clang-2100.3.34.2) |
| CMake | 4.4.4 |
| Deployment target | macOS 11.0 |

```sh
cmake -S . -B private/build/mac -DCMAKE_BUILD_TYPE=Release \
  "-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64" -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0 -DTFX_MAC_APP=ON
cmake --build private/build/mac -j4
```

This gives `private/build/mac/TFX.app`, ad hoc signed (`codesign --sign -`
after the build), plus `tfx_headless`, `test_mac` and the core's tests,
all universal. Without `-DTFX_MAC_APP=ON` only the portable host parts
and `tfx_headless` are built.

Checks:

```sh
lipo -archs private/build/mac/TFX.app/Contents/MacOS/TFX     # x86_64 arm64
codesign -dv private/build/mac/TFX.app                         # Signature=adhoc
plutil -lint private/build/mac/TFX.app/Contents/Info.plist
```

## The package (M12)

```sh
tools/mac/m12/package.sh private/release/<name> [commit]
```

This builds `TFX.app` from a commit's sources alone and puts the
package in `private/release/<name>`:

- The sources are the commit's `CMakeLists.txt`, `include/`, `src/`,
  `mac/` and `tests/`, taken by `git archive`. Nothing comes from
  `reference/`, `private/` or the analysis.
- The build follows the configuration above, without the tests, with
  the source path mapped out of the objects.
- The script checks both slices, the ad hoc signature, the property
  list and the notices.

It writes:

- `TFX-<version>-<commit>.zip` (made by `ditto`, which keeps the
  signature);
- the source archive `TFX-<version>-<commit>-source.tar.gz`;
- their SHA-256 sums;
- a manifest of the bundle's files.

The same commit gives the same bundle and the same zip, byte for byte,
in any directory: compare two runs' outputs. The bundle holds the executable, its
Info.plist, `Resources/NOTICES.txt` (what it holds of the original, the
content's owners, no third-party software, the system frameworks used) and
`Resources/ReadMe.txt` (the user's notes).

`tools/mac/m12/audit.py <zip> <source.tar.gz> <out.json>` audits a
package against the originals; `tools/mac/m12/literals.py <out.json>`
lists the sources' string literals the originals also hold;
`tools/mac/m12/clean_run.sh <zip> <game folder> <work dir>` runs the
package on both slices with a fresh home (analysis/m12/README.md).

Notarization needs the user's Developer ID; it is not done here. With
one, sign the bundle with the hardened runtime and a timestamp, submit
it, then staple the ticket. TFX needs no entitlements: it loads no
plug-ins and no third-party libraries.

```sh
codesign --force --deep --options runtime --timestamp \
  --sign "Developer ID Application: <name> (<team>)" TFX.app
ditto -c -k --keepParent TFX.app TFX.zip
xcrun notarytool submit TFX.zip --keychain-profile <profile> --wait
xcrun stapler staple TFX.app
spctl --assess --type execute -vv TFX.app
```

## Running

`TFX.app` finds the game folder in `TFX_ORIGINAL_DIR`, then in the saved
choice, otherwise it asks for a folder (one containing `DID.DAT`,
`TFX.DAT` and `START.EXE`); each must be the supported release, and a
failure names the reason and offers another folder (File > Choose Game
Folder…, Command-O, at any time). The game's writes (configuration,
pilots, saves) go to `~/Library/Application Support/TFX/Game`, the log
to `TFX/Logs/TFX.log` (the previous run's in `TFX.previous.log`; it records the folder's check, the start, the first frame shown and, at the quit, the frames shown: a termination signal quits as Quit does); the
game folder is only read. TFX > Support Information shows the version,
architecture, macOS, the folder and its profile, with a Copy button.
The first connected game controller is the joystick. The CD's music
comes from the user's own image of TFX's CD: `TFX_CD_IMAGE=<cue sheet>`
or File > Choose CD Image… (File > Play Without CD: none, as a machine
without a CD-ROM drive). TFX > Settings… (Command-comma) is CONFIG: the
game pauses while it is open, and saving writes TFX.CFG to the store
(never the game folder) and restarts the game on it. File > Import
Saved Data… / Export Saved Data… / Restore a Previous Version… handle
the game's saved files (M07; the last three replaced versions of each are
kept in the store's `.history`). A store that cannot be written stops
the start with the reason (M08), before the game would find it at its
next save. The mouse's motion steers the ARCADE and mouse-controlled
flight; View > Capture the Mouse (Control-Command-M) hides and holds the
cursor so the steering does not stop at the window's edge (M09).
Control-Shift-G (View > Enhanced 3D; `--enhanced` starts in it) toggles
the enhanced 3D mode, which is not the original's: the shapes and the
sky drawn again at the frame rectangle's own size in backing pixels
(`render/hires.h`, `tfx_enhanced` / `tfx_frame_enhanced`, as the Windows
host's Ctrl+Shift+G), drawn 1:1, everything else the original's pixels
scaled up; a note names the mode for 2 s at the picture's top right. The
G is the host's (not passed to the game); Control and Shift still are.

`tfx_headless` checks the profile first (exit status 3 with the
diagnostic; a store it cannot write: the same). `TFX_HEADLESS_PRESENT=n` / `TFX_HEADLESS_PUMP=1` exercise the
app's display conversion and pump (neither may change a hash),
`TFX_HEADLESS_STICK="x y z w buttons"` connects a joystick,
`TFX_HEADLESS_ENHANCED=<w>x<h>` turns the enhanced 3D mode on and hashes
its pictures apart (the frames' hashes must not change; with
`TFX_HEADLESS_DUMP` each is written beside its frame as `<name>_hi.ppm`),
`TFX_HEADLESS_AUDIO=1` renders and hashes the sound on the virtual clock
(`TFX_HEADLESS_WAV=<file>` writes it), `TFX_HEADLESS_CUE=<cue sheet>`
gives the machine a CD. Keys are given as separate arguments
(`400:39,b9 700:1c,9c`) at frame numbers, or on the virtual clock for
the menus, which show no frames while they wait (M08): `@31000:1c,9c`
types at 31 s, `m48000:400,400,0` moves the mouse (x 0 .. 639, y
0 .. 199, buttons), `d56000:` only writes the screen;
`v61000:8,0` moves it by mickeys (the motion the flight steers by,
M09); `TFX_HEADLESS_MS=<ms>` ends the run there, and with
`TFX_HEADLESS_DUMP=<folder>` the screen is written before each timed
input (`at_<ms>.ppm`) and at the end (`last.ppm`).

```sh
private/build/mac/tfx_headless <game folder> <scratch store> 3000              # arm64
arch -x86_64 private/build/mac/tfx_headless <game folder> <scratch store> 3000 # the Intel slice
```

## Both architectures compute the same game

ctest's own binary is arm64-only, so the x86_64 slice runs each test
command under `arch -x86_64` (Rosetta): `python3 tools/mac/x86_ctest.py
private/build/mac`. The core's x87 transcendental
functions are portable C (`src/core/x87math.c`), not the platform's libm,
because Apple's arm64 and x86_64 libm differ in the last place; see
[analysis/m01](../analysis/m01/README.md).
