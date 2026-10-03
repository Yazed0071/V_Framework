#include "pch.h"
#include "UiControllerImpl_HideBinocle.h"

#include "AddressSet.h"
#include "HookUtils.h"
#include "LuaBroadcaster.h"
#include "MissionCodeGuard.h"
#include "log.h"

namespace
{
    using HideBinocle_t = void(__fastcall*)(void* this_);
    static HideBinocle_t g_Orig = nullptr;

    static void __fastcall hkHideBinocle(void* this_)
    {
        if (g_Orig)
            g_Orig(this_);

        if (MissionCodeGuard::ShouldBypassHooks())
            return;

        V_FrameWork::EmitMessage("Player", "OffBinocularsMode");
    }
}

bool Install_HideBinocle_Hook()
{
    if (!gAddr.UiControllerImpl_HideBinocle)
    {
        LogError("HideBinocle: address missing for this build, OffBinocularsMode message off");
        return false;
    }

    void* target = ResolveGameAddress(gAddr.UiControllerImpl_HideBinocle);
    const bool ok = CreateAndEnableHook(target, reinterpret_cast<void*>(&hkHideBinocle),
                                        reinterpret_cast<void**>(&g_Orig));
    if (!ok)
        LogError("HideBinocle: hook failed, OffBinocularsMode message off");
    return ok;
}

bool Uninstall_HideBinocle_Hook()
{
    if (gAddr.UiControllerImpl_HideBinocle)
        DisableAndRemoveHook(ResolveGameAddress(gAddr.UiControllerImpl_HideBinocle));
    g_Orig = nullptr;
    return true;
}
