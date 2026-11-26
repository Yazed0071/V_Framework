#pragma once

#include <cstdint>

namespace ChangeFovaOffScreen
{
    void BeginHold(bool everySoldier, std::uint32_t soldierIndex);
    void EndHold(bool everySoldier, std::uint32_t soldierIndex);
    void Cancel(bool everySoldier, std::uint32_t soldierIndex);
    bool AnyHeld();
    void Clear();
}

bool Install_RealizedSoldier2Impl_ExecuteSerially_Hook();
void Uninstall_RealizedSoldier2Impl_ExecuteSerially_Hook();
