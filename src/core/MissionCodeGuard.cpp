#include "pch.h"

#include <atomic>
#include <cstdint>

#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "AddressSet.h"

namespace
{
    using GetCurrentMissionCode_t = std::uint16_t(__fastcall*)();
    using GetQuarkSystemTable_t   = std::uint8_t*(__fastcall*)();

    constexpr std::size_t kQuarkTableScriptVars = 0x98;   // the game's getter reads table+0x98 -> +0x10 -> vars.missionCode
    constexpr std::size_t kScriptVarsPtr        = 0x10;

    static GetCurrentMissionCode_t g_GetCurrentMissionCode = nullptr;
    static GetQuarkSystemTable_t   g_GetQuarkSystemTable   = nullptr;
    std::atomic<std::uint16_t>     g_LastMissionCode{ 0 };

    bool ScriptVarsExist()
    {
        std::uint8_t* table = g_GetQuarkSystemTable();
        if (!table)
            return false;
        std::uint8_t* holder = *reinterpret_cast<std::uint8_t**>(table + kQuarkTableScriptVars);
        return holder && *reinterpret_cast<std::uint8_t**>(holder + kScriptVarsPtr);
    }
}

namespace MissionCodeGuard
{
    std::uint16_t GetCurrentMissionCode()
    {
        if (!g_GetCurrentMissionCode)
            g_GetCurrentMissionCode = reinterpret_cast<GetCurrentMissionCode_t>(ResolveGameAddress(gAddr.GetCurrentMissionCode));
        if (!g_GetQuarkSystemTable)
            g_GetQuarkSystemTable = reinterpret_cast<GetQuarkSystemTable_t>(ResolveGameAddress(gAddr.GetQuarkSystemTable));
        if (!g_GetCurrentMissionCode || !g_GetQuarkSystemTable)
            return 0;

        if (!ScriptVarsExist())   // the game's getter would read through null; a load briefly drops the vars, and a 0 then reads as a mission change
            return g_LastMissionCode.load(std::memory_order_relaxed);
        const std::uint16_t code = g_GetCurrentMissionCode();
        g_LastMissionCode.store(code, std::memory_order_relaxed);
        return code;
    }

    bool IsMissionBlocked(std::uint16_t missionCode)
    {
        return missionCode == 50050u || missionCode == 50055u;
    }

    bool ShouldBypassHooks()
    {
        return IsMissionBlocked(GetCurrentMissionCode());
    }
}
