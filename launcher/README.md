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
3. Downloads its build tools into `toolchain/`, unless they are there already: llvm-mingw (clang),
   CMake and Ninja, about 234 MB, each pinned to a tested version and checked against its SHA-256.
   Nothing is installed in Windows and no administrator rights are needed. Then configures CMake
   into `external/PS2Recomp/out/build-game`.
4. Builds the recompiler.
5. Translates the game's code to C++ in `recomp/` and `src/`.
6. Compiles the runner and puts the renderer (`pcsx2-gs`) beside it.

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

The launcher is built with the same tools setup builds the game with: llvm-mingw, CMake and Ninja,
downloaded into `toolchain/`. It needs Qt 6.5 or newer built for llvm-mingw, with qtbase. From the
project root, get the tools, then Qt with [aqtinstall](https://github.com/miurahr/aqtinstall)
(about 140 MB):

```bash
python tools/setup_game.py get-tools
```

```bash
pip install aqtinstall
```

```bash
aqt install-qt windows desktop 6.10.3 win64_llvm_mingw --archives qtbase --outputdir C:/Qt
```

Then build. Qt in another folder is passed with `--qt`, and `--fresh` configures from scratch:

```bash
python tools/build_launcher.py
```

The script puts the toolchain on `PATH`, because Qt's own tools such as `moc` are llvm-mingw programs
too, and builds in `launcher/out/build-mingw`. A Visual Studio build with Qt's `msvc2022_64` still
works for development, but the release script packages only the llvm-mingw build.

To use the game's crest as the icon, run this before configuring (it needs Pillow, and a save
folder in `iso_extracted/mc0`). Git ignores the `.ico`, so make it once on each checkout. Without it
the launcher builds with Windows' default icon:

```bash
python tools/make_launcher_icon.py
```

Setup gives the game window the same icon: it builds the runner with this `.ico`, or, in a release,
with the icon it copies out of `drakengard-launcher.exe`.

The build copies the Qt DLLs and llvm-mingw's `libc++.dll` and `libunwind.dll` next to the exe, so it
runs without Qt or the toolchain on `PATH`. It never builds the game: setup does that.

## Run

```bash
launcher/out/build-mingw/drakengard-launcher.exe
```

Press **Play**. **Stop** asks the game window to close and force-kills it after 3 seconds.

The launcher finds the runner and the game in the project it sits under: the game is
`iso_extracted/SLUS_207.32`, because the runner does not read `.iso` files. It runs from that
folder, and the `mc0` memory card is found next to it. If either is missing, setup opens.

## Options

| Tab | Controls |
| --- | --- |
| Controls | Keyboard and controller bindings for every PS2 button and stick direction |
| Video | Aspect ratio, window size, full screen, internal resolution, full-height picture, fog distance, sharpening, renderer |
| Picture | PCSX2's blending, texture filtering, trilinear, dithering and upscaling fixes |
| Log | Game output and the console log file's path |

The game is drawn by PCSX2's GS. F5 steps the internal resolution from 1x to 8x while playing and
shows the new one in the top-left corner; enlarge the window to see the extra detail. F12 saves a
screenshot to `screenshots\` in the game's folder. Every option is described in
[`tools/settings.json`](../tools/settings.json); those without a control on a tab, such as the saves
folder, pad recording or scripted presses, can be added to `ps2x.ini` by hand.

### Controls

The tab shows a controller with a box for each PS2 button, joined to it by a line; the stick
directions are grouped under each stick. Click a box, then press a key or a controller button (or
push a stick or trigger): whichever comes first is bound, and replaces that box's key or controller
input. Each box shows both. Right-click a box to clear its key, its controller input or both; Escape
or a second click cancels. Every controller connected counts. F1, F3, F5, F11 and F12 are kept for
the game. If a controller was connected after the launcher started, press **Find controllers**.

- **Bind all** goes through every PS2 button and stick direction in turn, lighting up each box and
  binding it to the next key or controller input pressed. **Skip** leaves one as it is; **Stop**,
  Escape, or clicking a box ends the run. Inputs bound so far are kept.
- **Unbind All** removes every key and controller binding at once, after asking. **Restore
  defaults** brings back the default controls.

The picture is `launcher/res/controller.png`, built into the exe as a Qt resource. The file format allows two keys and two controller inputs per PS2 input; a second one added to
`controls.ini` by hand shows in its box as "first / second", and binding the box replaces both.

Changes are saved at once to `controls.ini` in the game's folder, or to the file `PS2X_CONTROLS`
names in `ps2x.ini`, a readable text file. The game reads the same file, and otherwise uses the defaults shown on
the tab. The launcher reads controllers with GLFW 3.4, the same library and version as the game, so
captured buttons mean the same thing in both.

## Where options are kept

The launcher keeps no settings of its own. Each option on the Video and Picture tabs is a
`NAME=value` line in `ps2x.ini` in the game's folder (the folder of the game ELF, `iso_extracted\`). It is written as
soon as an option changes, and only options that differ from the game's default get a line. The
game reads the file itself, so the options also apply when it is started without the launcher.
Lines the launcher doesn't know, and comments, are left as they are.

- The console log path lasts until the launcher closes.
- `PS2X_*` variables the launcher inherited are removed before the game starts, since they would
  win over `ps2x.ini`. The `[launcher]` line at the top of the log shows the command; the game's `[config]` line says how many options it read from `ps2x.ini`.
- The log view keeps the last 5000 lines. The full output goes to the console log file
  (`launch_console.log` by default), which is rewritten on each start.
- The game's stdout is buffered when it is not a console, so its lines can appear late or out of
  order against stderr.
