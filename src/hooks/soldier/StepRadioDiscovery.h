#pragma once

#include <cstdint>

bool Install_LostHostageDiscovery_Hooks();
bool Uninstall_LostHostageDiscovery_Hooks();

void Add_LostHostageDiscovery(std::uint32_t gameObjectId, int hostageType);
void Remove_LostHostageDiscovery(std::uint32_t gameObjectId);
void Clear_LostHostageDiscovery();
void Dump_LostHostageDiscovery();


bool LostHostageDiscovery_TryOverrideForCallWithRadioType(   // the label for the call StepRadioDiscovery requested for that CP member
    std::uint32_t ownerIndex,
    std::uint8_t radioType,
    std::uint16_t member,
    std::uint32_t& outOverrideLabel);
