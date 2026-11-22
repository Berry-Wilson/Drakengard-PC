# Full-Height Picture

How the Drakengard PC port shows the game's whole 640x480 frame instead of the 224-line copy a PS2
sends to the TV, why it has to present the frame at one exact moment, and how to find the same thing
in another game.

| | |
| --- | --- |
| Option | `PS2X_PROGRESSIVE`: on by default, `0` turns it off. The launcher's Video tab has a **Full-Height Picture** box. |
| Game code | [`src/drakengard_progressive.cpp`](../src/drakengard_progressive.cpp), registered from `src/drakengard_overrides.cpp` (`applyDrakengardProgressive`) |
| Runtime support | `g_ps2xFinishedFrameTick` and `UploadFrame` in `external/PS2Recomp/ps2xRuntime/src/lib/ps2_runtime.cpp`; `GS::latchHostPresentationFrame()` in `src/lib/gs/gs_frontend.cpp` |
| Renderer | PCSX2's GS as `ps2x-gs.dll` |
| History | added 2026-11-09 (`bd4eff1`), flicker fix 2026-11-16 (`b485519`), on by default 2026-11-22 (`da0c4a2`) |

## The problem

Upscaling didn't add as much detail as it should have, at any internal resolution. Logging the
render targets in the GIF stream showed why:

1. The game draws each frame, scene and HUD, into a **640x480** buffer (FBP 80, the drawing buffer).
2. Its flip, `func_0028A964`, then copies that buffer into a **640x224** buffer (FBP 0), in two
   passes of 20 sprites, the second half a line lower and alpha-blended. That's a flicker filter
   for an interlaced TV, and it squeezes the 480 lines down to 448 in the process.
3. Only the 224-line buffer is shown, with SMODE2 set to interlaced field mode (INT 1, FFMD 1).

So more than half the vertical detail is thrown away before anything reaches the screen, and the
blend softens what's left. A higher internal resolution only makes a sharper copy of a blurred,
squeezed picture.

Since the game finishes a whole 480-line frame every time, the fix is to show that frame.

## What the game keeps

The game's graphics object (set up in `func_00289940`) holds the parameters of both buffers, one
after the other:

| Offset | Buffer | Fields |
| --- | --- | --- |
| +0x1018 | shown (FBP 0, 640x224) | FBP, PSM, width, height, DX, DY, SMODE2 INT, SMODE2 FFMD |
| +0x1038 | drawing (FBP 80, 640x480) | FBP, PSM, width, height, ... |

Two functions turn these into the display registers:

- `func_0012445C` builds PMODE, SMODE2, DISPFB and DISPLAY from the shown buffer's parameters (its
  `$a0` points at +0x1018) into a register block at +0x1078. The game's environment code then
  moves that block along (+0x1078, +0x10A8, +0x10D8).
- `func_0012454C` writes the display registers. The flip calls it every frame.

The flip, `func_0028A964`, calls these in order:

1. 0x28A638 and `func_0012445C`, twice each
2. 0x28A6F4, 0x28A784, 0x28A800, 0x28A87C, a printf (0x311B78), `func_00310B58`
3. `func_0012454C`, which writes the display registers and returns to **0x28AB14**
4. `func_0031BD20`, then `sceDmaGetChan` (0x2F9998) and `sceDmaSend` (0x2F9CC8): one DMA packet
   that copies the frame into the 224-line buffer and then **clears the drawing buffer** for the
   next frame

`func_0012454C` has one other caller, which returns to 0x28AE54.

## How it works

There are two hooks, both registered only when the option is on.

### 1. Show the drawing buffer

`hookBuildDisplay` replaces `func_0012445C`. For the duration of the call it overwrites the shown
buffer's parameters with the drawing buffer's (FBP, PSM, height), sets FFMD to 0 (frame mode),
calls the original, then puts the parameters back:

