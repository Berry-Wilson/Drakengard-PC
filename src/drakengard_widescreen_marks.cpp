// 16:9 mode: marks drawn around a projected point with offsets in screen pixels. The offsets come out
// 4/3 too wide, so they are narrowed about the point right after the game builds them.
#include "drakengard_widescreen.h"

#include <algorithm>
#include <cstdlib>

namespace drakengard_widescreen
{
    namespace
    {
        // func_00212F78 draws the aiming reticle: its spinning arms, then (through func_00212864) its dots.
        // Both are offsets in screen pixels from the reticle's centre on screen, which is at +0x1B0 of the
        // object in a0 in GS units, so they come out 4/3 too wide. Everything goes into the packet builder
        // in a1, whose write pointer is at +0x4, and every vertex has depth 0x7300:
        //  - arms: PACKED XYZF2 qwords, x | y << 32 in the low half and the depth in the high half;
        //  - dots: sprites in REGLIST GIF tags (RGBAQ, UV, XYZF2, UV, XYZF2), each XYZF2 a 64-bit word
        //    x | y << 16 | z << 32.
        constexpr uint32_t kReticleCentreX = 0x1B0u;
        constexpr uint32_t kReticleDepth = 0x7300u;

        void hookReticle(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            const uint32_t reticle = GPR_U32(ctx, 4);
            const uint32_t builder = GPR_U32(ctx, 5);
            const uint32_t start = READ32(builder + 4u);
            func_00212F78_0x212f78(rdram, ctx, runtime);
            const uint32_t end = READ32(builder + 4u);
            if (end <= start || end - start > 0x2000u)
                return;
            const int centre = static_cast<int32_t>(READ32(reticle + kReticleCentreX));
            auto narrow = [&](uint32_t address)
            {
                const int x = READ16(address);
                WRITE16(address, static_cast<uint16_t>(centre + (x - centre) * 3 / 4));
            };
            for (uint32_t q = start; q + 16u <= end; q += 16u)
            {
                if (READ64(q + 8u) == kReticleDepth)
                {
                    narrow(q);
                }
                else if (READ64(q + 8u) == kSpriteRegs && q + 0x40u <= end &&
                         (READ32(q + 0x24u) & 0xFFFFFFu) == kReticleDepth &&
                         (READ32(q + 0x34u) & 0xFFFFFFu) == kReticleDepth)
                {
                    narrow(q + 0x20u);
                    narrow(q + 0x30u);
                    q += 0x30u;
                }
            }
        }

        // Enemy health bars (func_002AF5D0) and the EXP that floats up from a kill (func_0016609C) are
        // drawn the same way, each into a packet of its own (see readPacket). Every mark is drawn at
        // its own depth, which is how its pieces are told apart.

