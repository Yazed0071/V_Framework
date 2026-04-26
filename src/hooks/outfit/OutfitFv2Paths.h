#pragma once

namespace outfit
{
    // Hooks on the FV2 (face-variant-2) sub-asset loaders:
    //   - LoadPlayerCamoFv2              (gAddr.LoadPlayerCamoFv2 = 0x146863F80)
    //   - LoadPlayerSnakeBlackDiamondFv2 (gAddr.LoadPlayerSnakeBlackDiamondFv2 = 0x146864C80)
    //
    // FV2 paths are a parallel asset slot to the .fpk path — same
    // playerType / playerPartsType keying, separate file. Custom
    // outfits that want a custom FV2 declare it via OutfitDefinition.
    // For outfits that don't enable FV2 customization, our hooks
    // pass through to the vanilla FV2 array (preserving stock
    // behavior).
    bool Install_OutfitFv2Paths_Hooks();
    void Uninstall_OutfitFv2Paths_Hooks();
}
