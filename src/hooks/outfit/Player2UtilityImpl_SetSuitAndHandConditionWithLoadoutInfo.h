#pragma once

#include <cstdint>

namespace outfit
{


    bool Install_OutfitSuitConditionApply_Hook();
    void Uninstall_OutfitSuitConditionApply_Hook();


    bool ForceLiveSuitReload(std::uint8_t playerType,
                             std::uint8_t partsType,
                             std::uint8_t selectorCode,
                             std::uint8_t variantIndex);

    bool ReplayCapturedSuitEquip();
}