        // Each bar is an icon and a batch of four untextured sprites (backing, icon slot, empty bar, fill),
        // plus a frame for the enemies near the player, all at the enemy's depth but not necessarily next
        // to each other in the packet. With X the enemy's point on screen and h half the bar's length (16
        // or 24 pixels), the third sprite of the four runs from X - h to X + h, and the frame, a sprite on
        // its own, from X - (h + 13) to X + (h + 4), so its width gives h and then X.
        void hookEnemyBars(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            const uint32_t start = READ32(kSpritePacketStart);
            // Called by the minimap's function, but these are marks in the world.
            const Edge edge = g_hudEdge;
            g_hudEdge = Edge::None;
            func_002AF5D0_0x2af5d0(rdram, ctx, runtime);
            g_hudEdge = edge;
            const std::vector<PacketBatch> batches = readPacket(rdram, ctx, runtime, start, READ32(kSpritePacketStart));
            struct Bar
            {
                uint32_t z;
                int anchor;
            };
            std::vector<Bar> bars;
            for (const PacketBatch &batch : batches)
            {
                const bool untexturedSprite = (batch.prim & 7u) == 6u && ((batch.prim >> 4u) & 1u) == 0u;
                if (!untexturedSprite)
                    continue;
                if (batch.vertices.size() == 8u)
                {
                    const int x0 = READ16(batch.vertices[4].address);
                    const int x1 = READ16(batch.vertices[5].address);
                    bars.push_back({batch.vertices[4].z, (x0 + x1) / 2});
                    continue;
                }
                if (batch.vertices.size() != 2u || batch.vertices[0].z != batch.vertices[1].z)
                    continue;
                const int x0 = READ16(batch.vertices[0].address);
                const int x1 = READ16(batch.vertices[1].address);
                const int width = x1 - x0;
                if (width != 16 * 49 && width != 16 * 65)
                    continue;
                const int half = (width / 16 - 17) / 2;
                bars.push_back({batch.vertices[0].z, x1 - (half + 4) * 16});
            }
            for (const PacketBatch &batch : batches)
            {
                if ((batch.prim & 7u) != 6u)
                    continue;
                for (const PacketVertex &vertex : batch.vertices)
                {
                    // Enemies at the same depth are told apart by which frame is nearest.
                    const int x = READ16(vertex.address);
                    const Bar *nearest = nullptr;
                    for (const Bar &bar : bars)
                    {
                        if (bar.z == vertex.z && (!nearest || std::abs(bar.anchor - x) < std::abs(nearest->anchor - x)))
                            nearest = &bar;
                    }
                    if (nearest)
                        narrowAbout(rdram, ctx, runtime, vertex.address, nearest->anchor);
                }
            }
        }

        // The EXP text is centred on the enemy's point on screen: one sprite per letter, each with a
        // shadow copy 3 pixels to its right, at one depth per number, and the letters show one after
        // another. The function measures the whole string with func_002A5918 and starts it half that
        // width (times 16 for GS units) left of the point, so the point is the first letter's left edge
        // plus the width times 8, whichever letters are showing yet.
        int g_expTextWidth = -1;

        void hookTextWidth(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            constexpr uint32_t kFromExpText = 0x0016638Cu;
            const bool fromExpText = GPR_U32(ctx, 31) == kFromExpText;
            func_002A5918_0x2a5918(rdram, ctx, runtime);
            if (fromExpText)
                g_expTextWidth = static_cast<int32_t>(GPR_U32(ctx, 2));
        }

        void hookExpText(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
        {
            const uint32_t start = READ32(kSpritePacketStart);
            g_expTextWidth = -1;
            func_0016609C_0x16609c(rdram, ctx, runtime);
            if (g_expTextWidth <= 0)
                return;
            const std::vector<PacketBatch> batches = readPacket(rdram, ctx, runtime, start, READ32(kSpritePacketStart));
            auto isLetter = [](const PacketBatch &batch)
            {
                return (batch.prim & 7u) == 6u && ((batch.prim >> 4u) & 1u) != 0u;
            };
            int left = 0x10000;
            for (const PacketBatch &batch : batches)
            {
                if (isLetter(batch))
                    for (const PacketVertex &vertex : batch.vertices)
                        left = std::min(left, static_cast<int>(READ16(vertex.address)));
            }
            if (left == 0x10000)
                return;
            const int anchor = left + g_expTextWidth * 8;
            for (const PacketBatch &batch : batches)
            {
                if (isLetter(batch))
                    for (const PacketVertex &vertex : batch.vertices)
                        narrowAbout(rdram, ctx, runtime, vertex.address, anchor);
            }
        }
    }

    void registerWorldMarkHooks(PS2Runtime &runtime)
    {
        runtime.registerFunction(0x00212F78u, hookReticle);
        runtime.registerFunction(0x002AF5D0u, hookEnemyBars);
        runtime.registerFunction(0x0016609Cu, hookExpText);
        runtime.registerFunction(0x002A5918u, hookTextWidth);
    }
}
