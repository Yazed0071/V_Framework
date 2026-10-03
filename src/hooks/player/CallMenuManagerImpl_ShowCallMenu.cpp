#include "pch.h"
#include "CallMenuManagerImpl_ShowCallMenu.h"

#include <cstdint>

#include "AddressSet.h"
#include "CallMenuManagerImpl_UpdateInShowing.h"
#include "HookUtils.h"
#include "log.h"
#include "MissionCodeGuard.h"

namespace
{
    using ShowCallMenu_t = bool(__fastcall*)(std::uint8_t* callMenuManager, std::uint32_t playerIndex);

    ShowCallMenu_t g_Orig       = nullptr;
    void*          g_HookTarget = nullptr;

    bool __fastcall hk_ShowCallMenu(std::uint8_t* callMenuManager, std::uint32_t playerIndex)
    {
        if (callMenuManager && !MissionCodeGuard::ShouldBypassHooks())
            CallMenuPaging::OpenOnFirstPage(callMenuManager);
        return g_Orig(callMenuManager, playerIndex);
    }
}

bool Install_CallMenuManagerImpl_ShowCallMenu_Hook()
{
    if (g_HookTarget)
        return true;

    if (!gAddr.CallMenuManagerImpl_ShowCallMenu)
    {
        LogError("CallMenu: CallMenuManagerImpl::ShowCallMenu address missing for %s, the menu can open with the last page's column icons",
                 GetGameBuildName(gGameBuild));
        return true;
    }

    void* target = ResolveGameAddress(gAddr.CallMenuManagerImpl_ShowCallMenu);
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hk_ShowCallMenu),
                             reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("CallMenu: CallMenuManagerImpl::ShowCallMenu hook failed at %p, the menu can open with the last page's column icons", target);
        return false;
    }

    g_HookTarget = target;
    return true;
}

void Uninstall_CallMenuManagerImpl_ShowCallMenu_Hook()
{
    if (g_HookTarget)
    {
        DisableAndRemoveHook(g_HookTarget);
        g_HookTarget = nullptr;
        g_Orig       = nullptr;
    }
}
