#include <pch.h>
#include "AttackActionImpl_SubState_Idle.h"

#include <Windows.h>
#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "RouteAiImpl_SetSubActionParamWithNodeEvent.h"
#include "log.h"

namespace
{
    using SubStateIdle_t   = void(__fastcall*)(std::uint8_t* attackAction, std::uint32_t slot, std::uint32_t proc, void* param);
    using GetLayerState_t  = const std::uint64_t*(__fastcall*)(void* motion, std::uint64_t* out, std::uint32_t slot, std::uint32_t layer);
    using RequestLayerState_t = void(__fastcall*)(void* motion, std::uint32_t slot, std::uint32_t layer, std::uint64_t state, float blendTime, std::uint32_t flags);

    constexpr std::uint64_t kTagMask       = 0xFFFFFFFFFFFFull;
    constexpr std::uint64_t kSquatDown     = 0xC04CB5C49CF4ull; // stateSquatIdle's squat-down state
    constexpr std::uint64_t kSquatIdle     = 0xF437F80A4E8Aull; // stateSquatIdle
    constexpr std::uint64_t kSquatStandUp  = 0xF637F233D7E0ull;

    constexpr std::uint32_t kBodyLayer     = 0;
    constexpr std::uint32_t kArmLayer      = 1;
    constexpr std::uint32_t kProcUpdateArm = 3; // procs 3 and 4 only keep layer 1's arm pose in line with the relaxed flags

    constexpr std::size_t kSlotBase        = 0x1DC;
    constexpr std::size_t kWorks           = 0x1C0;
    constexpr std::size_t kWorkSize        = 0x2F0;
    constexpr std::size_t kWorkMotion      = 0x190; // the AnimationController over this slot's block
    constexpr std::size_t kControllerUnitCount = 0x08;
    constexpr std::size_t kControllerFirstSlot = 0x0C;
    constexpr std::size_t kControllerUnits     = 0x10;
    constexpr std::size_t kUnitSize            = 0xC0;
    constexpr std::size_t kUnitAnimControl     = 0x70;
    constexpr std::size_t kWorkSoldier     = 0x2A0;
    constexpr std::size_t kSoldierStates   = 0x78;  // +0x110: one 0x28-byte entry per soldier, bit 28 of +0x20 = another action owns layer 1
    constexpr std::size_t kStateArray      = 0x110;
    constexpr std::size_t kStateSize       = 0x28;
    constexpr std::size_t kStateFlags      = 0x20;
    constexpr std::uint32_t kLayerOwnedBit = 28;

    constexpr std::size_t kGetLayerStateSlot     = 0x208 / 8;
    constexpr std::size_t kRequestLayerStateSlot = 0x170 / 8;

    const std::uint8_t kSubStateIdlePrologue[] = {
        0x40, 0x53, 0x56, 0x57, 0x48, 0x83, 0xEC, 0x50, 0x8B, 0xDA, 0x8B, 0xF2, 0x48, 0x8B, 0xF9, 0x2B, 0x99, 0xDC, 0x01, 0x00, 0x00,
        0x48, 0x69, 0xDB, 0xF0, 0x02, 0x00, 0x00, 0x48, 0x03, 0x99, 0xC0, 0x01, 0x00, 0x00, 0x45, 0x85, 0xC0, 0x0F, 0x84
    };
    constexpr std::size_t   kNullStateLoad   = 0x133; // mov r9,[rip+x]: the "no arm pose" state the game requests for soldiers that are not relaxed
    const std::uint8_t      kNullStateOpcode[] = { 0x4C, 0x8B, 0x0D };
    constexpr std::size_t   kBlendTimeLoad   = 0x153; // movss xmm0,[rip+x]: its blend time
    const std::uint8_t      kBlendTimeOpcode[] = { 0xF3, 0x0F, 0x10, 0x05 };

    SubStateIdle_t g_OrigSubStateIdle = nullptr;
    void*          g_HookTarget       = nullptr;
    std::uint64_t  g_NullArmState     = 0;
    float          g_NullArmBlendTime = 0.0f;

    std::uintptr_t SubStateIdleAddr()
    {
        return gAddr.SoldierAttackActionImpl_SubState_Idle;
    }

