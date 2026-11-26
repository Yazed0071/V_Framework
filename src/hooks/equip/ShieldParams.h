#pragma once

#include <cstdint>

namespace ShieldParams
{
    void Set(std::uint32_t equipId, std::uint32_t durability, std::uint32_t level);
}

bool Install_ShieldParams_Hooks();
void Uninstall_ShieldParams_Hooks();
