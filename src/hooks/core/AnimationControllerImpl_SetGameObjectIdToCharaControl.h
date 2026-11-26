#pragma once
#include <cstdint>

namespace GameObjectMesh
{
    bool SetVisible(std::uint32_t gameObjectId, std::uint32_t meshId, bool visible);
    bool IsVisible(std::uint32_t gameObjectId, std::uint32_t meshId, bool& visible);
    void ClearForNewMission();
}

bool Install_AnimationControllerImpl_SetGameObjectIdToCharaControl_Hook();
void Uninstall_AnimationControllerImpl_SetGameObjectIdToCharaControl_Hook();
