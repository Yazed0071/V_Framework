#include "pch.h"
#include "AttackActionImpl_IsWeaponNoUseInPlaceAction.h"

#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"
#include "MissionCodeGuard.h"

namespace
{
    using IsWeaponNoUseInPlaceAction_t =
        std::uint64_t(__fastcall*)(void* self, void* param2, int param3);

    static IsWeaponNoUseInPlaceAction_t g_Orig       = nullptr;
    static void*                        g_HookTarget = nullptr;

    static std::uint64_t __fastcall hk_IsWeaponNoUseInPlaceAction(void* self, void* param2, int param3)
    {
        if (MissionCodeGuard::ShouldBypassHooks())
            return g_Orig ? g_Orig(self, param2, param3) : 0;

        return 0;
    }
}

bool Install_IsWeaponNoUseInPlaceActionPatch()
{
    if (g_HookTarget)
        return true;

    if (!gAddr.AttackActionImpl_IsWeaponNoUseInPlaceAction)
    {
        LogError("IsWeaponNoUseInPlace: no address for this build, hook off");
        return false;
    }

    void* target = ResolveGameAddress(gAddr.AttackActionImpl_IsWeaponNoUseInPlaceAction);
    if (!target)
    {
        LogError("IsWeaponNoUseInPlace: address did not resolve, hook off");
        return false;
    }

    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hk_IsWeaponNoUseInPlaceAction),
                             reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("IsWeaponNoUseInPlace: hook failed @ %p", target);
        return false;
    }

    g_HookTarget = target;
    return true;
}

void Uninstall_IsWeaponNoUseInPlaceActionPatch()
{
    if (g_HookTarget)
    {
        DisableAndRemoveHook(g_HookTarget);
        g_HookTarget = nullptr;
        g_Orig       = nullptr;
    }
}
