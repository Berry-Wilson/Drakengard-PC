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
| Game | Paths, VIF1/GIF worker thread, pad input recording or replay |
| Controls | Keyboard and controller bindings for every PS2 button and stick direction, and which controller stick drives each PS2 stick |
| Video | Aspect ratio, window size, draw distance, GS backend |
| Developer | VU1/VU0 mode, verify delay, VIF1/VU statistics, VIF verify, frame dumps |
| Environment | Any other variable, such as `PS2X_AUTOPAD`; these override the other tabs |
| Log | Game output and the console log file's path |

The game is drawn by PCSX2's GS. Its settings have no controls of their own yet. Add these on the
Environment tab:

| Variable | Values | Effect |
| --- | --- | --- |
| `PS2X_PCSX2_UPSCALE` | 1 to 8, default 2 | Internal resolution, as a multiple of the PS2's |
| `PS2X_PROGRESSIVE` | 0 (default), 1 | Shows the game's full 480-line frame instead of its 224-line copy for TV |
| `PS2X_PCSX2_RENDERER` | `dx11`, `dx12`, `vulkan`, `opengl`, `software` | Graphics API; unset picks one |
| `PS2X_PCSX2_SHARPNESS` | 0 (default), 1, 2 | Sharpening: off, normal or strong |
| `PS2X_PCSX2_MOVIE_RENDERER` | unset (default), `hardware` | Movies use PCSX2's software renderer, as the GPU renderers garble them (see `docs/movie-corruption.md`); `hardware` keeps the GPU renderer |

F5 steps the internal resolution from 1x to 8x while playing and shows the new one in the top-left
corner. Enlarge the window to see the extra detail.

### Controls

Click a slot, then press a key, or a controller button, trigger or stick direction. Right-click a
slot to clear it, and press Escape to cancel. Each PS2 input takes up to two keys and two controller
inputs, and every controller connected counts. F1, F5 and F12 are kept for the game. If a controller
was connected after the launcher started, press **Find controllers**.

Changes are saved at once to `%APPDATA%\Drakengard\controls.ini`, a readable text file, and the game
gets its path in `PS2X_CONTROLS`. Without that variable the game reads `controls.ini` from its
working folder if there is one, and otherwise uses the defaults shown on the tab. The launcher reads
controllers with GLFW 3.4, the same library and version as the game, so captured buttons mean the
same thing in both.

- Only the options you change from their defaults are set. `PS2X_*` variables inherited from the
  shell are removed, so the window is the only source of options.
- Pad input is recorded by default, to a new file in `input_recordings\` for each run.
- The `[launcher]` lines at the top of the log show the exact command and variables used.
- The log view keeps the last 5000 lines. The full output goes to the console log file
  (`launch_console.log` by default), which is rewritten on each start.
- The game's stdout is buffered when it is not a console, so its lines can appear late or out of
  order against stderr.

## Settings

Settings are saved when you press Play and when you close the launcher, in
`%APPDATA%\Drakengard\Launcher.ini`. Delete that file to return to the defaults. The controls are
in `controls.ini` in the same folder; **Restore defaults** on the Controls tab resets them.
