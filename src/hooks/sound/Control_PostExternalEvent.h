#pragma once

#include <cstdint>

bool Install_Control_PostExternalEvent_Hook();
bool Uninstall_Control_PostExternalEvent_Hook();

void Register_CustomTapeLongFilename(std::uint32_t fullNameHash, const char* fullName);

bool IsCustomTapeLongFilenameHookActive();
