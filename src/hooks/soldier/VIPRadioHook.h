#pragma once

#include <cstdint>


bool Install_VIPRadio_Hook();


bool Uninstall_VIPRadio_Hook();


// customDeadBodyLabel: optional StrCode32 speech-label hash. If non-zero, the
// VIP body-found radio for this target plays this label via g_CallImpl,
// overriding both the officer-default and the VipBodyFound radioType swap. 0
// keeps the built-in officer/VIP fallback behavior.
void Add_VIPRadioImportantGameObjectId(std::uint32_t gameObjectId, bool isOfficer, std::uint32_t customDeadBodyLabel = 0);


void Add_VIPRadioImportantTarget(std::uint32_t gameObjectId, std::uint16_t soldierIndex, bool isOfficer, std::uint32_t customDeadBodyLabel = 0);


void Remove_VIPRadioImportantGameObjectId(std::uint32_t gameObjectId);


void Clear_VIPRadioImportantGameObjectIds();


bool Try_GetSingleRecentImportantCorpseIndex(std::uint16_t& outSoldierIndex, bool& outIsOfficer);
