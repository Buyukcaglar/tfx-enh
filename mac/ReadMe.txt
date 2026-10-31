TFX for macOS

Requirements: macOS 11 or later, on Apple silicon or Intel (one universal
application), and your own copy of TFX: the Ocean CD edition (1995),
installed. TFX.app contains none of the game's files.

First start
- This build is signed ad hoc, not by a registered developer. The first
  time, Control-click TFX.app and choose Open (or allow it in System
  Settings > Privacy & Security > Open Anyway).
- TFX asks for the game folder: the installed TFX folder, the one with
  DID.DAT, TFX.DAT and START.EXE. It checks that it is the supported
  release and says precisely what is wrong if it is not. The folder is
  only read, never changed. File > Choose Game Folder (Command-O) picks
  another at any time.

Your data
- The game's writes (settings, pilots, saved games, high scores,
  recordings) go to ~/Library/Application Support/TFX/Game; the log to
  ~/Library/Application Support/TFX/Logs.
- File > Import Saved Data, Export Saved Data and Restore a Previous
  Version handle the saved files (the last three versions of each are
  kept).

Music and settings
- TFX > Settings (Command-comma) is the game's CONFIG: sound devices,
  controls, detail. Saving restarts the game with them.
- The CD's music plays from your own image of the TFX CD (a CUE sheet
  with its BIN): File > Choose CD Image. Without one the game runs as on
  a machine without a CD-ROM drive.
- A connected game controller is the joystick.

Enhanced 3D
- Control-Shift-G (View > Enhanced 3D) draws the 3D world at the
  window's full resolution instead of the original's 320 x 200; the rest
  of the picture stays the original's. It is an addition, not part of the
  1995 game, and is off by default. Press it again for the original.

TFX > Support Information shows the version, the architecture, macOS and
the game folder's check, with a Copy button.
