#pragma once

#include <cstdint>


void Set_SoldierCallSign(std::uint32_t gameObjectId, std::uint8_t callSign);


void Remove_SoldierCallSign(std::uint32_t gameObjectId);


void Clear_SoldierCallSigns();


void Set_SitrepCallSign(std::uint32_t cp, std::uint16_t member, std::uint16_t soldierIndex);   // the CP's unaddressed SITREP names him


void Clear_SitrepCallSign(std::uint32_t cp);


bool Install_SoldierCallSign_Hook();


bool Uninstall_SoldierCallSign_Hook();
