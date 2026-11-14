// Shared by the files that make up the optional 16:9 mode (see drakengard_widescreen.cpp).
#pragma once

#include "game_overrides.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "ps2_recompiled_functions.h"

#include <cstdint>
#include <vector>

namespace drakengard_widescreen
{
    // The picture is stretched by 4/3, so what should keep its shape is drawn 3/4 as wide.
    constexpr float kNarrow = 3.0f / 4.0f;

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

    // The game's sprite routines open a fresh packet at the buffer this points to and close it before
    // returning, leaving the pointer just past it. The packet is a chain of DMA cnt tags, each
    // followed by a VIF DIRECT of GIF tags.
    constexpr uint32_t kSpritePacketStart = 0x004F50D4u;
    // A sprite as those routines write it: a REGLIST GIF tag of RGBAQ, UV, XYZF2, UV, XYZF2.
    constexpr uint64_t kSpriteRegs = 0x43431ull;

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

    // The batches of a packet between two values of kSpritePacketStart (drakengard_widescreen_packet.cpp).
    std::vector<PacketBatch> readPacket(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime, uint32_t start, uint32_t end);
    // Moves the x at address to 3/4 of its distance from anchor.
    void narrowAbout(uint8_t *rdram, R5900Context *ctx, PS2Runtime *runtime, uint32_t address, int anchor);

    // Which edge of the screen the HUD element being drawn right now belongs at
    // (drakengard_widescreen_hud.cpp). Marks in the world drawn from inside an element set it aside.
    enum class Edge
    {
        None,
        Left,
        Right
    };
    extern Edge g_hudEdge;

    void registerWorldMarkHooks(PS2Runtime &runtime); // drakengard_widescreen_marks.cpp
    void registerHudHooks(PS2Runtime &runtime);       // drakengard_widescreen_hud.cpp
    void installScreenArtRewriter();                  // drakengard_widescreen_screen.cpp
}
