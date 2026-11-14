# Options

Generated from `tools/settings.json` by `tools/check_settings.py --write`. Do not edit by hand.

Every option is an environment variable. The launcher sets the common ones from its tabs and
takes any other on its Environment tab. Without the launcher, put `NAME=value` lines in
`ps2x.ini` in the game's folder (`iso_extracted`), or set them in the shell; the environment
wins over the file.

## Video

| Option | Values | Default | Effect |
| --- | --- | --- | --- |
| `PS2X_ASPECT` | 16:9 | 4:3 | Widescreen. The 3D view shows more at the sides; menus stay a centred 4:3 layout. |
| `PS2X_WINDOW_SIZE` | WIDTHxHEIGHT, such as 1280x896 | 640x448 (796x448 in 16:9) | The game window's size when it opens. |
| `PS2X_PCSX2_UPSCALE` | 1 to 8 | 2 | Internal resolution, as a multiple of the PS2's. F5 steps through them while playing. |
| `PS2X_PROGRESSIVE` | 0, 1 | 0 | 1 shows the game's full 480-line frame instead of its 224-line copy for TVs. The biggest gain in detail. |
| `PS2X_DRAW_DISTANCE` | a multiplier above 1, such as 2 | original | Pushes the fog back in ground missions. Objects can appear suddenly and the level's edge may show. |
| `PS2X_PCSX2_RENDERER` | dx11, dx12, vulkan, opengl, software | PCSX2 picks | The graphics API PCSX2's renderer uses. |
| `PS2X_GS_BACKEND` | cpu, software | unset | Older name for PS2X_PCSX2_RENDERER=software. Any other value is ignored. |
| `PS2X_PCSX2_SHARPNESS` | 0, 1, 2 | 0 | Sharpening and anisotropic filtering: off, normal or strong. |
| `PS2X_PCSX2_MOVIE_RENDERER` | hardware | software during movies | Movies are drawn with PCSX2's software renderer, as its GPU renderers garble them (docs/movie-corruption.md). hardware keeps the GPU renderer. |
| `PS2X_PCSX2_OSD` | 0, 1 | 0 | 1 shows PCSX2's own statistics overlay. |

## PCSX2 picture settings

| Option | Values | Default | Effect |
| --- | --- | --- | --- |
| `PS2X_PCSX2_BLENDING` | 0 (minimum) to 5 (maximum) | the level PCSX2's game database recommends (2) | Blending accuracy. |
| `PS2X_PCSX2_TEXTURE_FILTER` | 0 nearest, 1 bilinear always, 2 as the PS2 asks, 3 bilinear except sprites | 2 | Texture filtering. |
| `PS2X_PCSX2_TRILINEAR` | 0 off, 1 as the PS2 asks, 2 forced | PCSX2's | Trilinear filtering. |
| `PS2X_PCSX2_MIPMAP` | 0, 1 | 1 | Mipmapping as the game sets it up, or off. The game uses none. |
| `PS2X_PCSX2_DITHERING` | 0 off, 1 scaled, 2 unscaled, 3 forced 32-bit | 2 | Dithering. |
| `PS2X_PCSX2_HALF_PIXEL` | PCSX2's half-pixel offset modes, from 0 | 2, from the game database | Upscaling fix; only matters above 1x. |
| `PS2X_PCSX2_ROUND_SPRITE` | 0, 1, 2 | 0 | Upscaling fix; only matters above 1x. |
| `PS2X_PCSX2_NATIVE_SCALING` | PCSX2's native scaling modes, from 0 | 0 | Upscaling fix; only matters above 1x. |
| `PS2X_PCSX2_ALIGN_SPRITE` | 0, 1 | 0 | Upscaling fix; only matters above 1x. |
| `PS2X_PCSX2_MERGE_SPRITE` | 0, 1 | 1, from the game database | Upscaling fix; only matters above 1x. |

## Game

| Option | Values | Default | Effect |
| --- | --- | --- | --- |
| `PS2X_CONTROLS` | a file path | controls.ini in the game's folder, else built-in bindings | The key and controller bindings file. The launcher's Controls tab writes one. |
| `PS2X_MEMCARD_DIR` | a folder path | mc0 in the game's folder | Where memory card saves are kept. |
| `PS2X_INPUT_RECORD` | a file path | unset | Records pad input to this file. |
| `PS2X_INPUT_REPLAY` | a file path | unset | Plays a recording back instead of live input, then hands control back. |
| `PS2X_VIF1_THREAD` | 0, 1 | 1 | 0 keeps graphics data handling on the game's thread (slower; for comparing). |
| `PS2X_CONFIG` | a file path | ps2x.ini in the game's folder | The settings file to read. Environment only. |

## Paths

