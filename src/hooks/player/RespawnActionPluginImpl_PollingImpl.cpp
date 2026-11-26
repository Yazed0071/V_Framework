#include "pch.h"
#include "RespawnActionPluginImpl_PollingImpl.h"

#include <Windows.h>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "log.h"

namespace
{
    const std::uint8_t kGateCode[] = { 0xC1, 0xE8, 0x08, 0xA8, 0x01, 0x75, 0x12, 0x32, 0xC0 }; // shr eax,8 / test al,1 / jne +0x12 / xor al,al: only online players may respawn
    constexpr std::size_t  kGateLead = 5;
    constexpr std::uint8_t kJne      = 0x75;
    constexpr std::uint8_t kJmp      = 0xEB;

    std::uint8_t* g_Gate       = nullptr;
    bool          g_GateOpen   = false;
    bool          g_GateWarned = false;

    bool WriteByte(std::uint8_t* target, std::uint8_t value)
    {
        DWORD oldProtect = 0;
        if (!VirtualProtect(target, 1, PAGE_EXECUTE_READWRITE, &oldProtect))
            return false;
        *target = value;
        DWORD restored = 0;
        VirtualProtect(target, 1, oldProtect, &restored);
        FlushInstructionCache(GetCurrentProcess(), target, 1);
        return true;
    }

    void WarnGateChanged()
    {
        if (!g_GateWarned)
            LogWarn("SpRespawn: respawn gate at %p was changed by another module, left alone", static_cast<void*>(g_Gate));
        g_GateWarned = true;
    }

    void SetGate(bool open)
    {
        if (!g_Gate || g_GateOpen == open)
            return;

        if (open)
        {
            if (*g_Gate != kJne)
            {
                WarnGateChanged();
                return;
            }
            if (WriteByte(g_Gate, kJmp))
                g_GateOpen = true;
        }
        else
        {
            if (*g_Gate != kJmp)
            {
                WarnGateChanged();
                return;
            }
            if (WriteByte(g_Gate, kJne))
                g_GateOpen = false;
        }
    }
}

void SpRespawn_EnforceMissionGuard()
{
    if (MissionCodeGuard::ShouldBypassHooks())
        SetGate(false);
    else
        SetGate(true);
}

bool Install_RespawnActionPluginImpl_PollingImpl_Patch()
{
    if (g_Gate)
        return true;

    if (!gAddr.RespawnActionPluginImpl_PollingImpl_OnlineGate)
    {
        LogError("SpRespawn: respawn gate address missing on %s, RequestToRespawn stays FOB-only", GetGameBuildName(gGameBuild));
        return false;
    }

    auto* gate = static_cast<std::uint8_t*>(ResolveGameAddress(gAddr.RespawnActionPluginImpl_PollingImpl_OnlineGate));

    std::uint8_t live[sizeof(kGateCode)] = {};
    if (!gate
        || !ReadEntryBytesSEH(gate - kGateLead, live, sizeof(live))
        || std::memcmp(live, kGateCode, sizeof(live)) != 0)
    {
        LogError("SpRespawn: respawn gate code does not match %s, RequestToRespawn stays FOB-only", GetGameBuildName(gGameBuild));
        return false;
    }

    g_Gate = gate;
    SpRespawn_EnforceMissionGuard();
    return true;
}

void Uninstall_RespawnActionPluginImpl_PollingImpl_Patch()
{
    SetGate(false);
    g_GateOpen = false;
    g_Gate = nullptr;
}
