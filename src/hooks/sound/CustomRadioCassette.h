#pragma once

#include <cstdint>


bool Install_CustomRadioCassette_Hooks();
bool Uninstall_CustomRadioCassette_Hooks();

bool Register_CustomRadioCassette(
    std::uint32_t gimmickNameHash,
    std::uint32_t fox2PathHash,
    std::uint32_t wwiseEventId,
    std::uint32_t trackNameId,
    const char* fileName);
