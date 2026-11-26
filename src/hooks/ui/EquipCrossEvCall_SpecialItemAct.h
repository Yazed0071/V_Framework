#pragma once

#include <cstdint>

namespace ItemUse
{
    void OnPlayerFrame(std::uint16_t selectedItem);   // once a frame for the controlled player: Use with the item out
}

bool Install_EquipCrossEvCall_SpecialItemAct_Hook();
void Uninstall_EquipCrossEvCall_SpecialItemAct_Hook();