```cpp
const bool expected = saved[4] != 0u && drawWidth == saved[2] && drawHeight >= saved[3] * 2u;
if (expected) { /* write the drawing buffer's FBP, PSM, height; FFMD = 0 */ }
func_0012445C_0x12445c(rdram, ctx, runtime);
if (expected) { /* restore the four fields */ }
```

So the game builds PMODE, SMODE2, DISPFB and DISPLAY for a 640x480 frame in interlaced frame mode,
with its own code. Nothing else in the game sees a difference: it still makes the 224-line copy,
which is just no longer shown. The check only lets through the layout this was written for (a field
buffer shown in field mode, copied from a frame of the same width and at least twice the height).
Anything else, like a movie or a screen with a different setup, is left alone.

### 2. Present the frame at the one moment it is whole

Showing the drawing buffer brings in a timing problem. The runtime normally presents at every
vertical blank (`UploadFrame` on the main thread calls `latchHostPresentationFrame()` once per vsync
tick). For the drawing buffer, that's the wrong moment: the game starts drawing the next frame into
it before the vblank, so a vsync-time present shows a half-drawn frame (often just the sky).

The drawing buffer holds a whole frame for only a moment: after the game finishes drawing and
before the flip's DMA packet clears it. `hookWriteDisplay` presents exactly there:

```cpp
constexpr uint32_t kFlipAfterDisplayRegs = 0x0028AB14u;

void hookWriteDisplay(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
{
    const uint32_t returnAddress = GPR_U32(ctx, 31);
    func_0012454C_0x12454c(rdram, ctx, runtime);
    if (returnAddress != kFlipAfterDisplayRegs)
        return;
    // Everything queued for this frame reaches PCSX2's stream before the present.
    runtime->memory().waitVif1Idle("progressive-present");
    runtime->gs().latchHostPresentationFrame();
    g_ps2xFinishedFrameTick.store(runtime->eeScheduler().currentVSyncTick() + 1u, std::memory_order_release);
}
```

- It runs the original first, so the display registers already describe the new frame.
- It only presents when called from the flip (return address 0x28AB14), not from the other caller.
- `waitVif1Idle` waits until the VIF1 worker thread has passed everything the game has queued so
  far to the GS. The frame's last draws may still be on that thread.
- `latchHostPresentationFrame()` flushes and syncs the backend, then presents what the display
  registers point at.
- `g_ps2xFinishedFrameTick` tells the runtime the game is presenting its own frames.

### The runtime's side: `g_ps2xFinishedFrameTick`

This is a general mechanism in PS2Recomp for any game override that knows when a frame is finished
(`ps2_runtime.cpp`):

```cpp
std::atomic<uint64_t> g_ps2xFinishedFrameTick{0}; // vsync tick of the last finished frame, plus one
static constexpr uint64_t kFinishedFrameTimeoutTicks = 30;
...
const bool gamePresents = finishedFrameTick != 0u &&
                          currentTick + 1u < finishedFrameTick + kFinishedFrameTimeoutTicks;
if (needsLatch && !gamePresents)
    rt->gs().latchHostPresentationFrame();
```

While the override keeps reporting finished frames, `UploadFrame` stops presenting at each vsync on
its own. If none comes for 30 vsyncs (a loading screen that never runs the flip, say), it goes back
to presenting at vsync, so the window never freezes.

## The flicker bug, and why the present is where it is

The first version (2026-11-09) presented right **after** the flip returned. That looked fine on the
dev PC, but a slower test PC flickered: single frames came out black on the title screen, or flat
fog colour (RGB 125,118,101) or grey in missions.

The cause was a race with the flip's own DMA packet:

- The flip ends with `sceDmaSend`. The packet (the 224-line copy, then the clear of the drawing
  buffer) is processed on the runtime's VIF1 worker thread, not the game thread.
- PCSX2's GS takes the GIF stream and presents in one ordered queue (`ps2xgs_gif` appends under
  a lock from any thread, and `ps2xgs_vsync` cuts the stream into a frame there).
