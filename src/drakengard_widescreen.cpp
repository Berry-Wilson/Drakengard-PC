// Optional 16:9 mode for Drakengard, turned on by PS2X_ASPECT=16:9.
//
// The game always renders a 640x480 image, which the host display stretches to 16:9. So everything
// is drawn 3/4 as wide as it should end up:
//  - 3D: the projection's horizontal scale is reduced, which also shows more of the world. The VU1
//    polygon clipper has its own clip angles, computed from a fixed half width of 320, so those are
//    widened to match or the near ground stops at the old 4:3 edges. The frustum that decides what
//    is drawn at all is widened too, or terrain at the sides is left out.
//  - Marks drawn around a projected point with offsets in screen pixels (the aiming reticle, enemy
//    health bars, the EXP from a kill) have those offsets narrowed right after the game builds them.
//  - Screen-space art: the GIF stream is rewritten so that it all keeps its shape in a centred 4:3
//    layout, which is where menus belong. The HUD's elements are moved out to the screen's edges by
//    hooks on the functions that draw them.
//  - Movies are the exception: PCSX2 shows them in 4:3 with bars at the sides, and nothing is
//    reshaped while one plays (the title menu's background is a movie too).
//
// This file has the 3D part and turns the mode on. The rest:
//   drakengard_widescreen_marks.cpp   the reticle, enemy health bars and EXP
//   drakengard_widescreen_hud.cpp     the hooks that move HUD elements to the edges
//   drakengard_widescreen_screen.cpp  the GIF stream rewriter for screen-space art
//   drakengard_widescreen_packet.cpp  reading the packets the game's sprite routines build
#include "drakengard_widescreen.h"
#include "runtime/host_display.h"

#include <cmath>
#include <cstring>
#include <iostream>

namespace drakengard_widescreen
{
    namespace
    {
        constexpr uint32_t kClipAngleX = 0x298u; // in the camera at 0x480B00: horizontal half angle

        void hookViewScreenMatrix(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            ctx->f[13] *= kNarrow; // ax
            sceVu0ViewScreenMatrix_0x2fcfd0(rdram, ctx, runtime);
        }

        void hookSetClipAngles(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            const uint32_t camera = GPR_U32(ctx, 4);
            func_00263F44_0x263f44(rdram, ctx, runtime);
            uint32_t bits = READ32(camera + kClipAngleX);
            float angle;
            std::memcpy(&angle, &bits, sizeof(angle));
            // The game computes atan(scrz / 320), so a wider half width makes the tangent smaller.
            angle = std::atan(std::tan(angle) * kNarrow);
            std::memcpy(&bits, &angle, sizeof(bits));
            WRITE32(camera + kClipAngleX, bits);
        }

        // func_00129988 builds the view frustum's corners, which decide what terrain and objects get drawn
        // at all, from the viewport in a1: x reaches width * far * ax / (2 * scrz). That is the 4:3 view,
        // so the sides of the wider picture went unfilled (sky missions show it most). The function sees
        // a 4/3 wider screen; the viewport's width itself stays 640, as menus lay themselves out from it.
        void hookFrustumCorners(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            const uint32_t viewport = GPR_U32(ctx, 5);
            const uint32_t saved = READ32(viewport);
            float width;
            std::memcpy(&width, &saved, sizeof(width));
            width /= kNarrow;
            uint32_t bits;
            std::memcpy(&bits, &width, sizeof(bits));
            WRITE32(viewport, bits);
            func_00129988_0x129988(rdram, ctx, runtime);
            WRITE32(viewport, saved);
        }
    }

    void applyIfEnabled(PS2Runtime &runtime)
    {
        if (HostDisplay::outputAspect() <= 0.0f)
            return;
        runtime.registerFunction(0x002FCFD0u, hookViewScreenMatrix);
        runtime.registerFunction(0x00263F44u, hookSetClipAngles);
        runtime.registerFunction(0x00129988u, hookFrustumCorners);
        registerWorldMarkHooks(runtime);
        registerHudHooks(runtime);
        installScreenArtRewriter();
        std::cerr << "[DrakengardPC] 16:9 mode on" << std::endl;
    }
}

void applyDrakengardWidescreen(PS2Runtime &runtime)
{
    drakengard_widescreen::applyIfEnabled(runtime);
}
