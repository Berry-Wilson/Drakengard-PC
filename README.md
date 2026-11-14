# Drakengard-PC

A work-in-progress native PC port of **Drakengard** (PS2, USA, SLUS-20732), built by
statically recompiling the original game executable with
[PS2Recomp](https://github.com/ran-j/PS2Recomp).

This repository contains **no game data**. You need your own legally dumped copy of the game.

## Layout

| Path | What it is |
| --- | --- |
| `tools/` | ELF analysis, recomp config generation, VU0/VU1 microcode translation, source packing |
| `tools/testing/` | Replay runs, frame comparison, GIF capture reader, profiler (see its README) |
| `docs/` | Technical notes, such as the movie corruption investigation |
| `input_recordings/` | Recorded pad input, replayed for testing |
| `recomp/` | `recompile.txt`, the library functions to translate. PS2Recomp's config and function map are generated here from your disc |
| `src/` | Game-specific runtime overrides (translated VU programs are generated here at build time) |
| `launcher/` | Qt desktop launcher for the recompiled game (see its README) |
| `external/` | Submodule: a fork of PS2Recomp, with Drakengard fixes |

## Playing (Windows)

Download the release zip, unzip it to a short path, and run `drakengard-launcher.exe`. It asks for your
disc image and builds the game on your PC from it. See [launcher/README.md](launcher/README.md).

## Building (Windows)

1. Clone with the submodules:
   ```
   git clone --recursive https://github.com/Berry-Wilson/Drakengard-PC.git
   ```
2. Extract the game disc so that `iso_extracted/SLUS_207.32` exists.
3. Configure the build:
   ```
   cmake -S external/PS2Recomp -B external/PS2Recomp/out/build -G "Visual Studio 18 2026" -DPS2X_GAME_RECOMP_DIR=recomp/batched -DPS2X_GAME_SRC_DIR=src
   ```
4. Recompile and build:
   ```
   powershell -File build_recomp.ps1
   ```
   The runner ends up at `external/PS2Recomp/out/build/ps2xRuntime/Release/ps2EntryRunner.exe`.
5. Build the renderer, PCSX2's GS as `ps2x-gs.dll`, into the runner's `pcsx2-gs` folder. It is built
   from a PCSX2 source tree; see
   [external/PS2Recomp/ps2xRuntime/pcsx2-gs/README.md](external/PS2Recomp/ps2xRuntime/pcsx2-gs/README.md).
   The release zip has it built already (`tools/make_release.py` packs the one from this step).

## Options

The launcher's tabs cover the common options. All of them, with their values and defaults, are in
[docs/settings.md](docs/settings.md). Each is an environment variable; without the launcher, put
`NAME=value` lines in `ps2x.ini` in the game's folder, for example:

```
PS2X_ASPECT=16:9
PS2X_PROGRESSIVE=1
PS2X_PCSX2_UPSCALE=4
```

While playing: F1 shows PCSX2's statistics, F3 shows the game's speed (VSync/s, 60 is full speed), F5
steps the internal resolution, F12 saves a screenshot to `screenshots\` in the
game's folder.

## Troubleshooting

| Problem | What to do |
| --- | --- |
| The window opens but stays black, or the log says `PCSX2's GS did not start` | The renderer DLL, its DLLs or PCSX2's `resources` folder are missing from `pcsx2-gs\` next to the runner (build step 5). Try another graphics API with `PS2X_PCSX2_RENDERER=dx11`. |
| The game runs below 60 VSync/s (F3 shows the counter in the top-left corner) | Lower the internal resolution with F5. Check the runner was built as Release. |
| A movie stutters when it starts or ends | Movies switch to PCSX2's software renderer and back, see [docs/movie-corruption.md](docs/movie-corruption.md). |
| Something on screen is stretched or misplaced in 16:9 | 16:9 is experimental. Press F12 and report it with the screenshot; 4:3 is unaffected. |
| The game closes with no message | Run `launch_game.bat` and look at `launch_console.log`. |

For checking a change, see [tools/testing/README.md](tools/testing/README.md).

## Credits

- **Berry**: project author.
- **[Claude](https://claude.com/claude-code)** (Anthropic): AI pair programmer. Claude helped write
  and debug this project, including tooling, runtime fixes and repository setup.
  Commits it worked on carry a `Co-Authored-By: Claude` trailer.
- [PS2Recomp](https://github.com/ran-j/PS2Recomp) by ran-j and contributors.
- [PCSX2](https://github.com/PCSX2/pcsx2) by the PCSX2 team: its GS draws the game.

## License

GPL-3.0. See [LICENSE](LICENSE).
