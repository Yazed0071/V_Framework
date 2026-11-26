#pragma once

#include <cstdint>

bool Install_CcRootAdjustAction_Exec_Hook();
bool Uninstall_CcRootAdjustAction_Exec_Hook();

enum class RootAdjustPinPhase : std::uint8_t
{
    Learn,   // a heli clip plays, keep a copy of its seat param
    Hold,    // any pose, he stays on the seat matrix
};

void RootAdjustPin_Update(std::uint32_t soldierIndex, void* rootAdjustModule, RootAdjustPinPhase phase);   // rootAdjustModule = RealizedSoldier2Impl::GetRootAdjustModule(slot)
void RootAdjustPin_Release(std::uint32_t soldierIndex);   // keeps what was learned, he may come back to the seat
void RootAdjustPin_Forget(std::uint32_t soldierIndex);
void RootAdjustPin_Clear();
