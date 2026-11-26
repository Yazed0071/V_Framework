#include <pch.h>
#include "BasicAction2Impl_State_ChangeStandOrSquat.h"

#include <Windows.h>
#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "AttackActionImpl_SubState_Idle.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "log.h"

// AttackActionImpl::SubState_Idle does not run while a soldier squats down or stands up, so the relaxed arm pose from
// standing stays on layer 1 through those clips unless it is dropped here too.

namespace
{
    using StateProc_t = void(__fastcall*)(std::uint8_t* basicAction, std::uint32_t slot, std::uint32_t proc, void* param);

    constexpr std::uint32_t kProcUpdate    = 0;
    constexpr std::size_t   kSlotBase      = 0x1C8;
    constexpr std::size_t   kRecords       = 0x1C0;
    constexpr std::size_t   kRecordSize    = 0x120;
    constexpr std::size_t   kRecordMotion  = 0x10;
    constexpr std::size_t   kRecordSoldier = 0x18;
    constexpr std::size_t   kSoldierHub    = 0x1A8; // +0x110: the per-soldier state array AttackActionImpl reads too
    constexpr std::size_t   kStateArray    = 0x110;

    const std::uint8_t kPrologue[] = {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x8B, 0xC2, 0x44, 0x8B, 0xDA, 0x48, 0x8B, 0xF9, 0x2B, 0x81, 0xC8, 0x01,
        0x00, 0x00, 0x48, 0x8D, 0x1C, 0xC0, 0x48, 0xC1, 0xE3, 0x05, 0x48, 0x03, 0x99, 0xC0, 0x01, 0x00, 0x00, 0x45, 0x85, 0xC0
    };

    StateProc_t g_Orig       = nullptr;
    void*       g_HookTarget = nullptr;

    std::uintptr_t ChangeStandOrSquatAddr()
    {
        return gAddr.SoldierBasicAction2Impl_State_ChangeStandOrSquat;
    }

    void DropArmPoseSEH(std::uint8_t* basicAction, std::uint32_t slot)
    {
        __try
        {
            std::uint8_t* record = (slot - *reinterpret_cast<std::uint32_t*>(basicAction + kSlotBase)) * kRecordSize
                + *reinterpret_cast<std::uint8_t**>(basicAction + kRecords);
            const std::uint8_t* states = *reinterpret_cast<std::uint8_t**>(*reinterpret_cast<std::uint8_t**>(basicAction + kSoldierHub) + kStateArray);
            SquatIdleRoute_DropArmPose(*reinterpret_cast<void**>(record + kRecordMotion), slot, states, *reinterpret_cast<std::uint32_t*>(record + kRecordSoldier));
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    void __fastcall hk_State_ChangeStandOrSquat(std::uint8_t* basicAction, std::uint32_t slot, std::uint32_t proc, void* param)
    {
        MISSION_GUARD_ORIGINAL_VOID(g_Orig, basicAction, slot, proc, param);
        g_Orig(basicAction, slot, proc, param);
        if (proc == kProcUpdate)
            DropArmPoseSEH(basicAction, slot);
    }
}

bool Install_BasicAction2Impl_State_ChangeStandOrSquat_Hook()
{
    if (g_HookTarget)
        return true;
    const std::uintptr_t addr = ChangeStandOrSquatAddr();
    if (!addr)
        return true;

    auto* target = static_cast<std::uint8_t*>(ResolveGameAddress(addr));
    const HookEntry entry = CheckHookEntry(target, kPrologue, sizeof(kPrologue));
    if (entry == HookEntry::Unknown)
    {
        LogError("SquatIdleRoute: soldier stance change code does not match %s, GZ squat clips keep TPP's arm pose while squatting down",
            GetGameBuildName(gGameBuild));
        return false;
    }
    if (entry == HookEntry::ForeignHook)
        LogHookedOnTop("SquatIdleRoute: BasicAction2Impl::State_ChangeStandOrSquat", target);
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hk_State_ChangeStandOrSquat), reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("SquatIdleRoute: BasicAction2Impl::State_ChangeStandOrSquat hook failed, GZ squat clips keep TPP's arm pose while squatting down");
        return false;
    }
    g_HookTarget = target;
    return true;
}

bool Uninstall_BasicAction2Impl_State_ChangeStandOrSquat_Hook()
{
    if (g_HookTarget)
    {
        DisableAndRemoveHook(g_HookTarget);
        g_HookTarget = nullptr;
        g_Orig = nullptr;
    }
    return true;
}
