#include "pch.h"

#include <Windows.h>
#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "SoldierFaction.h"

namespace
{
    using SearchActiveMember_t = std::uint32_t(__fastcall*)(std::uint8_t* result, const float* center, float radius);

    SearchActiveMember_t g_OrigSearchActiveMember = nullptr;

    std::uint32_t __fastcall hk_SearchActiveMember(std::uint8_t* result, const float* center, float radius)
    {
        MISSION_GUARD_ORIGINAL_RET(g_OrigSearchActiveMember, result, center, radius);

        const std::uint32_t count = g_OrigSearchActiveMember(result, center, radius);
        return SoldierFaction_FilterSoldierSearch(result, count);
    }
}

bool Install_Soldier2Util_SearchActiveMember_Hook()
{
    void* target = ResolveGameAddress(gAddr.Soldier2Util_SearchActiveMember);
    if (!target)
        return false;
    return CreateAndEnableHook(target, reinterpret_cast<void*>(&hk_SearchActiveMember),
                               reinterpret_cast<void**>(&g_OrigSearchActiveMember));
}

bool Uninstall_Soldier2Util_SearchActiveMember_Hook()
{
    void* target = ResolveGameAddress(gAddr.Soldier2Util_SearchActiveMember);
    if (target)
        DisableAndRemoveHook(target);
    g_OrigSearchActiveMember = nullptr;
    return true;
}
