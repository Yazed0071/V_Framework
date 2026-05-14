#pragma once

#include <cstdint>

bool Install_GetGameObjectIdWithIndex();
bool Uninstall_GetGameObjectIdWithIndex();
bool Is_GetGameObjectIdWithIndex_Installed();

bool GetGameObjectIdWithIndex(const char* typeName,
    std::uint32_t index,
    std::uint32_t& gameObjectIdOut);

bool GetSoldierGameObjectIdWithIndex(std::uint32_t soldierIndex,
    std::uint32_t& gameObjectIdOut);