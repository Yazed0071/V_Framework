#include <pch.h>
#include "AttackActionImpl_GetAimState.h"

#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "RideVehicleActionImpl_State_IdleAction.h"
#include "log.h"

namespace
{
    using GetAimState_t = std::uint8_t(__fastcall*)(void* attackAction, const void* task, std::uint32_t slot, std::uint32_t soldier,
                                                    std::int32_t stance, std::int32_t gunAimType);

    constexpr std::uint8_t kAimStateMachineGun = 9; // the emplaced machine gun's aim layer, enethew_s_mgn_idl_aim_all

    const std::uint8_t kGetAimStatePrologue[] = {
        0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0x81, 0x80, 0x00, 0x00, 0x00, 0x4C, 0x8B, 0x59,
        0x78, 0x33, 0xDB, 0x48, 0x8B, 0x40, 0x08
    };

    GetAimState_t g_OrigGetAimState = nullptr;
    void*         g_HookTarget      = nullptr;

    std::uint8_t __fastcall hk_GetAimState(void* attackAction, const void* task, std::uint32_t slot, std::uint32_t soldier,
                                           std::int32_t stance, std::int32_t gunAimType)
    {
        MISSION_GUARD_ORIGINAL_RET(g_OrigGetAimState, attackAction, task, slot, soldier, stance, gunAimType);
        if (JeepGunner_IsGunner(soldier))
            return kAimStateMachineGun;
        return g_OrigGetAimState(attackAction, task, slot, soldier, stance, gunAimType);
    }
}

bool Install_AttackActionImpl_GetAimState_Hook()
{
    if (g_HookTarget)
        return true;
    auto* target = static_cast<std::uint8_t*>(ResolveGameAddress(gAddr.SoldierAttackActionImpl_GetAimState));
    if (!target)
        return true;
    const HookEntry entry = CheckHookEntry(target, kGetAimStatePrologue, sizeof(kGetAimStatePrologue));
    if (entry == HookEntry::Unknown)
    {
        LogError("JeepGunner: soldier aim code does not match %s, the jeep gunner aims like a passenger", GetGameBuildName(gGameBuild));
        return false;
    }
    if (entry == HookEntry::ForeignHook)
        LogHookedOnTop("JeepGunner: AttackActionImpl::GetAimState", target);
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hk_GetAimState), reinterpret_cast<void**>(&g_OrigGetAimState)))
    {
        LogError("JeepGunner: AttackActionImpl::GetAimState hook failed, the jeep gunner aims like a passenger");
        return false;
    }
    g_HookTarget = target;
    return true;
}

bool Uninstall_AttackActionImpl_GetAimState_Hook()
{
    if (g_HookTarget)
    {
        DisableAndRemoveHook(g_HookTarget);
        g_HookTarget = nullptr;
        g_OrigGetAimState = nullptr;
    }
    return true;
}
