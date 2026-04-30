#pragma once

#include <cstdint>


void Add_CallSignExtraSoldier(std::uint32_t gameObjectId);


void Remove_CallSignExtraSoldier(std::uint32_t gameObjectId);


void Clear_CallSignExtraSoldiers();


bool Install_CallSignExtra_Hook();


bool Uninstall_CallSignExtra_Hook();