#pragma once

#include <cstdint>

bool Install_LostHostageDiscovery_Hooks();
bool Uninstall_LostHostageDiscovery_Hooks();

void Add_LostHostageDiscovery(std::uint32_t gameObjectId, int hostageType);
void Remove_LostHostageDiscovery(std::uint32_t gameObjectId);
void Clear_LostHostageDiscovery();
void Dump_LostHostageDiscovery();


void LostHostageDiscovery_OnRadioRequest(void* self, int actionIndex, int stateProc);


bool LostHostageDiscovery_TryOverrideForCallWithRadioType(
    std::uint32_t ownerIndex,
    std::uint8_t radioType,
    std::uint32_t& outOverrideLabel);

bool LostHostageDiscovery_TryConsumeConvertOverride(
    std::uint8_t radioType,
    std::uint32_t& outOverrideLabel);
