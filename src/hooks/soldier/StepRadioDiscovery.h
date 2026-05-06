#pragma once

#include <cstdint>

bool Install_LostHostageDiscovery_Hooks();
bool Uninstall_LostHostageDiscovery_Hooks();

void Add_LostHostageDiscovery(std::uint32_t gameObjectId, int hostageType);
void Remove_LostHostageDiscovery(std::uint32_t gameObjectId);
void Clear_LostHostageDiscovery();
void Dump_LostHostageDiscovery();


void LostHostageDiscovery_OnRadioRequest(void* self, int actionIndex, int stateProc);


// Resolves the speaker-soldier-keyed pending hostage-discovery override at
// CallWithRadioType time. Returns true and writes outOverrideLabel if the
// speaker has a queued override for one of the four hostage-found radio
// types; the pending entry is consumed on success.
bool LostHostageDiscovery_TryOverrideForCallWithRadioType(
    std::uint32_t ownerIndex,
    std::uint8_t radioType,
    std::uint32_t& outOverrideLabel);

// Convert-label dispatch fallback. The found-hostage radios don't always
// reach CallWithRadioType, so the most-recent OnRadioRequest also stores a
// single-slot override here. Returns true (and writes outOverrideLabel)
// when the slot matches the requested radio type; consumes on success.
bool LostHostageDiscovery_TryConsumeConvertOverride(
    std::uint8_t radioType,
    std::uint32_t& outOverrideLabel);
