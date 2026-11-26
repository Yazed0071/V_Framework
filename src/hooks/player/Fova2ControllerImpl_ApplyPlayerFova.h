#pragma once
#include <cstdint>

namespace PlayerMesh
{
    bool SetVisible(std::uint32_t playerId, std::uint32_t meshId, bool visible);
    bool IsVisible(std::uint32_t playerId, std::uint32_t meshId, bool& visible);
    void ClearForNewMission();

    // hides every mesh of the local player's body, face and outfit parts (true), or shows again only the ones it hid (false);
    // false when the player's models are not there yet. A model reloaded while hidden is hidden too.
    bool SetAllHidden(bool hidden);
    bool AllHidden();
}

bool Install_Fova2ControllerImpl_ApplyPlayerFova_Hook();
void Uninstall_Fova2ControllerImpl_ApplyPlayerFova_Hook();
