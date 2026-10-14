#pragma once

#include <cstdint>

namespace outfit
{
    bool Install_OutfitPalettePack();
    void Uninstall_OutfitPalettePack();
    bool Install_OutfitPalette();
    void Uninstall_OutfitPalette();
    bool IsOutfitPaletteReady();
    void ResetOutfitPaletteForMission();

    constexpr const char* kOutfitPalettePack = "/Assets/tpp/pack/V_FrameWork/V_OutfitPalette.fpk";
}
