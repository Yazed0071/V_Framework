#include <pch.h>
#include "EquipDevelopCallbackImpl_IsLatestItemTool.h"

#include <Windows.h>
#include <cstdint>

#include "HookUtils.h"
#include "log.h"
#include "AddressSet.h"

namespace
{
    using IsLatestItemTool_t = bool (__fastcall*)(void* self, std::uint16_t equipId);

    constexpr std::uint16_t kFirstLevelItemId = 0x1C2;
    constexpr std::uint16_t kLastLevelItemId  = 0x209;

    IsLatestItemTool_t g_OrigIsLatestItemTool = nullptr;
    bool g_Installed = false;

    bool __fastcall hkIsLatestItemTool(void* self, std::uint16_t equipId)
    {
        if (equipId < kFirstLevelItemId || equipId > kLastLevelItemId) // the owned-level table only has 0x1C2-0x209; a custom Tool row (equipId 0) would read 4 GB past it
            return true;
        return g_OrigIsLatestItemTool(self, equipId);
    }
}

bool Install_EquipDevelopCallbackImpl_IsLatestItemTool()
{
    if (g_Installed)
        return true;

    void* target = ResolveGameAddress(gAddr.EquipDevelopCallbackImpl_IsLatestItemTool);
    if (!target)
    {
        LogError("SupplyDropItemGuard: IsLatestItemTool address missing for this build, sending a custom tool from R&D can crash");
        return false;
    }

    if (!CreateAndEnableHook(target,
                             reinterpret_cast<void*>(&hkIsLatestItemTool),
                             reinterpret_cast<void**>(&g_OrigIsLatestItemTool)))
    {
        LogError("SupplyDropItemGuard: IsLatestItemTool hook failed, sending a custom tool from R&D can crash");
        return false;
    }

    g_Installed = true;
    return true;
}

void Uninstall_EquipDevelopCallbackImpl_IsLatestItemTool()
{
    if (!g_Installed)
        return;

    if (void* target = ResolveGameAddress(gAddr.EquipDevelopCallbackImpl_IsLatestItemTool))
        DisableAndRemoveHook(target);

    g_OrigIsLatestItemTool = nullptr;
    g_Installed = false;
}
