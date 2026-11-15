# Drakengard Launcher

A small Qt 6 desktop launcher for the recompiled game. On first run it sets the game up from the
player's disc image. After that it starts `ps2EntryRunner.exe` with the game ELF and the `PS2X_*`
options you pick, and shows the game's output.

## First-run setup

If the runner or the game ELF is missing, the launcher opens its setup page. The player picks an
`.iso`, and the launcher runs [`tools/setup_game.py`](../tools/setup_game.py), which also works by
hand:

1. Checks the disc: `SLUS_207.32` must match the supported version's SHA-256.
2. Extracts the disc to `iso_extracted/`.
3. Finds Visual Studio 2022 or 2026 with the C++ tools, or, if the player agrees, installs
   Microsoft's C++ Build Tools (about 6 GB), then configures CMake.
4. Builds the recompiler.
5. Translates the game's code to C++ in `recomp/` and `src/`.
6. Compiles the runner.

A finished step is skipped when setup runs again, so Stop can be followed by Continue. The log is
in `setup.log`. Apart from the launcher's icon, everything derived from the
game is made on the player's PC.

## Making the player's zip

```bash
python tools/make_release.py
```

This writes `out/dist/DrakengardPC-windows.zip`. It holds the launcher, Python's embeddable
package, the sources setup builds and their dependencies, so the player needs neither git nor
Python. The FFmpeg DLLs are downloaded during setup.

- The script refuses to write the zip if any file looks like game data or translated game code.
- `external/PS2Recomp` is taken at its `HEAD` commit, so uncommitted runtime changes aren't included.
- `--allow-dirty` packages uncommitted project files, which is only for testing.
- The launcher's icon is the game's crest, the one piece of the game's art in the zip. The script
  refuses to run if the icon file is missing or newer than the launcher build.

## Build

You need CMake, MSVC, and Qt 6.5 or newer with qtbase. To install prebuilt Qt with
[aqtinstall](https://github.com/miurahr/aqtinstall):

```bash
pip install aqtinstall
```

```bash
aqt install-qt windows desktop 6.10.3 win64_msvc2022_64 --archives qtbase --outputdir C:/Qt
```

From the project root:

```bash
cmake -S launcher -B launcher/out/build -DCMAKE_PREFIX_PATH=C:/Qt/6.10.3/msvc2022_64
```

```bash
cmake --build launcher/out/build --config Release
```

To use the game's crest as the icon, run this before configuring (it needs Pillow, and a save
folder in `iso_extracted/mc0`). Git ignores the `.ico`, so make it once on each checkout. Without it
the launcher builds with Windows' default icon:

```bash
python tools/make_launcher_icon.py
```

The build copies the Qt DLLs next to the exe, so it runs without Qt on `PATH`. It never builds the
runner: build that separately with the `ps2EntryRunner` target.

## Run

```bash
launcher/out/build/Release/drakengard-launcher.exe
```

Press **Play**. **Stop** asks the game window to close and force-kills it after 3 seconds.

The runner and game paths are detected when the launcher sits under the project; otherwise select
them on the Game tab. The game path is `SLUS_207.32` in the extracted ISO, because the runner does
not read `.iso` files. It runs from that folder, and the `mc0` memory card is found next to it.

## Options

| Tab | Controls |
| --- | --- |
| Game | Runner and game paths, controls file, saves folder, VIF1/GIF worker thread, pad input recording or replay |
| Controls | Keyboard and controller bindings for every PS2 button and stick direction, and which controller stick drives each PS2 stick |
| Video | Aspect ratio, window size, internal resolution, full-height picture, draw distance, sharpening, renderer, movie renderer |
| Picture | PCSX2's blending, texture filtering, trilinear, mipmapping, dithering and upscaling fixes |
| Paths | Renderer DLL, PCSX2 resources folder, PCSX2 log, game serial |
| Scripted | Scripted button presses and a frame limit, for test runs |
| Log | Game output and the console log file's path |

The game is drawn by PCSX2's GS. F5 steps the internal resolution from 1x to 8x while playing and
shows the new one in the top-left corner; enlarge the window to see the extra detail. F12 saves a
screenshot to `screenshots\` in the game's folder. Every option is described in
[`docs/settings.md`](../docs/settings.md).

### Controls

Click a slot, then press a key, or a controller button, trigger or stick direction. Right-click a
slot to clear it, and press Escape to cancel. Each PS2 input takes up to two keys and two controller
inputs, and every controller connected counts. F1, F5 and F12 are kept for the game. If a controller
was connected after the launcher started, press **Find controllers**.

Changes are saved at once to `controls.ini` in the game's folder, or to the file named on the Game
tab, a readable text file. The game reads the same file, and otherwise uses the defaults shown on
the tab. The launcher reads controllers with GLFW 3.4, the same library and version as the game, so
captured buttons mean the same thing in both.

## Where options are kept

The launcher keeps no settings of its own. Each option on the Video, Picture, Paths and Scripted
tabs, and the controls file, saves folder and worker thread on the Game tab, is a `NAME=value` line
in `ps2x.ini` in the game's folder (the folder of the game ELF, `iso_extracted\`). It is written as
soon as an option changes, and only options that differ from the game's default get a line. The
game reads the file itself, so the options also apply when it is started without the launcher.
Lines the launcher doesn't know, and comments, are left as they are.

- The runner and game paths, pad input recording or replay, and the console log path last until
  the launcher closes. Pad input starts off; a recording goes to a new file in `input_recordings\`.
- `PS2X_*` variables the launcher inherited are removed before the game starts, since they would
  win over `ps2x.ini`. The `[launcher]` lines at the top of the log show the command and the
  variables it set; the game's `[config]` line says how many options it read from `ps2x.ini`.
- When the game's folder has no `controls.ini` yet, the launcher copies one from where earlier
  launchers kept it: the project folder, or `%APPDATA%\Drakengard`.
- The log view keeps the last 5000 lines. The full output goes to the console log file
  (`launch_console.log` by default), which is rewritten on each start.
- The game's stdout is buffered when it is not a console, so its lines can appear late or out of
  order against stderr.
