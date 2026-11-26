#pragma once

#include <cstdint>

bool Install_MessageArgs_GetValueAsDouble_Hook();
void Uninstall_MessageArgs_GetValueAsDouble_Hook();

bool WideEquipMessages_Prepare();
bool WideEquipMessages_RealEquipId(const std::uint8_t* message, unsigned index, std::int64_t engineId, std::int32_t& realId);
