#pragma once

namespace outfit
{
    // Installs the CamoufParamInfoImpl::GetCamoufValue hook used to
    // route surface-bonus reads of virtual camo-type ids
    // (kCamoVirtualIdStart..kCamoVirtualIdEnd) to the per-outfit
    // inline `camoBonusValues[]` array. Vanilla camo types (0..116)
    // pass straight through to orig untouched. Idempotent.
    bool Install_OutfitGetCamoufValue_Hook();
    void Uninstall_OutfitGetCamoufValue_Hook();
}
