#pragma once

#include <cstdint>

bool Install_HeliSoundController_Hook();
bool Uninstall_HeliSoundController_Hook();

bool Play_PilotCallVoice(std::uint32_t voiceId, std::uint32_t slot, std::uint32_t voiceType, std::uint32_t param4);