| Option | Values | Default | Effect |
| --- | --- | --- | --- |
| `PS2X_PCSX2_GS_DLL` | a file path | pcsx2-gs\ps2x-gs.dll beside the runner | The renderer DLL. |
| `PS2X_PCSX2_RESOURCES` | a folder path | resources beside the DLL | PCSX2's resources folder. |
| `PS2X_PCSX2_LOG` | 1 | unset | Writes PCSX2's log to gs-data\logs\ps2x-gs.log beside the DLL. |
| `PS2X_GAME_SERIAL` | a serial, such as SLUS-20732 | from the game file's name | The serial PCSX2 looks up for its per-game fixes. |

## Developer: running

| Option | Values | Default | Effect |
| --- | --- | --- | --- |
| `PS2X_AUTOPAD` | start:duration:hexmask;... in seconds | unset | Scripted button presses, counted from the first pad read. 0008 is Start, 4000 is Cross. |
| `PS2X_MAX_TICKS` | a number of frames | unset | Exits after that many frames. |
| `PS2X_DUMP_FRAMES_DIR` | a folder path | unset | Saves frames as PNGs there. |
| `PS2X_DUMP_FRAMES_EVERY` | vsyncs | unset | Gap between saved frames. |
| `PS2X_DUMP_FRAMES_AFTER` | seconds | 0 | Time before the first saved frame. |
| `PS2X_DUMP_FRAMES_COUNT` | a count | no limit | How many frames to save. |

## Developer: VU and VIF

| Option | Values | Default | Effect |
| --- | --- | --- | --- |
| `PS2X_VU1_NATIVE` | 0, verify | translated code | 0 runs VU1 in the interpreter; verify runs both and logs any difference. |
| `PS2X_VU0_NATIVE` | 0, verify | translated code | The same for VU0. |
| `PS2X_VU_VERIFY_AFTER` | seconds | 0 | When verifying starts. |
| `PS2X_VU_STATS` | 1 | unset | Reports VU call counts and cost. |
| `PS2X_VIF1_STATS` | 1 | unset | Reports VIF1 worker counts and what the game thread waited for. |
| `PS2X_VIF_VERIFY` | 1 | unset | Runs the fast and general UNPACK paths together and compares them. |
| `PS2X_VU1_DUMP_DIR` | a folder path | unset | Saves each distinct VU1 microcode image the game loads. |
| `PS2X_VU0_DUMP_DIR` | a folder path | unset | The same for VU0. |
| `PS2X_VU1_SNAPSHOT_DIR` | a folder path | unset | Saves VU1 state before program runs, for replaying them offline. |
| `PS2X_VU1_SNAPSHOT_AT` | seconds | 0 | When VU1 snapshots start. |
| `PS2X_VU1_SNAPSHOT_COUNT` | a count | 64 | How many VU1 snapshots to save. |
| `PS2X_VU0_SNAPSHOT_DIR` | a folder path | unset | Saves VU0 state before program runs. |
| `PS2X_VU0_SNAPSHOT_AT` | seconds | 0 | When VU0 snapshots start. |

## Developer: captures and tracing

| Option | Values | Default | Effect |
| --- | --- | --- | --- |
| `PS2X_GIF_CAPTURE` | a file path | unset | Records the GIF packets sent to the GS (read it with tools/testing/gif_capture.py). |
| `PS2X_GIF_CAPTURE_AT` | seconds | 0 | When the capture starts. |
| `PS2X_GIF_CAPTURE_VSYNCS` | a count | 1 | How many vsyncs to capture. |
| `PS2X_AUDIO_DUMP` | a file path | unset | Writes the sound output as raw 48 kHz 16-bit stereo. |
| `PS2X_CD_STREAM_LOG` | 1 | unset | Logs disc stream reads. |
| `PS2X_TRACE_FN` | hex addresses, comma separated | unset | Logs arguments and results of the first 400 calls to those game functions. Slows the game enough to disturb replays. |
| `PS2X_TRACE_FN_AFTER` | seconds | 0 | When call tracing starts. |
| `PS2X_TRACE_FN_FROM` | hexlo-hexhi | unset | Only traces calls made from that address range. |
| `PS2X_CALLPROF` | start,window in seconds | unset | Counts game function calls by caller and target over that window. |
| `PS2X_CALLPROF_ARGS` | a hex address | unset | Also logs the arguments of the first 200 calls to that function. |
| `PS2X_WATCH` | hexaddr:hexlength, comma separated | unset | Logs which game code writes to those memory ranges. |
| `PS2X_WATCH_AFTER` | seconds | 0 | When write watching and memory peeking start. |
| `PS2X_WATCH_VALUES` | 1, i, b, j[:delta] | unset | Logs every write with its value; i, b and j keep only suspicious float values. |
| `PS2X_PEEK` | hexaddr:hexlength, comma separated | unset | Prints those memory ranges as floats at intervals. |
| `PS2X_PEEK_MS` | milliseconds | 100 | The interval for PS2X_PEEK. |
| `PS2X_PEEK_HEX` | 1 | unset | Prints PS2X_PEEK ranges as hex words. |

