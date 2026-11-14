// 16:9 mode: reading the packets the game's sprite routines build (see drakengard_widescreen.h).
#include "drakengard_widescreen.h"

namespace drakengard_widescreen
{
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
}
