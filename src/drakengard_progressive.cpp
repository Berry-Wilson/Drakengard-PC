// Optional full-height output for Drakengard, turned on by PS2X_PROGRESSIVE=1.
//
// The game draws each frame (scene and HUD) into a 640x480 buffer, then copies it into a 640x224
// buffer for an interlaced TV: two passes half a line apart, blended together (a flicker filter).
// Only that 224-line buffer is shown, so more than half the vertical detail is lost, at any internal
// resolution.
//
// The game keeps the parameters of both buffers in its graphics object: the shown buffer at +0x1018
// (FBP, PSM, width, height, DX, DY, SMODE2 INT, SMODE2 FFMD) and the drawing buffer right after it at
// +0x1038 (FBP, PSM, width, height, ...). func_0012445C turns the shown buffer's parameters into the
// display registers (PMODE, SMODE2, DISPFB, DISPLAY). With this setting it builds them from the
// drawing buffer instead, in interlaced frame mode (FFMD 0), so the GS shows all 480 lines of the
// finished frame. The game still makes its copy into the 224-line buffer, which is no longer shown.
//
// The drawing buffer only holds a whole frame for a moment: after its flip (func_0028A964, which sends
// the copy) the game starts drawing the next frame into it before the vertical blank. So the frame
// is presented right after the flip instead of at the vertical blank (g_ps2xFinishedFrameTick).
//
// Only with PCSX2's renderer (PS2X_GS_BACKEND=pcsx2): it takes the GIF stream and the present on one
// queue, in order, from the game's thread. parallel-gs presents from the host thread while its worker
// replays the stream, so it cannot present at the flip; it keeps the full height its own way, with
// PS2X_HIRES_SCANOUT and 4x super-sampled textures.
#include "game_overrides.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "ps2_recompiled_functions.h"
#include "runtime/ee_scheduler.h"
#include "runtime/host_display.h"

#include <atomic>
#include <cstdlib>
#include <iostream>

extern std::atomic<uint64_t> g_ps2xFinishedFrameTick; // ps2_runtime.cpp

namespace drakengard_progressive
{
    // Offsets in the display parameters (func_0012445C's $a0).
    constexpr uint32_t kFbp = 0x00u, kPsm = 0x04u, kWidth = 0x08u, kHeight = 0x0Cu, kFfmd = 0x1Cu;
    // The drawing buffer's parameters follow the display's.
    constexpr uint32_t kDraw = 0x20u;

    void hookBuildDisplay(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const uint32_t params = GPR_U32(ctx, 4);
        const uint32_t saved[5] = {READ32(params + kFbp), READ32(params + kPsm), READ32(params + kWidth),
                                   READ32(params + kHeight), READ32(params + kFfmd)};
        const uint32_t drawFbp = READ32(params + kDraw + kFbp);
        const uint32_t drawPsm = READ32(params + kDraw + kPsm);
        const uint32_t drawWidth = READ32(params + kDraw + kWidth);
        const uint32_t drawHeight = READ32(params + kDraw + kHeight);

        // Only the layout this was written for: a field buffer shown in field mode, copied from a frame
        // of the same width and at least twice its height (480 lines into 224: the copy also squeezes
        // the frame to 448 lines). Anything else is left alone.
        const bool expected = saved[4] != 0u && drawWidth == saved[2] && drawHeight >= saved[3] * 2u;
        if (expected)
        {
            WRITE32(params + kFbp, drawFbp);
            WRITE32(params + kPsm, drawPsm);
            WRITE32(params + kHeight, drawHeight);
            WRITE32(params + kFfmd, 0u);
        }
        func_0012445C_0x12445c(rdram, ctx, runtime);
        if (expected)
        {
            WRITE32(params + kFbp, saved[0]);
            WRITE32(params + kPsm, saved[1]);
            WRITE32(params + kHeight, saved[3]);
            WRITE32(params + kFfmd, saved[4]);
        }
    }

    // The flip: once it returns, the copy has been sent and the frame in the drawing buffer is whole.
    void hookFlip(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        func_0028A964_0x28a964(rdram, ctx, runtime);
        runtime->gs().latchHostPresentationFrame();
        g_ps2xFinishedFrameTick.store(runtime->eeScheduler().currentVSyncTick() + 1u, std::memory_order_release);
    }
}

void applyDrakengardProgressive(PS2Runtime &runtime)
{
    using namespace drakengard_progressive;
    const char *value = std::getenv("PS2X_PROGRESSIVE");
    if (!value || value[0] != '1')
        return;
    if (!HostDisplay::externalPresenter())
    {
        std::cerr << "[DrakengardPC] PS2X_PROGRESSIVE needs PCSX2's renderer (PS2X_GS_BACKEND=pcsx2); ignored" << std::endl;
        return;
    }
    runtime.registerFunction(0x0012445Cu, hookBuildDisplay);
    runtime.registerFunction(0x0028A964u, hookFlip);
    std::cerr << "[DrakengardPC] full-height output (640x480 frame instead of the 224-line field copy)" << std::endl;
}
