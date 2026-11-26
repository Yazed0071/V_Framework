#pragma once

#include <cstdint>

struct SupportHeliFireRequest
{
    bool          hasTarget   = false;
    std::uint32_t targetId    = 0;
    bool          hasPosition = false;
    float         position[3] = {};
};

bool Install_TppHeli2_LuaCommands_Hook();
void Uninstall_TppHeli2_LuaCommands_Hook();

void SupportHeliFire_SetPending(const SupportHeliFireRequest& request);
bool SupportHeliFire_TakeHandled();
