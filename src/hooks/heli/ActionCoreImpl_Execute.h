#pragma once

#include <cstdint>

bool Install_ActionCoreImpl_Execute_Hook();
bool Uninstall_ActionCoreImpl_Execute_Hook();

void HeliDamageEffects_SetEnabled(std::uint32_t heliKey, bool enabled);
void HeliRoomLight_SetMode(std::uint32_t heliKey, std::int32_t mode);
void HeliActionCore_Clear();
