#include <pch.h>
#include "AttackActionImpl_ProcessFireControl.h"

#include <Windows.h>
#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "RouteAiImpl_StepEvent.h"
#include "log.h"

namespace
{
    using ProcessFireControl_t = bool(__fastcall*)(void* attackAction, std::uint32_t slot, std::uint8_t* work, const float* target);

    constexpr std::size_t kWorkSoldierIndex = 0x2A0; // the work is per realized slot; this is the soldier it holds
    constexpr std::size_t kWorkNextShot     = 0x1D0;
    constexpr std::size_t kWorkShotInterval = 0x1D4; // the game puts 0.15 s here for turrets each time a new command arrives
    constexpr float       kTimerSpread      = 1.2f;  // each shot reloads the timer to the interval +-20%

    const std::uint8_t kProcessFireControlPrologue[] = {
        0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x48, 0x8D, 0xAC, 0x24, 0x10, 0xFF, 0xFF, 0xFF, 0x48, 0x81, 0xEC, 0xF0, 0x01, 0x00, 0x00
    };

    ProcessFireControl_t g_OrigProcessFireControl = nullptr;
    void*                g_HookTarget             = nullptr;

    std::uintptr_t ProcessFireControlAddr()
    {
        return gAddr.SoldierAttackActionImpl_ProcessFireControl;
    }

    std::uint32_t SoldierIndexSEH(const std::uint8_t* work)
    {
        __try
        {
            return work ? *reinterpret_cast<const std::uint32_t*>(work + kWorkSoldierIndex) : 0xFFFFFFFFu;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0xFFFFFFFFu;
        }
    }

    bool __fastcall hk_ProcessFireControl(void* attackAction, std::uint32_t slot, std::uint8_t* work, const float* target)
    {
        MISSION_GUARD_ORIGINAL_RET(g_OrigProcessFireControl, attackAction, slot, work, target);
        const bool fired = g_OrigProcessFireControl(attackAction, slot, work, target);
        const std::uint32_t soldierIndex = SoldierIndexSEH(work);
        const float interval = AntiAirGunRoute_ShotInterval(soldierIndex);
        if (interval > 0.0f && work)
        {
            float* shotInterval = reinterpret_cast<float*>(work + kWorkShotInterval);
            float* nextShot     = reinterpret_cast<float*>(work + kWorkNextShot);
            *shotInterval = interval;
            if (*nextShot > interval * kTimerSpread)
                *nextShot = interval; // a slower timer left over from the game's own interval
        }
        return fired;
    }
}

bool Install_AttackActionImpl_ProcessFireControl_Hook()
{
    if (g_HookTarget)
        return true;
    const std::uintptr_t addr = ProcessFireControlAddr();
    if (!addr)
        return true;

    auto* target = static_cast<std::uint8_t*>(ResolveGameAddress(addr));
    const HookEntry entry = CheckHookEntry(target, kProcessFireControlPrologue, sizeof(kProcessFireControlPrologue));
    if (entry == HookEntry::Unknown)
    {
        LogError("AntiAirGunRoute: soldier fire control code does not match %s, AttackFireAAG param0 (fire rate) is ignored",
            GetGameBuildName(gGameBuild));
        return false;
    }
    if (entry == HookEntry::ForeignHook)
        LogHookedOnTop("AntiAirGunRoute: AttackActionImpl::ProcessFireControl", target);
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hk_ProcessFireControl), reinterpret_cast<void**>(&g_OrigProcessFireControl)))
    {
        LogError("AntiAirGunRoute: AttackActionImpl::ProcessFireControl hook failed, AttackFireAAG param0 (fire rate) is ignored");
        return false;
    }
    g_HookTarget = target;
    return true;
}

bool Uninstall_AttackActionImpl_ProcessFireControl_Hook()
{
    if (g_HookTarget)
    {
        DisableAndRemoveHook(g_HookTarget);
        g_HookTarget = nullptr;
        g_OrigProcessFireControl = nullptr;
    }
    return true;
}
