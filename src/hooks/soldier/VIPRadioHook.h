#pragma once

#include <cstdint>


bool Install_VIPRadio_Hook();


bool Uninstall_VIPRadio_Hook();


void Add_VIPRadioImportantGameObjectId(std::uint32_t gameObjectId, bool isOfficer);


void Add_VIPRadioImportantTarget(std::uint32_t gameObjectId, std::uint16_t soldierIndex, bool isOfficer);


void Remove_VIPRadioImportantGameObjectId(std::uint32_t gameObjectId);


void Clear_VIPRadioImportantGameObjectIds();


bool Try_GetSingleRecentImportantCorpseIndex(std::uint16_t& outSoldierIndex, bool& outIsOfficer);
