#include <pch.h>

#include <atomic>
#include <cstdint>
#include <mutex>

#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "AddressSet.h"
#include "log.h"

namespace
{
    using GetCurrentMissionCode_t = std::uint16_t(__fastcall*)();
    using GetQuarkSystemTable_t   = std::uint8_t*(__fastcall*)();
    using GetMissionFlags_t       = std::uint32_t(__fastcall*)();

    constexpr std::size_t   kQuarkTableScriptVars = 0x98;   // table+0x98 -> +0x10 -> vars.missionCode
    constexpr std::size_t   kScriptVarsPtr        = 0x10;
    constexpr std::uint32_t kMissionFlagFob       = 1;

    // mov eax,[mission flags] / ret
    const std::uint8_t kGetMissionFlagsCode[] = { 0x8B, 0x05, 0x00, 0x00, 0x00, 0x00, 0xC3 };
    const std::uint8_t kGetMissionFlagsMask[] = { 1, 1, 0, 0, 0, 0, 1 };

    static GetCurrentMissionCode_t g_GetCurrentMissionCode = nullptr;
    static GetQuarkSystemTable_t   g_GetQuarkSystemTable   = nullptr;
    std::atomic<std::uint16_t>     g_LastMissionCode{ 0 };
    std::once_flag                 g_MissionFlagsResolved;
    GetMissionFlags_t              g_GetMissionFlags = nullptr;

    void ResolveMissionFlags()
    {
        auto* code = static_cast<std::uint8_t*>(ResolveGameAddress(gAddr.Mission_GetMissionFlags));
        std::uint8_t live[sizeof(kGetMissionFlagsCode)] = {};
        if (!code || !ReadEntryBytesSEH(code, live, sizeof(live)))
        {
            LogWarn("MissionCodeGuard: mission flags address missing on %s, every mission counts as FOB", GetGameBuildName(gGameBuild));
            return;
        }
        for (std::size_t i = 0; i < sizeof(live); ++i)
        {
            if (kGetMissionFlagsMask[i] && live[i] != kGetMissionFlagsCode[i])
            {
                LogWarn("MissionCodeGuard: mission flags code differs on this build, every mission counts as FOB");
                return;
            }
        }
        g_GetMissionFlags = reinterpret_cast<GetMissionFlags_t>(code);
    }

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

    bool IsFob()
    {
        std::call_once(g_MissionFlagsResolved, ResolveMissionFlags);
        if (!g_GetMissionFlags)
            return true;
        return (g_GetMissionFlags() & kMissionFlagFob) != 0
            || GetCurrentMissionCode() / 10000 == 5;   // TppMission.IsFOBMission
    }
}
