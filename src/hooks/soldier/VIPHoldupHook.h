#pragma once

#include <cstdint>


void Add_VIPHoldupImportantGameObjectId(std::uint32_t gameObjectId, bool isOfficer);


void Remove_VIPHoldupImportantGameObjectId(std::uint32_t gameObjectId);


void Clear_VIPHoldupImportantGameObjectIds();

void Set_UseCustomNonVipHoldupRecovery(bool enabled);
bool Get_UseCustomNonVipHoldupRecovery();


bool Install_VIPHoldup_Hook();


bool Uninstall_VIPHoldup_Hook();