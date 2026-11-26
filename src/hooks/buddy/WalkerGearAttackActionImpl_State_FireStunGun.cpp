#include <pch.h>

#include "WalkerGearAttackActionImpl_State_FireStunGun.h"

#include <Windows.h>
#include <algorithm>
#include <cstdint>

#include "AddressSet.h"
#include "BattleGearSound.h"
#include "BattleGearWeapons.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    using State_t = void(__fastcall*)(void* attack, std::uint32_t walker, std::uint32_t proc);

    constexpr std::uint32_t kProcStart      = 1;
    constexpr std::uint32_t kProcEnd        = 2;
    constexpr std::uint32_t kProcUpdate     = 4;
    constexpr std::size_t   kSystem         = 0x70;
    constexpr std::size_t   kWorks          = 0x90;
    constexpr std::size_t   kFirstWalker    = 0x98;
    constexpr std::size_t   kWorkSize       = 0x2D0;
    constexpr std::size_t   kWorkStep       = 0x144;
    constexpr std::size_t   kWorkWalkerId   = 0x150;
    constexpr std::size_t   kWorkTimer      = 0x1A0;   // the charge time, then the time since the shot
    constexpr std::size_t   kSystemClock    = 0x18;
    constexpr std::size_t   kClockPerWalker = 0x38;    // frame time = this walker's scale * the frame delta
    constexpr std::size_t   kClockFrame     = 0x48;
    constexpr std::uint16_t kLevelOneHeld   = 2;       // steps 2-6 hold a level 1-3 charge, 7 fires
    constexpr std::uint16_t kFullChargeHeld = 6;
    constexpr std::uint16_t kAfterShot      = 9;       // ends when the timer reaches the stun gun's 1 second
    constexpr std::uint16_t kNoStep         = 0xFFFF;
    constexpr float         kStunGunWait    = 1.0f;
    constexpr float         kWaits[3]       = { 1.0f, 2.0f, 3.0f };

    const std::uint8_t kStateEntry[] = { 0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x18, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56,
                                         0x41, 0x57, 0x48, 0x8D, 0xA8 };

    State_t g_Orig = nullptr;
    void*   g_Target = nullptr;

    void StretchWaitSEH(const std::uint8_t* attack, std::uint32_t walker, float wait)   // the game adds a frame after this
    {
        __try
        {
            auto* works = *reinterpret_cast<std::uint8_t* const*>(attack + kWorks);
            const auto first = *reinterpret_cast<const std::uint32_t*>(attack + kFirstWalker);
            if (!works)
                return;
            std::uint8_t* work = works + static_cast<std::uint64_t>(walker - first) * kWorkSize;
            if (*reinterpret_cast<const std::uint16_t*>(work + kWorkStep) != kAfterShot)
                return;
            const auto* system = *reinterpret_cast<const std::uint8_t* const*>(attack + kSystem);
            const auto* clock = *reinterpret_cast<const std::uint8_t* const*>(system + kSystemClock);
            const auto walkerId = *reinterpret_cast<const std::uint32_t*>(work + kWorkWalkerId);
            const float dt = (*reinterpret_cast<const float* const*>(clock + kClockPerWalker))[walkerId]
                           * *reinterpret_cast<const float*>(clock + kClockFrame);
            *reinterpret_cast<float*>(work + kWorkTimer) -= dt * (1.0f - kStunGunWait / wait);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    std::uint16_t StepSEH(const std::uint8_t* attack, std::uint32_t walker)
    {
        __try
        {
            const auto* works = *reinterpret_cast<const std::uint8_t* const*>(attack + kWorks);
            const auto first = *reinterpret_cast<const std::uint32_t*>(attack + kFirstWalker);
            if (!works)
                return kNoStep;
            return *reinterpret_cast<const std::uint16_t*>(works + static_cast<std::uint64_t>(walker - first) * kWorkSize + kWorkStep);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return kNoStep;
        }
    }

    void __fastcall hkState(void* attack, std::uint32_t walker, std::uint32_t proc)
    {
        const bool battleGear = attack && battleGearWeapons::IsBattleGearAttack(attack);
        if (battleGear && proc == kProcUpdate)
            StretchWaitSEH(static_cast<const std::uint8_t*>(attack), walker, kWaits[std::clamp(battleGearWeapons::LastRailShotLevel(), 1, 3) - 1]);
        if (!battleGear)
        {
            g_Orig(attack, walker, proc);
            return;
        }
        battleGearSound::EnterRailState(attack);
        g_Orig(attack, walker, proc);
        battleGearSound::LeaveRailState(proc == kProcEnd);
        if (proc == kProcUpdate)
        {
            const std::uint16_t step = StepSEH(static_cast<const std::uint8_t*>(attack), walker);
            battleGearWeapons::PublishRailCharging(step >= kLevelOneHeld && step <= kFullChargeHeld);
        }
        else if (proc == kProcStart || proc == kProcEnd)
        {
            battleGearWeapons::PublishRailCharging(false);
        }
    }
}

bool Install_WalkerGearAttackActionImpl_State_FireStunGun_Hook()
{
    if (g_Target)
        return true;
    void* target = gAddr.WalkerGearAttackActionImpl_State_FireStunGun ? ResolveGameAddress(gAddr.WalkerGearAttackActionImpl_State_FireStunGun)
                                                                        : nullptr;
    if (!target)
    {
        LogError("BattleGear: walker stun gun state address missing on this build, the rail-gun waits 1 s after every shot");
        return false;
    }
    const HookEntry state = CheckHookEntry(target, kStateEntry, sizeof(kStateEntry));
    if (state == HookEntry::Unknown)
    {
        char desc[160] = {};
        DescribeFunctionEntry(target, desc, sizeof(desc));
        LogError("BattleGear: walker stun gun state code differs on this build (%s), the rail-gun waits 1 s after every shot", desc);
        return false;
    }
    if (state == HookEntry::ForeignHook)
        LogHookedOnTop("BattleGear: walker State_FireStunGun", target);
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hkState), reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("BattleGear: walker stun gun state hook failed, the rail-gun waits 1 s after every shot");
        return false;
    }
    g_Target = target;
    return true;
}

void Uninstall_WalkerGearAttackActionImpl_State_FireStunGun_Hook()
{
    if (!g_Target)
        return;
    DisableAndRemoveHook(g_Target);
    g_Target = nullptr;
    g_Orig = nullptr;
}
