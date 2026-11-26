#pragma once
#include <cstdint>

namespace SoldierBackupCall
{
    std::uint32_t LabelForCall(std::uint8_t radioType, std::uint16_t member);   // CPR0293/0294/0297/0299 for a guard asking for backup, 0 = keep the type's label
    void          ClearForNewMission();
}

bool Install_SoldierBackupCall_Hooks();
void Uninstall_SoldierBackupCall_Hooks();
