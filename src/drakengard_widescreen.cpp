// Optional 16:9 mode for Drakengard, turned on by PS2X_ASPECT=16:9.
//
// The game always renders a 640x480 image, which the host display stretches to 16:9. So everything
// is drawn 3/4 as wide as it should end up:
//  - 3D: the projection's horizontal scale is reduced, which also shows more of the world. The VU1
//    polygon clipper has its own clip angles, computed from a fixed half width of 320, so those are
//    widened to match or the near ground stops at the old 4:3 edges.
//  - Screen-space art: the GIF stream is rewritten. Each piece is anchored to the left or right edge,
//    or stays in a centred 4:3 layout.
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

    // Every draw here is offset so that (0x6C00, 0x7100) is the top left of the screen. X and Y have
    // 4 fractional bits.
    constexpr int kScreenLeft = 0x6C00;
    constexpr int kScreenTop = 0x7100;
    constexpr int kScreenWidth = 640 * 16;
    constexpr int kScreenCentre = kScreenLeft + kScreenWidth / 2;
    constexpr int kScreenRight = kScreenLeft + kScreenWidth;

    constexpr int px(int pixels) { return pixels * 16; }

    // Screen-space art is drawn with every vertex in a batch at one fixed, very high depth: 65535
    // (HUD, menus), 16777215 (dialog text), 65000 and 49999-50000 (map).
    constexpr uint32_t kMinScreenSpaceZ = 49000u;
    constexpr uint32_t kHudZ = 65535u;
    constexpr uint32_t kDialogTextZ = 16777215u;

    constexpr uint32_t kPathVu1 = 1u;

    enum class Anchor
    {
        Centre,
        Left,
        Right
    };

    class HudRewriter final : public GifRewriter
    {
    public:
        bool wantsPacket(uint32_t path) override
        {
            if (path == kPathVu1)
            {
                // Only 3D goes through VU1, and the menus and logos have none.
                m_draws3D.store(true, std::memory_order_relaxed);
                return false;
            }
            return true;
        }

        void endFrame() override
        {
            // Two frames, as the VIF1 worker can deliver a frame's packets after it is presented.
            m_draws3DPrevious.store(m_draws3D.exchange(false, std::memory_order_relaxed), std::memory_order_relaxed);
            m_overlayUp.store(false, std::memory_order_relaxed);
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

        // A batch counts only if all its vertices share the depth: 3D very close to the camera can
        // also reach these depths, but its depth varies.
        void adjustBatch(uint8_t *data, uint64_t prim)
        {
            if (m_vertices.empty() || m_vertices[0].z < kMinScreenSpaceZ)
                return;
            const uint32_t z = m_vertices[0].z;
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

            // A fill across the whole screen is a fade or a tint, and has to keep covering it.
            const bool textured = ((prim >> 4u) & 1u) != 0u;
            if (!textured && minX <= kScreenLeft && maxX >= kScreenRight)
                return;

            if (z != kHudZ && z != kDialogTextZ)
                m_overlayUp.store(true, std::memory_order_relaxed);

            const Anchor anchor = chooseAnchor(z, (minX + maxX) / 2 - kScreenLeft, (minY + maxY) / 2 - kScreenTop);
            for (const Vertex &vertex : m_vertices)
            {
                const uint16_t moved = static_cast<uint16_t>(move(read16(data, vertex.xOffset), anchor));
                std::memcpy(data + vertex.xOffset, &moved, sizeof(moved));
            }
        }

        // cx and cy are the batch's centre on screen. The game draws each letter and gauge segment
        // as its own batch, so pieces of one element are told apart by where the element sits.
        Anchor chooseAnchor(uint32_t z, int cx, int cy) const
        {
            const bool inMission = m_draws3D.load(std::memory_order_relaxed) ||
                                   m_draws3DPrevious.load(std::memory_order_relaxed);
            // The map screen is one centred layout, and the minimap is drawn before it.
            if (!inMission || m_overlayUp.load(std::memory_order_relaxed))
                return Anchor::Centre;
            if (z == kDialogTextZ)
                return Anchor::Left;
            if (cy < px(130))
                return cx >= px(490) ? Anchor::Right : Anchor::Left; // minimap; dialog portrait, backing
            if (cy >= px(360) && cx >= px(520))
                return Anchor::Right; // kill counter
            if (cy >= px(400) || (cy >= px(240) && cx < px(280)))
                return Anchor::Left; // emblem, gauges, chain counter
            return Anchor::Centre;
        }

        static int move(int x, Anchor anchor)
        {
            switch (anchor)
            {
            case Anchor::Left:
                // The dialog's backing art runs to the right edge, and keeps reaching it.
                return x >= kScreenRight ? x : kScreenLeft + (x - kScreenLeft) * 3 / 4;
            case Anchor::Right:
                return kScreenRight - (kScreenRight - x) * 3 / 4;
            default:
                return kScreenCentre + (x - kScreenCentre) * 3 / 4;
            }
        }

        uint64_t m_prim[4] = {};
        std::vector<Vertex> m_vertices;
        std::atomic<bool> m_draws3D{false};
        std::atomic<bool> m_draws3DPrevious{false};
        std::atomic<bool> m_overlayUp{false};
    };

    void applyIfEnabled(PS2Runtime &runtime)
    {
        if (HostDisplay::outputAspect() <= 0.0f)
            return;
        runtime.registerFunction(0x002FCFD0u, hookViewScreenMatrix);
        runtime.registerFunction(0x00263F44u, hookSetClipAngles);
        static HudRewriter rewriter;
        ps2xSetGifRewriter(&rewriter);
        std::cerr << "[DrakengardPC] 16:9 mode on" << std::endl;
    }
}

void applyDrakengardWidescreen(PS2Runtime &runtime)
{
    drakengard_widescreen::applyIfEnabled(runtime);
}
