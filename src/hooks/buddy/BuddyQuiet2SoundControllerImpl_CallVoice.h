#pragma once

#include <cstdint>

bool Install_BuddyQuiet2SoundControllerImpl_CallVoice_Hook();
void Uninstall_BuddyQuiet2SoundControllerImpl_CallVoice_Hook();

void BuddyQuietVoice_SayAngryLine(void* soundController, std::uint32_t slot);
