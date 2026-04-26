#pragma once

namespace outfit
{
    // Installs hooks on the runtime asset-load pipeline:
    //
    //   gAddr.LoadPlayerPartsParts              = 0x146865F80
    //   gAddr.LoadPlayerPartsFpk                = 0x146866C80
    //   gAddr.LoadPlayerCamoFpk                 = 0x146864180
    //   gAddr.LoadPlayerSnakeBlackDiamondFpk    = 0x146864E30
    //   gAddr.Player2BlockController_LoadPartsNew = 0x1409B3B60
    //
    // Each path-loader hook intercepts the call, checks if the
    // requested playerPartsType is in our custom range (0x40..0x7F)
    // and the playerType matches a registered outfit, and if so
    // returns the registered FoxPath. Otherwise it passthroughs.
    //
    // The Player2BlockController_LoadPartsNew hook normalizes the
    // LoadPartsPlayerInfo struct for custom outfits — clears
    // donor-arm / donor-face fields so the game doesn't mis-route
    // them through vanilla face/arm tables sized for non-custom
    // partsTypes only.
    bool Install_OutfitRuntimeParts_Hooks();
    void Uninstall_OutfitRuntimeParts_Hooks();

    // Force an immediate parts reload using the captured
    // Player2BlockController instance from a prior LoadPartsNew fire.
    // Used by OutfitSupplyDropSetup to make custom-outfit supply-drop
    // selection equip immediately (the vanilla supply-drop submission
    // pipeline doesn't handle custom flowIndices). Returns false if
    // no controller has been captured yet (no LoadPartsNew has fired
    // — typically only happens during very early mission boot).
    bool ForcePartsReload(unsigned char playerType,
                          unsigned char partsType,
                          unsigned char selectorCode);
}
