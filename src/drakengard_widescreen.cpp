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
#include "game_overrides.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "ps2_recompiled_functions.h"
#include "runtime/gs/gif_rewriter.h"
#include "runtime/host_display.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <iostream>
#include <vector>

namespace drakengard_widescreen
{
    constexpr float kNarrow = 3.0f / 4.0f;

    // Which edge of the screen the HUD element being drawn right now belongs at (see hookHudElement).
    enum class Edge
    {
        None,
        Left,
        Right
    };
    Edge g_hudEdge = Edge::None;
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

    // func_00212F78 draws the aiming reticle: its spinning arms, then (through func_00212864) its dots.
    // Both are offsets in screen pixels from the reticle's centre on screen, which is at +0x1B0 of the
    // object in a0 in GS units, so they come out 4/3 too wide. Everything goes into the packet builder
    // in a1, whose write pointer is at +0x4, and every vertex has depth 0x7300:
    //  - arms: PACKED XYZF2 qwords, x | y << 32 in the low half and the depth in the high half;
    //  - dots: sprites in REGLIST GIF tags (RGBAQ, UV, XYZF2, UV, XYZF2), each XYZF2 a 64-bit word
    //    x | y << 16 | z << 32.
    constexpr uint32_t kReticleCentreX = 0x1B0u;
    constexpr uint32_t kReticleDepth = 0x7300u;
    constexpr uint64_t kSpriteRegs = 0x43431ull;

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
                     (READ32(q + 0x24u) & 0xFFFFFFu) == kReticleDepth && (READ32(q + 0x34u) & 0xFFFFFFu) == kReticleDepth)
            {
                narrow(q + 0x20u);
                narrow(q + 0x30u);
                q += 0x30u;
            }
        }
    }

    // Enemy health bars (func_002AF5D0) and the EXP that floats up from a kill (func_0016609C) are also
    // drawn around a projected point with offsets in screen pixels. Each opens a fresh packet at the
    // buffer kSpritePacketStart points to and closes it before returning, leaving that pointer just
    // past it. The packet is a chain of DMA cnt tags, each followed by a VIF DIRECT of GIF tags; every
    // mark is drawn at its own depth, which is how its pieces are told apart.
    constexpr uint32_t kSpritePacketStart = 0x004F50D4u;

    struct PacketVertex
    {
        uint32_t address; // the x field, 16 bits
        uint32_t z;
    };

    struct PacketBatch
    {
        uint32_t prim;
        uint32_t regs;   // the GIF tag's register list
        uint32_t format; // 0 PACKED, 1 REGLIST
        std::vector<PacketVertex> vertices;
    };

    std::vector<PacketBatch> readPacket(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime, uint32_t start, uint32_t end)
    {
        std::vector<PacketBatch> batches;
        if (end <= start || end - start > 0x40000u)
            return batches;
        uint32_t prim = 0u;
        for (uint32_t chunk = start; chunk + 16u <= end;)
        {
            const uint32_t tag = READ32(chunk);
            const uint32_t qwc = tag & 0xFFFFu;
            const uint32_t id = (tag >> 28u) & 7u;
            const uint32_t vif = READ32(chunk + 12u);
            const uint32_t data = chunk + 16u;
            const uint32_t dataEnd = std::min(end, data + qwc * 16u);
            constexpr uint32_t kCnt = 1u, kNext = 2u, kRef = 3u, kRefs = 4u, kCall = 5u, kEnd = 7u;
            if (id == kRef || id == kRefs)
            {
                // The data (texture images here) lives elsewhere; the next tag follows this one.
                chunk = data;
                continue;
            }
            if (id == kNext)
            {
                const uint32_t next = READ32(chunk + 4u) & 0x7FFFFFF0u;
                if (next <= chunk || next >= end)
                    break;
                chunk = next;
            }
            else if (id == kCnt || id == kEnd || id == kCall)
            {
                // A call runs a shared chain elsewhere (texture uploads) and comes back to the tag after
                // its own data, which is where this walk goes on.
                chunk = data + qwc * 16u;
            }
            else
            {
                break;
            }
            if (((vif >> 24u) & 0x7Fu) != 0x50u && ((vif >> 24u) & 0x7Fu) != 0x51u) // DIRECT, DIRECTHL
                continue;
            for (uint32_t q = data; q + 16u <= dataEnd;)
            {
                const uint64_t tagLo = READ64(q);
                const uint64_t tagHi = READ64(q + 8u);
                q += 16u;
                const uint32_t nloop = static_cast<uint32_t>(tagLo & 0x7FFFu);
                const uint32_t format = static_cast<uint32_t>((tagLo >> 58u) & 3u);
                uint32_t nreg = static_cast<uint32_t>((tagLo >> 60u) & 0xFu);
                if (nreg == 0u)
                    nreg = 16u;
                if ((tagLo >> 46u) & 1u)
                    prim = static_cast<uint32_t>((tagLo >> 47u) & 0x7FFu);
                PacketBatch batch{prim, static_cast<uint32_t>(tagHi), format, {}};
                if (format == 0u)
                {
                    for (uint32_t i = 0u; i < nloop * nreg && q + 16u <= dataEnd; ++i, q += 16u)
                    {
                        const uint32_t desc = static_cast<uint32_t>((tagHi >> ((i % nreg) * 4u)) & 0xFu);
                        if (desc == 5u || desc == 13u) // XYZ2, XYZ3
                            batch.vertices.push_back({q, READ32(q + 8u)});
                        else if (desc == 4u || desc == 12u) // XYZF2, XYZF3
                            batch.vertices.push_back({q, (READ32(q + 8u) >> 4u) & 0xFFFFFFu});
                        else if (desc == 0u || (desc == 14u && (READ32(q + 8u) & 0xFFu) == 0u)) // PRIM, A+D PRIM
                            batch.prim = prim = READ32(q) & 0x7FFu;
                    }
                }
                else if (format == 1u)
                {
                    const uint32_t words = nloop * nreg;
                    for (uint32_t k = 0u; k < words && q + k * 8u + 8u <= dataEnd; ++k)
                    {
                        const uint32_t at = q + k * 8u;
                        const uint32_t desc = static_cast<uint32_t>((tagHi >> ((k % nreg) * 4u)) & 0xFu);
                        if (desc == 5u || desc == 13u)
                            batch.vertices.push_back({at, READ32(at + 4u)});
                        else if (desc == 4u || desc == 12u)
                            batch.vertices.push_back({at, READ32(at + 4u) & 0xFFFFFFu});
                        else if (desc == 0u)
                            batch.prim = prim = READ32(at) & 0x7FFu;
                    }
                    q += (words + 1u) / 2u * 16u;
                }
                else
                {
                    q += nloop * 16u;
                }
                if (!batch.vertices.empty())
                    batches.push_back(std::move(batch));
            }
        }
        return batches;
    }

    void narrowAbout(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime, uint32_t address, int anchor)
    {
        const int x = READ16(address);
        WRITE16(address, static_cast<uint16_t>(anchor + (x - anchor) * 3 / 4));
    }

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
        auto isLetter = [](const PacketBatch &batch) { return (batch.prim & 7u) == 6u && ((batch.prim >> 4u) & 1u) != 0u; };
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

    // Every draw here is offset so that (0x6C00, 0x7100) is the top left of the screen. X and Y have
    // 4 fractional bits.
    constexpr int kScreenLeft = 0x6C00;
    constexpr int kScreenTop = 0x7100;
    constexpr int kScreenWidth = 640 * 16;
    constexpr int kScreenCentre = kScreenLeft + kScreenWidth / 2;
    constexpr int kScreenRight = kScreenLeft + kScreenWidth;

    constexpr int px(int pixels) { return pixels * 16; }

    // Screen-space art is drawn with every vertex in a batch at one of a few fixed depths: 65535 (HUD,
    // menus), 16777215 (dialog text), 65000 and 49999-50000 (map). Flat 3D sprites, such as the enemy
    // health bar icons and some spell effects, also share one depth per batch and can come close to
    // these, so only the exact values count.
    constexpr uint32_t kHudZ = 65535u;
    constexpr uint32_t kDialogTextZ = 16777215u;
    constexpr uint32_t kMapGradientZ = 65000u;
    constexpr uint32_t kMapZ = 50000u;
    constexpr uint32_t kMapLowZ = 49999u;

    constexpr bool isScreenSpaceZ(uint32_t z)
    {
        return z == kHudZ || z == kDialogTextZ || z == kMapGradientZ || z == kMapZ || z == kMapLowZ;
    }

    // The R3 menu (map, mission, status) is drawn in layers at fixed depths from 51000 up: its parchment
    // at 51000, a marker per enemy from 53000, buttons and panels from 60000, gauges from 64000. Spell
    // effects in a mission can use the same depths, so these count only while a menu is up; the
    // parchment, a large textured batch at exactly 51000, shows that one is.
    constexpr uint32_t kMenuParchmentZ = 51000u;

    constexpr bool isMenuLayerZ(uint32_t z)
    {
        return z >= kMenuParchmentZ && z < kHudZ;
    }

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

    constexpr uint32_t kPathVu1 = 1u;

    class HudRewriter final : public GifRewriter
    {
    public:
        // Only 3D goes through VU1.
        bool wantsPacket(uint32_t path) override { return path != kPathVu1; }

        void endFrame() override
        {
            // Two frames, as the VIF1 worker can deliver a frame's packets after it is presented.
            m_menuPrevious.store(m_menuUp.exchange(false, std::memory_order_relaxed), std::memory_order_relaxed);
        }

        void rewrite(uint32_t path, uint8_t *data, uint32_t sizeBytes) override
        {
            uint64_t &prim = m_prim[path & 3u];
            size_t offset = 0u;
            while (offset + 16u <= sizeBytes)
            {
                uint64_t tagLo;
                uint64_t tagHi;
                std::memcpy(&tagLo, data + offset, sizeof(tagLo));
                std::memcpy(&tagHi, data + offset + 8u, sizeof(tagHi));
                offset += 16u;

                const uint32_t nloop = static_cast<uint32_t>(tagLo & 0x7FFFu);
                const uint32_t format = static_cast<uint32_t>((tagLo >> 58u) & 3u);
                uint32_t nreg = static_cast<uint32_t>((tagLo >> 60u) & 0xFu);
                if (nreg == 0u)
                    nreg = 16u;
                if ((tagLo >> 46u) & 1u)
                    prim = (tagLo >> 47u) & 0x7FFu;

                m_vertices.clear();
                if (format == 0u)
                {
                    if (!collectPacked(data, sizeBytes, offset, nloop, nreg, tagHi, prim))
                        return;
                }
                else if (format == 1u)
                {
                    if (!collectRegList(data, sizeBytes, offset, nloop, nreg, tagHi, prim))
                        return;
                }
                else
                {
                    offset += static_cast<size_t>(nloop) * 16u;
                }
                adjustBatch(data, prim);
            }
        }

    private:
        struct Vertex
        {
            size_t xOffset;
            size_t yOffset;
            uint32_t z;
        };

        static bool isXyz(uint32_t reg) { return reg == 5u || reg == 13u; }  // XYZ2, XYZ3
        static bool isXyzf(uint32_t reg) { return reg == 4u || reg == 12u; } // XYZF2, XYZF3

        bool collectPacked(const uint8_t *data, uint32_t sizeBytes, size_t &offset, uint32_t nloop, uint32_t nreg,
                           uint64_t tagHi, uint64_t &prim)
        {
            constexpr uint32_t kAddressData = 14u;
            for (uint32_t i = 0u; i < nloop; ++i)
            {
                for (uint32_t r = 0u; r < nreg; ++r, offset += 16u)
                {
                    if (offset + 16u > sizeBytes)
                        return false;
                    const uint32_t desc = static_cast<uint32_t>((tagHi >> (r * 4u)) & 0xFu);
                    uint32_t w[4];
                    std::memcpy(w, data + offset, sizeof(w));
                    if (isXyz(desc))
                    {
                        m_vertices.push_back({offset, offset + 4u, w[2]});
                    }
                    else if (isXyzf(desc))
                    {
                        m_vertices.push_back({offset, offset + 4u, (w[2] >> 4u) & 0xFFFFFFu});
                    }
                    else if (desc == 0u)
                    {
                        prim = w[0] & 0x7FFu;
                    }
                    else if (desc == kAddressData)
                    {
                        const uint32_t reg = w[2] & 0xFFu;
                        if (isXyz(reg))
                            m_vertices.push_back({offset, offset + 2u, w[1]});
                        else if (isXyzf(reg))
                            m_vertices.push_back({offset, offset + 2u, w[1] & 0xFFFFFFu});
                        else if (reg == 0u)
                            prim = w[0] & 0x7FFu;
                    }
                }
            }
            return true;
        }

        bool collectRegList(const uint8_t *data, uint32_t sizeBytes, size_t &offset, uint32_t nloop, uint32_t nreg,
                            uint64_t tagHi, uint64_t &prim)
        {
            const uint64_t words = static_cast<uint64_t>(nloop) * nreg;
            for (uint64_t k = 0u; k < words; ++k)
            {
                const size_t at = offset + static_cast<size_t>(k) * 8u;
                if (at + 8u > sizeBytes)
                    return false;
                const uint32_t desc = static_cast<uint32_t>((tagHi >> ((k % nreg) * 4u)) & 0xFu);
                uint32_t w[2];
                std::memcpy(w, data + at, sizeof(w));
                if (isXyz(desc))
                    m_vertices.push_back({at, at + 2u, w[1]});
                else if (isXyzf(desc))
                    m_vertices.push_back({at, at + 2u, w[1] & 0xFFFFFFu});
                else if (desc == 0u)
                    prim = w[0] & 0x7FFu;
            }
            offset += static_cast<size_t>((words + 1u) / 2u) * 16u;
            return true;
        }

        static uint16_t read16(const uint8_t *data, size_t offset)
        {
            uint16_t value;
            std::memcpy(&value, data + offset, sizeof(value));
            return value;
        }

        bool menuActive() const
        {
            return m_menuUp.load(std::memory_order_relaxed) || m_menuPrevious.load(std::memory_order_relaxed);
        }

        // A batch counts only if all its vertices share the depth: 3D very close to the camera can
        // also reach these depths, but its depth varies.
        void adjustBatch(uint8_t *data, uint64_t prim)
        {
            if (m_vertices.empty())
                return;
            const uint32_t z = m_vertices[0].z;
            const bool menuLayer = !isScreenSpaceZ(z) && isMenuLayerZ(z);
            if (!isScreenSpaceZ(z) && !menuLayer)
                return;
            int minX = 0xFFFF;
            int maxX = 0;
            int minY = 0xFFFF;
            int maxY = 0;
            for (const Vertex &vertex : m_vertices)
            {
                if (vertex.z != z)
                    return;
                const int x = read16(data, vertex.xOffset);
                const int y = read16(data, vertex.yOffset);
                minX = std::min(minX, x);
                maxX = std::max(maxX, x);
                minY = std::min(minY, y);
                maxY = std::max(maxY, y);
            }

            // An untextured fill across the whole width is a fade, a tint or the backing band of a menu,
            // and has to keep covering the width. A band that is not the whole height belongs to a
            // menu drawn over a mission (results, memory card), unless it is shaded and runs to the
            // top or bottom edge: that is a tint over the scene, such as the darkening behind a
            // mission's opening lines (rows 200-480, Z 65000).
            const bool textured = ((prim >> 4u) & 1u) != 0u;
            if (!textured && minX <= kScreenLeft && maxX >= kScreenRight)
            {
                const bool shaded = ((prim >> 3u) & 1u) != 0u;
                const bool reachesEdge = minY <= kScreenTop || maxY >= kScreenTop + px(440);
                if (maxY - minY < px(400) && !(shaded && reachesEdge))
                    m_menuUp.store(true, std::memory_order_relaxed);
                return;
            }

            if (menuLayer)
            {
                if (z == kMenuParchmentZ && textured && maxX - minX >= px(400) && maxY - minY >= px(400))
                    m_menuUp.store(true, std::memory_order_relaxed);
                if (!menuActive())
                    return;
            }
            else if (z != kHudZ && z != kDialogTextZ)
            {
                m_menuUp.store(true, std::memory_order_relaxed); // the map's own layers
            }

            for (const Vertex &vertex : m_vertices)
            {
                const int x = read16(data, vertex.xOffset);
                const uint16_t moved = static_cast<uint16_t>(kScreenCentre + (x - kScreenCentre) * 3 / 4);
                std::memcpy(data + vertex.xOffset, &moved, sizeof(moved));
            }
        }

        uint64_t m_prim[4] = {};
        std::vector<Vertex> m_vertices;
        // A menu is up over the mission; only then do its layers count as screen space.
        std::atomic<bool> m_menuUp{false};
        std::atomic<bool> m_menuPrevious{false};
    };

    void applyIfEnabled(PS2Runtime &runtime)
    {
        if (HostDisplay::outputAspect() <= 0.0f)
            return;
        runtime.registerFunction(0x002FCFD0u, hookViewScreenMatrix);
        runtime.registerFunction(0x00263F44u, hookSetClipAngles);
        runtime.registerFunction(0x00129988u, hookFrustumCorners);
        runtime.registerFunction(0x00212F78u, hookReticle);
        runtime.registerFunction(0x002AF5D0u, hookEnemyBars);
        runtime.registerFunction(0x0016609Cu, hookExpText);
        runtime.registerFunction(0x002A5918u, hookTextWidth);

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
        static HudRewriter rewriter;
        ps2xSetGifRewriter(&rewriter);
        std::cerr << "[DrakengardPC] 16:9 mode on" << std::endl;
    }
}

void applyDrakengardWidescreen(PS2Runtime &runtime)
{
    drakengard_widescreen::applyIfEnabled(runtime);
}
