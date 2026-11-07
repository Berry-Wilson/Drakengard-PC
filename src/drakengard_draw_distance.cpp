// Optional longer draw distance for Drakengard, turned on by PS2X_DRAW_DISTANCE=<multiplier>.
//
// The visible limit is fog: the game still draws the level well past the point where the fog turns
// it solid. This multiplies the fog start and end while the frame's GS setup is built, which is the
// only place they become fog settings for VU1. Other code reads and writes the same fields to blend
// between fog settings, so the real values go back afterwards.
#include "game_overrides.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "ps2_recompiled_functions.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>

namespace drakengard_draw_distance
{
    constexpr uint32_t kCamera = 0x480B00u;
    constexpr uint32_t kFogStart = kCamera + 0x29Cu;
    constexpr uint32_t kFogEnd = kCamera + 0x2A0u;

    float g_multiplier = 1.0f;

    float readFloat(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime, uint32_t address)
    {
        const uint32_t bits = READ32(address);
        float value;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }

    void writeFloat(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime, uint32_t address, float value)
    {
        uint32_t bits;
        std::memcpy(&bits, &value, sizeof(bits));
        WRITE32(address, bits);
    }

    void hookFrameGsSetup(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        const float start = readFloat(rdram, ctx, runtime, kFogStart);
        const float end = readFloat(rdram, ctx, runtime, kFogEnd);
        writeFloat(rdram, ctx, runtime, kFogStart, start * g_multiplier);
        writeFloat(rdram, ctx, runtime, kFogEnd, end * g_multiplier);
        func_00251DEC_0x251dec(rdram, ctx, runtime);
        writeFloat(rdram, ctx, runtime, kFogStart, start);
        writeFloat(rdram, ctx, runtime, kFogEnd, end);
    }
}

void applyDrakengardDrawDistance(PS2Runtime &runtime)
{
    using namespace drakengard_draw_distance;
    const char *value = std::getenv("PS2X_DRAW_DISTANCE");
    if (!value || !value[0])
        return;
    const float multiplier = static_cast<float>(std::atof(value));
    if (!std::isfinite(multiplier) || multiplier <= 1.0f)
    {
        std::cerr << "[DrakengardPC] ignoring PS2X_DRAW_DISTANCE=" << value << ", it must be above 1" << std::endl;
        return;
    }
    g_multiplier = multiplier;
    runtime.registerFunction(0x00251DECu, hookFrameGsSetup);
    std::cerr << "[DrakengardPC] draw distance x" << multiplier << std::endl;
}
