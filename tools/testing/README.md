# Testing tools

Checks here work by replaying recorded pad input and comparing frame dumps. `launch_game.bat` and
the launcher record a new file in `input_recordings\` on every run. Recordings stay on this PC: `input_recordings/` is
ignored by git and must never be committed.

| Tool | What it does |
| --- | --- |
| `check.ps1` | The regression check: options table, build, replay, compare with a baseline kept on this PC |
| `replay.ps1` | Runs the game on a recording for a fixed time, with logs and optional frame dumps |
| `frames.py` | Contact sheets, frame-by-frame diffs of two runs, 4:3 against 16:9 comparisons |
| `gif_capture.py` | Lists what a `PS2X_GIF_CAPTURE` file draws: GIF tags, depths, boxes on screen |
| `sampler.cpp` | Sampling profiler for the running game (Windows) |
| `clean_test_pc.ps1` | Lists, and with `-Clean` removes, what could hide a missing file on a PC that tests a release zip |

The Python tools need Pillow (`pip install pillow`).

## A clean PC for a release zip

A release installs nothing into Windows, so a PC that tests one should have no build tools of its own:
anything there can stand in for a file the zip is missing. Copy `clean_test_pc.ps1` and
`clean_test_pc.bat` to the test PC, never to a PC you build on.

- `powershell -ExecutionPolicy Bypass -File clean_test_pc.ps1` only lists: Visual Studio and its Build
  Tools, Windows SDKs, the v14 Visual C++ runtime, Python, Git, CMake, Ninja, LLVM, MinGW, Qt, build
  programs on `PATH`, `PS2X_*` variables and extracted releases in Downloads, Desktop and Documents
  (more with `-SearchFolder`).
- `clean_test_pc.bat` (or `-Clean`) asks for administrator rights, lists the same, then asks before
  removing each one. Programs are uninstalled with their own uninstallers; folders, including old
  releases and the saves in them, go to the Recycle Bin. Older Visual C++ runtimes are left alone,
  because other programs use them and the release doesn't.
- Restart Windows afterwards and list again: `PATH` changes only show once Windows has restarted.

## The regression check

On a build you trust, save a baseline once: `tools\testing\check.ps1 -SaveBaseline`. After a change,
`tools\testing\check.ps1` rebuilds, replays the sky mission in 16:9 and fails if any frame differs.

## A before/after check

```
tools\testing\replay.ps1 -Recording input_recordings\input_20261109_161215.txt -Seconds 132 -OutDir out\test\before -DumpEvery 120
```

Change the code, rebuild, run again into `out\test\after`, then:

```
python tools\testing\frames.py diff out\test\before\frames out\test\after\frames
```

```
python tools\testing\frames.py sheet out\test\after\frames out\test\after.png
```

Recordings these checks have used, kept locally (record your own if you don't have them):

| File | Covers |
| --- | --- |
| `input_20261109_161215.txt` | Chapter 1 Verse 9 sky mission, from the save in `iso_extracted\mc0` (about 125 s) |
| `input_20261110_203136.txt` | Played in 16:9: opening movie, a ground mission, the R3 menu, Status (about 240 s) |
| `input_20261101_014311.txt` | Mission 1, a long fight (the performance benchmark) |

Things that make a replay leave its path:

- A different renderer, `PS2X_TRACE_FN`, or frame dumps every few vsyncs.
- The PS2's clock. The game seeds its random numbers from it (the length of the logo screens depends
  on that), so the clock runs on game time and a recording stores its start as a `# clock` line. A
  recording without one gets the time in its file name, so keep the `input_YYYYMMDD_HHMMSS` name.
- After a movie the GPU renderer is a new instance, so frames can differ from older runs by a few
  pixels while following the same path.

## Profiling

Build the sampler once from a Visual Studio developer prompt:

```
cl /O2 /EHsc tools\testing\sampler.cpp /Fe:out\sampler.exe
```

Then, with the game running: `out\sampler.exe <pid> <seconds> <interval ms> <report.txt>`. The
runner needs symbols: configure it with
`-DCMAKE_EXE_LINKER_FLAGS_RELEASE="/INCREMENTAL:NO /DEBUG /OPT:REF /OPT:ICF"`. Odd names inside hot
code (`basic_ostream::sentry::~sentry` and the like) are identical functions the linker folded.
Check that `PS2X_ENABLE_AGRESSIVE_LOGS` is OFF before measuring anything.
