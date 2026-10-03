#include "pch.h"
#include "MbTopMenuSelectorEv_UpdateMenuNotice.h"

#include "DeployMissionRegistry.h"

#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "log.h"

namespace
{
    using UpdateMenuNotice_t = void(__fastcall*)(void* selector);

    UpdateMenuNotice_t g_OrigUpdateMenuNotice = nullptr;

    void __fastcall hkUpdateMenuNotice(void* selector)
    {
        std::uint8_t* iface = MissionCodeGuard::ShouldBypassHooks() ? nullptr : deployMission::DeployIface();
        deployMission::NoticeCounts saved;
        const bool counted = iface && deployMission::CountImportantAsStory(iface, saved);
        g_OrigUpdateMenuNotice(selector);
        if (counted)
            deployMission::RestoreNoticeCounts(iface, saved);
    }
}

bool Install_MbTopMenuSelectorEv_UpdateMenuNotice_Hook()
{
    void* target = ResolveGameAddress(gAddr.MbTopMenuSelectorEv_UpdateMenuNotice);
    const bool installed = CreateAndEnableHook(target,
        reinterpret_cast<void*>(&hkUpdateMenuNotice),
        reinterpret_cast<void**>(&g_OrigUpdateMenuNotice));
    if (!installed)
        LogError("Dispatch: iDroid menu notice hook failed, custom dispatch missions off");
    return installed;
}

bool Uninstall_MbTopMenuSelectorEv_UpdateMenuNotice_Hook()
{
    DisableAndRemoveHook(ResolveGameAddress(gAddr.MbTopMenuSelectorEv_UpdateMenuNotice));
    g_OrigUpdateMenuNotice = nullptr;
    return true;
}
