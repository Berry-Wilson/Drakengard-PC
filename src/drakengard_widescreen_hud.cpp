// 16:9 mode: the HUD's elements are moved out to the screen's edges.
#include "drakengard_widescreen.h"

namespace drakengard_widescreen
{
    Edge g_hudEdge = Edge::None;

    namespace
    {
        // The HUD. Each element has its own function, called once a frame from the mission's draw function
        // (func_0014074C), and they all place their art through four routines that take the position in
        // a2 (x) and a3 (y) and the depth in t0:
        //   func_002A8934  a sprite, from its top left corner in pixels
        //   func_002BF680  a turned and scaled picture, from its centre in pixels
        //   func_002BF7A0  a gauge segment, in pixels
        //   func_002A51D4  a letter, in GS units
        // The stream rewriter below squeezes all screen-space art about the screen's centre. Art that
        // belongs at an edge is moved sideways here first, by the distance that makes the squeeze leave it
        // as far from that edge as it is in 4:3: a third of half the screen, 106.67 pixels.
        constexpr int kEdgeShiftPixels = 107;
        // Squeezed, this is the right edge of the screen.
        constexpr int kPastScreenRight = kScreenCentre + ((kScreenRight - kScreenCentre) * 4 + 2) / 3;

        template <Edge E, auto Original>
        void hookHudElement(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            const uint32_t returnsTo = GPR_U32(ctx, 31);
            const uint32_t start = READ32(kSpritePacketStart);
            const Edge outer = g_hudEdge;
            g_hudEdge = E;
            Original(rdram, ctx, runtime);
            // A recompiled function can also come back here before it is done, to be resumed by the
            // dispatcher. Its element then stays current until the next one starts.
            if (ctx->pc != returnsTo)
                return;
            g_hudEdge = outer;
            if (outer != Edge::None)
                return;
            // Flat fills (the lines under the gauges) are written straight into the element's packet,
            // not through the routines above, so they are moved here.
            const int shift = kEdgeShiftPixels * 16 * (E == Edge::Left ? -1 : 1);
            for (const PacketBatch &batch : readPacket(rdram, ctx, runtime, start, READ32(kSpritePacketStart)))
            {
                if (((batch.prim >> 4u) & 1u) != 0u)
                    continue;
                for (const PacketVertex &vertex : batch.vertices)
                {
                    if (vertex.z == kHudZ)
                        WRITE16(vertex.address, static_cast<uint16_t>(READ16(vertex.address) + shift));
                }
            }
        }

        void hookMissionDraw(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            g_hudEdge = Edge::None;
            func_0014074C_0x14074c(rdram, ctx, runtime);
        }

        // The recompiler split func_002B04DC in two. Its first piece ends by handing over to the second
        // (func_002B0500) through the dispatcher, which makes every caller return first and resume later,
        // so the minimap's function (the caller) would leave its hook long before it is done. Running the
        // second piece from here lets it return the usual way.
        void hookJoinSplitFunction(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            // It draws the lock-on runes: marks in the world, like the enemy bars.
            const Edge edge = g_hudEdge;
            g_hudEdge = Edge::None;
            func_002B04DC_0x2b04dc(rdram, ctx, runtime);
            if (ctx->pc == 0x002B0500u)
                func_002B0500_0x2b0500(rdram, ctx, runtime);
            g_hudEdge = edge;
        }

        // Returns how far the art about to be drawn was moved, in the routine's own units.
        int moveToEdge(R5900Context *ctx, int unitsPerPixel)
        {
            constexpr int kX = 6;     // a2
            constexpr int kDepth = 8; // t0
            const uint32_t z = GPR_U32(ctx, kDepth);
            if (g_hudEdge == Edge::None || (z != kHudZ && z != kDialogTextZ))
                return 0;
            const int shift = kEdgeShiftPixels * unitsPerPixel * (g_hudEdge == Edge::Left ? -1 : 1);
            SET_GPR_S32(ctx, kX, static_cast<int32_t>(GPR_U32(ctx, kX)) + shift);
            return shift;
        }

        template <auto Original>
        void hookHudArt(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            moveToEdge(ctx, 1);
            Original(rdram, ctx, runtime);
        }

        // The letter routine returns where the next letter goes, which its callers pass straight back
        // in, so that has to be where it would have been.
        void hookHudLetter(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            const int shift = moveToEdge(ctx, 16);
            func_002A51D4_0x2a51d4(rdram, ctx, runtime);
            if (shift != 0)
                SET_GPR_S32(ctx, 2, static_cast<int32_t>(GPR_U32(ctx, 2)) - shift);
        }

        // The backing of the dialog box at the top left runs to the right edge of the screen, and has to
        // keep reaching it. The sprite is the last thing this routine writes into the packet builder in
        // a1: a REGLIST GIF tag (kSpriteRegs) and three qwords, with the corners in the last two.
        void hookHudSprite(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            const uint32_t builder = GPR_U32(ctx, 5);
            const bool moved = moveToEdge(ctx, 1) != 0;
            func_002A8934_0x2a8934(rdram, ctx, runtime);
            if (!moved || g_hudEdge != Edge::Left)
                return;
            const uint32_t tag = READ32(builder + 4u) - 0x40u;
            if (READ64(tag + 8u) != kSpriteRegs)
                return;
            for (const uint32_t corner : {tag + 0x20u, tag + 0x30u})
            {
                if (READ16(corner) + kEdgeShiftPixels * 16 >= kScreenRight)
                    WRITE16(corner, static_cast<uint16_t>(kPastScreenRight));
            }
        }
    }

    void registerHudHooks(PS2Runtime &runtime)
    {
        runtime.registerFunction(0x0014074Cu, hookMissionDraw);
        runtime.registerFunction(0x002B04DCu, hookJoinSplitFunction);
        runtime.registerFunction(0x002B58C4u, hookHudElement<Edge::Right, func_002B58C4_0x2b58c4>); // minimap
        runtime.registerFunction(0x002BC370u, hookHudElement<Edge::Right, func_002BC370_0x2bc370>); // kill counter
        runtime.registerFunction(0x002B9114u, hookHudElement<Edge::Left, func_002B9114_0x2b9114>);  // emblem
        runtime.registerFunction(0x002BA0ECu, hookHudElement<Edge::Left, func_002BA0EC_0x2ba0ec>);  // gauges
        runtime.registerFunction(0x002B47E0u, hookHudElement<Edge::Left, func_002B47E0_0x2b47e0>);  // chain counter
        runtime.registerFunction(0x002B7C30u, hookHudElement<Edge::Left, func_002B7C30_0x2b7c30>);  // dialog box
        // On the dragon the dialog box is drawn through func_0020B558, which ends in a jump to the
        // function above; the recompiler turns that into a direct call, which no hook sees.
        runtime.registerFunction(0x0020B558u, hookHudElement<Edge::Left, func_0020B558_0x20b558>);
        runtime.registerFunction(0x002618C4u, hookHudElement<Edge::Left, func_002618C4_0x2618c4>);  // dialog text
        runtime.registerFunction(0x001A35A0u, hookHudElement<Edge::Left, func_001A35A0_0x1a35a0>);  // opening lines
        runtime.registerFunction(0x002A8934u, hookHudSprite);
        runtime.registerFunction(0x002BF680u, hookHudArt<func_002BF680_0x2bf680>);
        runtime.registerFunction(0x002BF7A0u, hookHudArt<func_002BF7A0_0x2bf7a0>);
        runtime.registerFunction(0x002A51D4u, hookHudLetter);
    }
}
