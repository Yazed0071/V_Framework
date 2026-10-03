#pragma once

#include <cstdint>

void Register_InterrogationVoiceEvent(std::uint32_t cpIndex, std::uint32_t eventId,
                                      std::uint32_t markerEventId,
                                      const std::uint32_t* labelBases,
                                      std::uint32_t labelBaseCount);
void Clear_InterrogationVoiceEvents();

bool Install_InterrogationVoiceEvent_Hook();
bool Uninstall_InterrogationVoiceEvent_Hook();
