#pragma once

namespace outfit
{
    // Installs the CamouflageControllerImpl::ExecSuitCorrect hook used to
    // pin the surface-bonus PlayerCamoType for a registered custom outfit
    // that declares `camoBonusType` in its OutfitDefinition. Idempotent.
    bool Install_OutfitCamoBonus_Hook();
    void Uninstall_OutfitCamoBonus_Hook();
}
