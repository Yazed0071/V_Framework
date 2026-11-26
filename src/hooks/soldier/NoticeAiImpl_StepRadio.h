#pragma once

#include <cstdint>

enum class NoticeRadio : std::uint8_t
{
    None,
    VehicleBlast,     // NoticeNoiseThreatAi: he reports a vehicle, walker gear or heli the player blew up, or the charge that wrecked one
    Decoy,            // NoticeDecoyAi: a VIP-special guard reports a decoy
    NoiseEmergency,   // NoticeNoiseEmergencyAi: a VIP-special guard reports a cry-out
    Vehicle,          // NoticeVehicleAi: a vehicle drove at him
};

void        NoticeAiImpl_StepRadio_OnNodeWake(void* node, std::uint32_t agent);
NoticeRadio NoticeAiImpl_RadioFrom(std::uint32_t soldierIndex, float spot[3]);   // while his notice node's radio step asks for the radio; spot = what he reports
bool        NoticeAiImpl_ThreatNoise(std::uint32_t soldierIndex, std::uint16_t& flags, std::int32_t& noise, bool& wreck);   // inside his NoiseThreat radio step
bool        NoticeAiImpl_HeardNoiseAt(std::uint32_t soldierIndex, float spot[3]);   // inside his NoiseThreat radio step: where the noise he reports was
void        NoticeAiImpl_StepRadio_ClearForReload();
void        Uninstall_NoticeAiImpl_StepRadio();
