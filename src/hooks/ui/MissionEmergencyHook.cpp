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
        g_EmergencyMissionAllowlist.insert(missionCode);
    }
    else
    {
        g_EmergencyMissionAllowlist.erase(missionCode);
    }
}


bool MissionEmergency_IsEnabled(std::uint16_t missionCode)
{
    std::lock_guard<std::mutex> lock(g_AllowlistMutex);
    return g_EmergencyMissionAllowlist.find(missionCode) != g_EmergencyMissionAllowlist.end();
}


void MissionEmergency_ClearAll()
{
    std::lock_guard<std::mutex> lock(g_AllowlistMutex);
    if (!g_EmergencyMissionAllowlist.empty())
    {
        g_EmergencyMissionAllowlist.clear();
    }
}


bool Install_MissionEmergency_Hook()
{
    if (g_MissionEmergencyHookInstalled)
    {
        return true;
    }

    if (!gAddr.GetMissionCodeCategory)
    {
        LogError("MissionEmergency: GetMissionCodeCategory address missing, emergency missions off");
        return false;
    }

    void* target = ResolveGameAddress(gAddr.GetMissionCodeCategory);
    if (!target)
    {
        LogError("MissionEmergency: GetMissionCodeCategory address unresolved, emergency missions off");
        return false;
    }

    if (!CreateAndEnableHook(
        target,
        reinterpret_cast<void*>(&hkGetMissionCodeCategory),
        reinterpret_cast<void**>(&g_OrigGetMissionCodeCategory)))
    {
        LogError("MissionEmergency: GetMissionCodeCategory hook failed, emergency missions off");
        return false;
    }

    g_MissionEmergencyHookInstalled = true;
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

    return true;
}
