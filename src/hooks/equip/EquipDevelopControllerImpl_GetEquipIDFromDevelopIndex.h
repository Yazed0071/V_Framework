#pragma once

#include <cstdint>

namespace equip
{
    constexpr std::int32_t  kDevelopEquipIdFieldLast = 0xFFF; // develop records keep grade:4 | equipId:12
    constexpr std::uint16_t kDevelopWideMark         = 0xFFF;

    void          NoteWideDevelopRow(std::uint16_t developId, std::uint16_t wideEquipId);
    std::uint16_t DevelopRecordEquipId(const std::uint8_t* record);
}

bool Install_EquipDevelopControllerImpl_GetEquipIDFromDevelopIndex();
void Uninstall_EquipDevelopControllerImpl_GetEquipIDFromDevelopIndex();
