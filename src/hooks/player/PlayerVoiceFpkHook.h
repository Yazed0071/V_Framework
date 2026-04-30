#pragma once

#include <cstdint>

bool Install_PlayerVoiceFpk_Hook();
bool Uninstall_PlayerVoiceFpk_Hook();


void Set_PlayerVoiceFpkPathForType(std::uint32_t playerType, const char* rawPath);


void Clear_PlayerVoiceFpkPathForType(std::uint32_t playerType);

void Clear_AllPlayerVoiceFpkOverrides();