- After the flip, the present on the game thread and the packet on the VIF1 thread raced. Whenever
  the worker sent the clear first, the present showed the cleared buffer.
- Being a race, it came and went with timing, which is why a slower PC saw it more and the dev PC
  rarely did. On the dev PC it showed most readily with the DX11 and Vulkan renderers.

The fix (2026-11-16, `b485519`) moved the present inside the flip, onto the return of
`func_0012454C`. At that point the frame is complete and the packet hasn't been sent yet, so there
is nothing to race, and `waitVif1Idle` makes sure everything before it is already in the stream.

How the two failure modes look, which helps tell them apart:

| What you see | What it means |
| --- | --- |
| Whole frames of one flat colour (black, fog, grey) | The present ran after the clear: presenting too late |
| Partial frames (sky only, missing geometry) | The vsync fallback on the main thread is presenting: the hook isn't firing (for example, a wrong return address) |

The second one bit during the fix itself: the first attempt used return address 0x28AB0C, misread
from the dispatcher's arguments, so the hook never presented and the fallback did. Logging `$ra`
once from inside the hook gave the real 0x28AB14.

## Testing it

- **Stress test:** add an `fprintf` inside `latchHostPresentationFrame()` to slow the present.
  Before the fix, about 199 of 200 frames came out blank this way. After it, 0 of 194.
- **Blank frames:** dump frames during a replay and flag those with a pixel standard deviation
  under 5 (PIL). Flashes show as a frame far from both neighbours.
- **Results after the fix:** no flashes in 899 frames of the title and menus on DX11, 0 blank of
  200 in a Vulkan mission. The regression check (`tools/testing/check.ps1`, which runs with
  `PS2X_PROGRESSIVE=1`) matched its baseline.
- **On/off:** with the option unset the log prints
  `[DrakengardPC] full-height output (640x480 frame instead of the 224-line field copy)`. With
  `PS2X_PROGRESSIVE=0` it doesn't.

## Finding the same thing in another game

Many PS2 games draw at 640x448 or 640x480 and send the TV a field-sized or blended copy. The steps
that found it here:

1. **Log a census of the GIF stream.** For each draw, record FRAME (FBP, FBW), SCISSOR and TEX0.
   A big target that is drawn into all frame and then only ever read as a texture into a smaller
   target, which is the one DISPFB points at, is the pattern.
2. **Compare with the display registers.** DISPFB's FBP and DISPLAY's DH tell you what's actually
   shown. SMODE2 INT 1 with FFMD 1 means field mode, i.e. one field-height buffer per vsync.
3. **Find where the game builds the display registers.** Here they come from a parameter block in
   a graphics object, so one hook on the builder changes them without touching the game's data
   for good. Look for the writes to PMODE, SMODE2, DISPFB and DISPLAY (0x12000000 up) and walk back
   to where the values come from.
4. **Find the moment the frame is whole.** Usually that's in the flip, after the last draw and
   before the game clears the buffer or starts the next frame. Present there with
   `waitVif1Idle()`, `latchHostPresentationFrame()` and `g_ps2xFinishedFrameTick`, and check the
   caller's return address if the function you hook has more than one caller.
5. **Check before you present that nothing still to come will change the buffer.** Look at the DMA
   the flip sends after your hook point. A clear or copy there that runs on another thread is the
   race described above.

## Limits

- Needs PCSX2's renderer, which takes the GIF stream and presents in order on one queue. The removed
  parallel-gs renderer gave flat-colour frames when presented from the game thread.
- Verified through boot, the title and menus, a briefing and several minutes of fighting in the
  Chapter 1 Verse 9 sky mission, in 4:3 and 16:9, and on the clean test PC. Not checked: movies
  beyond the opening logos, other missions.
- The game's own flicker-filtered copy is still made every frame, so the saving is in picture
  quality only, not speed.
