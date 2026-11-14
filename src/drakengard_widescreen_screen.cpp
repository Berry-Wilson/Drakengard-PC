// 16:9 mode: screen-space art (menus, the HUD, text, the map) keeps its shape in a centred 4:3 layout.
// It is recognised in the GIF stream by its depth and squeezed about the screen's centre.
#include "drakengard_widescreen.h"
#include "runtime/gs/gif_rewriter.h"
#include "runtime/gs/gs_pcsx2_backend.h"

#include <algorithm>
#include <atomic>
#include <cstring>

namespace drakengard_widescreen
{
    namespace
    {
        constexpr uint32_t kPathVu1 = 1u;

        class ScreenArtRewriter final : public GifRewriter
        {
        public:
            // Only 3D goes through VU1. A movie is shown in 4:3 with bars at the sides, so what is
            // drawn over it already has its shape.
            bool wantsPacket(uint32_t path) override { return path != kPathVu1 && !ps2xPcsx2MoviePlaying(); }

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
    }

    void installScreenArtRewriter()
    {
        static ScreenArtRewriter rewriter;
        ps2xSetGifRewriter(&rewriter);
    }
}
