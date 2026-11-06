# Drakengard-PC

A work-in-progress native PC port of **Drakengard** (PS2, USA, SLUS-20732), built by
statically recompiling the original game executable with
[PS2Recomp](https://github.com/ran-j/PS2Recomp).

This repository contains **no game data**. You need your own legally dumped copy of the game.

## Layout

| Path | What it is |
| --- | --- |
| `tools/` | ELF analysis, recomp config generation, VU0/VU1 microcode translation, source packing |
| `recomp/` | PS2Recomp configs and the function map (`functions.csv`) |
| `src/` | Game-specific runtime overrides (translated VU programs are generated here at build time) |
| `analysis/` | ELF analysis and asset catalog output |
| `launcher/` | Qt desktop launcher for the recompiled game (see its README) |
| `external/` | Submodules: forks of PS2Recomp and parallel-gs, with Drakengard fixes |

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
   cmake -S external/PS2Recomp -B external/PS2Recomp/out/build -G "Visual Studio 18 2026"
   ```
4. Recompile and build:
   ```
   powershell -File build_recomp.ps1
   ```
   The runner ends up at `external/PS2Recomp/out/build/ps2xRuntime/Release/ps2EntryRunner.exe`.

## Credits

- **Berry**: project author.
- **[Claude](https://claude.com/claude-code)** (Anthropic): AI pair programmer. Claude helped write
  and debug this project, including tooling, runtime fixes and repository setup.
  Commits it worked on carry a `Co-Authored-By: Claude` trailer.
- [PS2Recomp](https://github.com/ran-j/PS2Recomp) by ran-j and contributors.
- [parallel-gs](https://github.com/Arntzen-software/parallel-gs) by Arntzen Software.

## License

GPL-3.0. See [LICENSE](LICENSE).
