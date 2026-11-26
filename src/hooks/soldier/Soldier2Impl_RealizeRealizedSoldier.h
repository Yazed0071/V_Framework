#pragma once
#include <cstdint>

namespace SoldierMesh
{
    bool SetVisible(std::uint32_t soldierId, std::uint32_t meshId, bool visible);
    bool IsVisible(std::uint32_t soldierId, std::uint32_t meshId, bool& visible);
    void ClearForNewMission();
}

bool Install_Soldier2Impl_RealizeRealizedSoldier_Hook();
void Uninstall_Soldier2Impl_RealizeRealizedSoldier_Hook();
