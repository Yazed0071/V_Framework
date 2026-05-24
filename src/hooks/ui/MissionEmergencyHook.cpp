#include "pch.h"
#include "MissionEmergencyHook.h"

#include <Windows.h>
#include <cstdint>
#include <mutex>
#include <unordered_set>

#include "HookUtils.h"
#include "log.h"
#include "AddressSet.h"

namespace
{
    using GetMissionCodeCategory_t = std::uint8_t (__fastcall*)(std::uint16_t missionCode);

    static GetMissionCodeCategory_t g_OrigGetMissionCodeCategory = nullptr;

    static std::mutex                       g_AllowlistMutex;
    static std::unordered_set<std::uint16_t> g_EmergencyMissionAllowlist;

    static std::mutex                       g_SortiePrepMutex;
    static std::unordered_set<std::uint16_t> g_MissionsWithSortiePrepDisabled;

    static bool g_MissionEmergencyHookInstalled = false;

    static constexpr std::uint8_t kEmergencyCategory = 4;
}


static std::uint8_t __fastcall hkGetMissionCodeCategory(std::uint16_t missionCode)
{
    bool allowlistHit = false;
    {
        std::lock_guard<std::mutex> lock(g_AllowlistMutex);
        allowlistHit = (g_EmergencyMissionAllowlist.find(missionCode) !=
                        g_EmergencyMissionAllowlist.end());
    }

    if (allowlistHit)
        return kEmergencyCategory;

    if (!g_OrigGetMissionCodeCategory)
        return 0;

    return g_OrigGetMissionCodeCategory(missionCode);
}


void MissionEmergency_SetEnabled(std::uint16_t missionCode, bool enabled)
{
    std::lock_guard<std::mutex> lock(g_AllowlistMutex);

    if (enabled)
    {
        const auto inserted = g_EmergencyMissionAllowlist.insert(missionCode);
        Log("[MissionEmergency] MissionEmergency_SetEnabled mc=%u enabled=true %s (allowlist size=%zu)\n",
            static_cast<unsigned>(missionCode),
            inserted.second ? "ADDED" : "already present",
            g_EmergencyMissionAllowlist.size());
    }
    else
    {
        const auto erased = g_EmergencyMissionAllowlist.erase(missionCode);
        Log("[MissionEmergency] MissionEmergency_SetEnabled mc=%u enabled=false %s (allowlist size=%zu)\n",
            static_cast<unsigned>(missionCode),
            erased > 0 ? "REMOVED" : "was not present",
            g_EmergencyMissionAllowlist.size());
    }
}


bool MissionEmergency_IsEnabled(std::uint16_t missionCode)
{
    std::lock_guard<std::mutex> lock(g_AllowlistMutex);
    return g_EmergencyMissionAllowlist.find(missionCode) != g_EmergencyMissionAllowlist.end();
}


void MissionEmergency_SetSortiePrepEnabled(std::uint16_t missionCode, bool enabled)
{
    std::lock_guard<std::mutex> lock(g_SortiePrepMutex);
    if (enabled)
    {
        const auto erased = g_MissionsWithSortiePrepDisabled.erase(missionCode);
        if (erased > 0)
        {
            Log("[MissionEmergency] sortie prep RE-ENABLED for mc=%u\n",
                static_cast<unsigned>(missionCode));
        }
    }
    else
    {
        const auto inserted = g_MissionsWithSortiePrepDisabled.insert(missionCode);
        if (inserted.second)
        {
            Log("[MissionEmergency] sortie prep DISABLED for mc=%u (will use IH-style direct ReserveMissionClear)\n",
                static_cast<unsigned>(missionCode));
        }
    }
}


bool MissionEmergency_IsSortiePrepEnabled(std::uint16_t missionCode)
{
    std::lock_guard<std::mutex> lock(g_SortiePrepMutex);
    return g_MissionsWithSortiePrepDisabled.find(missionCode) ==
           g_MissionsWithSortiePrepDisabled.end();
}


void MissionEmergency_ClearAll()
{
    std::lock_guard<std::mutex> lock(g_AllowlistMutex);
    if (!g_EmergencyMissionAllowlist.empty())
    {
        g_EmergencyMissionAllowlist.clear();
        Log("[MissionEmergency] allowlist cleared\n");
    }
}


bool Install_MissionEmergency_Hook()
{
    if (g_MissionEmergencyHookInstalled)
    {
        Log("[MissionEmergency] hook already installed\n");
        return true;
    }

    if (!gAddr.GetMissionCodeCategory)
    {
        Log("[MissionEmergency] GetMissionCodeCategory address is 0 for this build; skipping\n");
        return false;
    }

    void* target = ResolveGameAddress(gAddr.GetMissionCodeCategory);
    if (!target)
    {
        Log("[MissionEmergency] target resolve failed\n");
        return false;
    }

    if (!CreateAndEnableHook(
        target,
        reinterpret_cast<void*>(&hkGetMissionCodeCategory),
        reinterpret_cast<void**>(&g_OrigGetMissionCodeCategory)))
    {
        Log("[MissionEmergency] hook install failed\n");
        return false;
    }

    g_MissionEmergencyHookInstalled = true;
    Log("[MissionEmergency] hook installed at 0x%llX\n",
        static_cast<unsigned long long>(gAddr.GetMissionCodeCategory));
    return true;
}


bool Uninstall_MissionEmergency_Hook()
{
    if (!g_MissionEmergencyHookInstalled)
        return true;

    if (gAddr.GetMissionCodeCategory)
    {
        void* target = ResolveGameAddress(gAddr.GetMissionCodeCategory);
        if (target)
            DisableAndRemoveHook(target);
    }

    g_OrigGetMissionCodeCategory = nullptr;
    g_MissionEmergencyHookInstalled = false;

    MissionEmergency_ClearAll();

    Log("[MissionEmergency] hook removed\n");
    return true;
}
