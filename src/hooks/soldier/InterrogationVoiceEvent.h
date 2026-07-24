#pragma once

#include <cstdint>

void Register_InterrogationVoiceEvent(std::uint32_t cpIndex, std::uint32_t eventId);
void Unregister_InterrogationVoiceEvent(std::uint32_t cpIndex);
void Clear_InterrogationVoiceEvents();

bool Install_InterrogationVoiceEvent_Hook();
bool Uninstall_InterrogationVoiceEvent_Hook();