    bool BytesAtSEH(const std::uint8_t* at, const std::uint8_t* bytes, std::size_t size)
    {
        __try
        {
            return at && std::memcmp(at, bytes, size) == 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    const std::uint8_t* RipTarget(const std::uint8_t* instr, std::size_t opcodeSize)
    {
        std::int32_t disp = 0;
        std::memcpy(&disp, instr + opcodeSize, sizeof(disp));
        return instr + opcodeSize + sizeof(disp) + disp;
    }

    bool IsRelaxedSquat(std::uint64_t state)
    {
        state &= kTagMask;
        return state == kSquatDown || state == kSquatIdle || state == kSquatStandUp;
    }

    // true = layer 0 is the relaxed squat with GZ's clip, so layer 1 gets no arm pose and the clip keeps both arms.
    // TPP's own squat clip needs the pose, so it keeps the game's code.
    bool DropArmPoseSEH(void* motion, std::uint32_t slot, const std::uint8_t* soldierStates, std::uint32_t soldier)
    {
        if (!g_NullArmState)
            return false;
        __try
        {
            const std::uint8_t* first = *reinterpret_cast<const std::uint8_t* const*>(soldierStates + 8);
            const std::uint8_t* state = soldier < *reinterpret_cast<const std::uint32_t*>(soldierStates + 0x10) ? first + soldier * kStateSize : first;
            if ((*reinterpret_cast<const std::uint32_t*>(state + kStateFlags) >> kLayerOwnedBit) & 1)
                return false;

            const auto* controller = static_cast<const std::uint8_t*>(motion);
            const std::uint32_t unit = slot - *reinterpret_cast<const std::uint32_t*>(controller + kControllerFirstSlot);
            if (unit >= *reinterpret_cast<const std::uint32_t*>(controller + kControllerUnitCount))
                return false;
            const std::uint8_t* units = *reinterpret_cast<const std::uint8_t* const*>(controller + kControllerUnits);
            if (!SquatIdleRoute_PlaysGzSquat(*reinterpret_cast<const void* const*>(units + unit * kUnitSize + kUnitAnimControl)))
                return false;

            void** vtable = *reinterpret_cast<void***>(motion);
            const auto getState = reinterpret_cast<GetLayerState_t>(vtable[kGetLayerStateSlot]);
            std::uint64_t out = 0;
            if (!IsRelaxedSquat(*getState(motion, &out, slot, kBodyLayer)))
                return false;
            if (((*getState(motion, &out, slot, kArmLayer) ^ g_NullArmState) & kTagMask) != 0)
                reinterpret_cast<RequestLayerState_t>(vtable[kRequestLayerStateSlot])(motion, slot, kArmLayer, g_NullArmState, g_NullArmBlendTime, 0);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool DropArmPoseInIdleSEH(std::uint8_t* attackAction, std::uint32_t slot)
    {
        __try
        {
            std::uint8_t* work = (slot - *reinterpret_cast<std::uint32_t*>(attackAction + kSlotBase)) * kWorkSize
                + *reinterpret_cast<std::uint8_t**>(attackAction + kWorks);
            const std::uint8_t* states = *reinterpret_cast<std::uint8_t**>(*reinterpret_cast<std::uint8_t**>(attackAction + kSoldierStates) + kStateArray);
            return DropArmPoseSEH(*reinterpret_cast<void**>(work + kWorkMotion), slot, states, *reinterpret_cast<std::uint32_t*>(work + kWorkSoldier));
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void __fastcall hk_SubState_Idle(std::uint8_t* attackAction, std::uint32_t slot, std::uint32_t proc, void* param)
    {
        MISSION_GUARD_ORIGINAL_VOID(g_OrigSubStateIdle, attackAction, slot, proc, param);
        if (proc - kProcUpdateArm < 2 && DropArmPoseInIdleSEH(attackAction, slot))
            return;
        g_OrigSubStateIdle(attackAction, slot, proc, param);
    }
}

bool SquatIdleRoute_DropArmPose(void* motion, std::uint32_t slot, const std::uint8_t* soldierStates, std::uint32_t soldier)
{
    return DropArmPoseSEH(motion, slot, soldierStates, soldier);
}

bool Install_AttackActionImpl_SubState_Idle_Hook()
{
    if (g_HookTarget)
        return true;
    const std::uintptr_t addr = SubStateIdleAddr();
    if (!addr)
        return true;

    auto* target = static_cast<std::uint8_t*>(ResolveGameAddress(addr));
    const HookEntry entry = CheckHookEntry(target, kSubStateIdlePrologue, sizeof(kSubStateIdlePrologue));
    if (entry == HookEntry::Unknown
        || !BytesAtSEH(target + kNullStateLoad, kNullStateOpcode, sizeof(kNullStateOpcode))
        || !BytesAtSEH(target + kBlendTimeLoad, kBlendTimeOpcode, sizeof(kBlendTimeOpcode)))
    {
        LogError("SquatIdleRoute: soldier arm pose code does not match %s, relaxed squat clips keep TPP's left arm pose",
            GetGameBuildName(gGameBuild));
        return false;
    }
    if (entry == HookEntry::ForeignHook)
        LogHookedOnTop("SquatIdleRoute: AttackActionImpl::SubState_Idle", target);
    std::memcpy(&g_NullArmState, RipTarget(target + kNullStateLoad, sizeof(kNullStateOpcode)), sizeof(g_NullArmState));
    std::memcpy(&g_NullArmBlendTime, RipTarget(target + kBlendTimeLoad, sizeof(kBlendTimeOpcode)), sizeof(g_NullArmBlendTime));
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hk_SubState_Idle), reinterpret_cast<void**>(&g_OrigSubStateIdle)))
    {
        LogError("SquatIdleRoute: AttackActionImpl::SubState_Idle hook failed, relaxed squat clips keep TPP's left arm pose");
        return false;
    }
    g_HookTarget = target;
    return true;
}

bool Uninstall_AttackActionImpl_SubState_Idle_Hook()
{
    if (g_HookTarget)
    {
        DisableAndRemoveHook(g_HookTarget);
        g_HookTarget = nullptr;
        g_OrigSubStateIdle = nullptr;
    }
    return true;
}
