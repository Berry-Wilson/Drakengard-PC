#include "game_overrides.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "ps2_stubs.h"
#include "ps2_recompiled_functions.h"
#include <iostream>

void applyDrakengardWidescreen(PS2Runtime &runtime); // drakengard_widescreen.cpp
void applyDrakengardDrawDistance(PS2Runtime &runtime); // drakengard_draw_distance.cpp

namespace
{
    // func_0027ED44: VSync flag poll (*(0x48ad80) == 0).
    // Instead of busy-spinning until the VBlank handler sets the flag,
    // yield directly to EeScheduler's next VBlank tick.
    void hook_func_0027ED44(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        uint32_t flagAddr = GPR_U32(ctx, 4);
        if (flagAddr && flagAddr + 4 <= PS2_RAM_SIZE)
        {
            WRITE32(flagAddr, 1u);
        }
        SET_GPR_S32(ctx, 2, 0); // VSync wait condition satisfied
        uint32_t ra = GPR_U32(ctx, 31);
        runtime->eeWaitVSyncTicks(1, ra);
    }

    // 0x00289330: 4-instruction qsort comparator passed by pointer in func_00289340,
    // missed by the static analyzer:
    //   0x289330: lw   $v1, 0x0($a0)
    //   0x289334: lw   $v0, 0x0($a1)
    //   0x289338: jr   $ra
    //   0x28933c: subu $v0, $v1, $v0
    void stub_func_00289330(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime)
    {
        (void)runtime;
        uint32_t a0 = GPR_U32(ctx, 4);
        uint32_t a1 = GPR_U32(ctx, 5);
        uint32_t v1 = READ32(a0);
        uint32_t v0 = READ32(a1);
        SET_GPR_S32(ctx, 2, static_cast<int32_t>(SUB32(v1, v0)));
        ctx->pc = GPR_U32(ctx, 31);
    }

    void applyDrakengardOverrides(PS2Runtime &runtime)
    {
        std::cerr << "[DrakengardPC] Applying runtime overrides for Drakengard (USA) [SLUS_207.32]..." << std::endl;

        // Bind un-named / differently-named SDK functions in SLUS_207.32 to the runtime stubs
        runtime.registerFunction(0x00306428u, ps2_stubs::sceCdSearchFile); // sceCdLayerSearchFile
        runtime.registerFunction(0x00306740u, ps2_stubs::sceCdSearchFile); // sceCdSearchFile wrapper
        runtime.registerFunction(0x00306F78u, ps2_stubs::sceCdDiskReady);  // _sceCdDiskReady internal
        // Unnamed sceMpegIsEnd: the recompiled body reads mpeg->sys->isEnd, a field the MPEG
        // stubs don't maintain, so the intro movie quits after a few frames.
        runtime.registerFunction(0x002FE9E0u, ps2_stubs::sceMpegIsEnd);

        runtime.registerFunction(0x0027ED44u, hook_func_0027ED44);
        runtime.registerFunction(0x00289330u, stub_func_00289330);

        applyDrakengardWidescreen(runtime);
        applyDrakengardDrawDistance(runtime);
    }
}

// Register game override for Drakengard (USA) - SLUS_207.32 (entry point 0x00120008)
PS2_REGISTER_GAME_OVERRIDE(
    "Drakengard (USA)",
    "SLUS_207.32",
    0x00120008u,
    0u,
    applyDrakengardOverrides
);
