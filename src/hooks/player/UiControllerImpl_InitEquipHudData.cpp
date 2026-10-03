#include "pch.h"
#include "UiControllerImpl_InitEquipHudData.h"

#include <Windows.h>
#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"
#include "TimeCigaretteUiHook.h"
#include "MissionCodeGuard.h"
#include "UiControllerImpl_UpdateEquipHud.h"

namespace
{
    using InitEquipHudData_t = void(__fastcall*)(void* this_);
    static InitEquipHudData_t g_Orig = nullptr;

    static constexpr std::size_t kNoUseListOffset = 0x1A4;
    static constexpr std::size_t kNoUseListCount  = 32;

    bool ClearNoUseListSEH(void* info)
    {
        __try
        {
            std::memset(static_cast<std::uint8_t*>(info) + kNoUseListOffset, 0,
                        kNoUseListCount * sizeof(std::int32_t));
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }
}

static void __fastcall hkInitEquipHudData(void* this_)
{
    if (g_Orig)
        g_Orig(this_);

    TimeCigaretteUi_SetUiController(this_);

    if (MissionCodeGuard::ShouldBypassHooks())
        return;

    void* info = this_ ? EquipHud_ResolveInfo(this_) : nullptr;
    if (!info)
    {
        LogWarn("InitEquipHudData: EquipHudSystem info not resolved, vanilla no-use equip list kept");
        return;
    }

    if (!ClearNoUseListSEH(info))
        LogWarn("InitEquipHudData: clearing no-use equip list at %p faulted, vanilla list kept", info);
}

bool Install_InitEquipHudData()
{
    if (!gAddr.UiControllerImpl_InitEquipHudData)
    {
        LogError("InitEquipHudData: address missing for %s, no-use equip list not cleared",
            GetGameBuildName(gGameBuild));
        return false;
    }
    void* target = ResolveGameAddress(gAddr.UiControllerImpl_InitEquipHudData);
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hkInitEquipHudData),
                             reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("InitEquipHudData: hook failed at %p, no-use equip list not cleared", target);
        return false;
    }
    return true;
}

bool Uninstall_InitEquipHudData()
{
    if (gAddr.UiControllerImpl_InitEquipHudData)
        DisableAndRemoveHook(ResolveGameAddress(gAddr.UiControllerImpl_InitEquipHudData));
    g_Orig = nullptr;
    return true;
}
