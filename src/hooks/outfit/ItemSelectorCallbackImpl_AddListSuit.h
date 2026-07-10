#pragma once

#include <cstdint>

namespace outfit
{


    bool Install_OutfitListInject_Hook();
    void Uninstall_OutfitListInject_Hook();

    void SetHeadEquipDecideActive(bool active);

    std::uint8_t VextLookupCellSelector(std::uint16_t flowIndex,
                                        std::uint8_t cellPos);
}
