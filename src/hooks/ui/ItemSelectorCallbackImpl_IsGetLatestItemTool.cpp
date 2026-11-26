#include <pch.h>
#include "ItemSelectorCallbackImpl_IsGetLatestItemTool.h"

#include <Windows.h>
#include <cstdint>

#include "HookUtils.h"
#include "log.h"
#include "AddressSet.h"

namespace
{
    using IsGetLatestItemTool_t = bool (__fastcall*)(std::uint16_t equipId);

    constexpr std::uint16_t kFirstToolId = 0x1C2;
    constexpr std::uint16_t kLastToolId  = 0x1FB;

    IsGetLatestItemTool_t g_OrigIsGetLatestItemTool = nullptr;
    bool g_Installed = false;

    bool __fastcall hkIsGetLatestItemTool(std::uint16_t equipId)
    {
        if (equipId < kFirstToolId || equipId > kLastToolId) // the owned-grade table only has tool slots; a custom item id would read far past it
            return true;
        return g_OrigIsGetLatestItemTool(equipId);
    }
}

bool Install_ItemSelectorCallbackImpl_IsGetLatestItemTool()
{
    if (g_Installed)
        return true;

    void* target = ResolveGameAddress(gAddr.ItemSelector_IsGetLatestItemTool);
    if (!target)
    {
        LogError("SupplyDropItemGuard: IsGetLatestItemTool address missing for this build, a carried custom item can crash the supply drop list");
        return false;
    }

    if (!CreateAndEnableHook(target,
                             reinterpret_cast<void*>(&hkIsGetLatestItemTool),
                             reinterpret_cast<void**>(&g_OrigIsGetLatestItemTool)))
    {
        LogError("SupplyDropItemGuard: IsGetLatestItemTool hook failed, a carried custom item can crash the supply drop list");
        return false;
    }

    g_Installed = true;
    return true;
}

void Uninstall_ItemSelectorCallbackImpl_IsGetLatestItemTool()
{
    if (!g_Installed)
        return;

    if (void* target = ResolveGameAddress(gAddr.ItemSelector_IsGetLatestItemTool))
        DisableAndRemoveHook(target);

    g_OrigIsGetLatestItemTool = nullptr;
    g_Installed = false;
}
