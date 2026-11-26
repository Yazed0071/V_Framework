#include <pch.h>

#include "WalkerGearDamageActionImpl_CorrectVelocity.h"

#include <Windows.h>
#include <cstdint>

#include "AddressSet.h"
#include "BattleGearWeapons.h"
#include "HookUtils.h"
#include "WalkerGearBasicActionImpl_CorrectVelocity.h"
#include "log.h"

namespace
{
    using CorrectVelocity_t = void(__fastcall*)(std::uint8_t* action, std::uint32_t index, std::uint8_t* controller, const float* step,
                                                const float* turn);

    constexpr std::size_t  kWorks       = 0xC0;
    constexpr std::size_t  kFirstIndex  = 0xD0;
    constexpr std::size_t  kWorkSize    = 0x38;   // its first byte: the damage state, 0 none
    constexpr std::uint8_t kBulletDamage = 1;     // then BulletBigDamage 2 and BlowDamage 3; from 4 on it is dead or down
    constexpr std::uint8_t kBlowDamage   = 3;

    const std::uint8_t kCorrectVelocityEntry[] = { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x60, 0x48, 0x8B, 0x05 };

    CorrectVelocity_t g_Orig = nullptr;
    void*             g_Target = nullptr;
#ifdef _DEBUG
    ULONGLONG         g_NextLog = 0;   // TEMP probe: the reactions the Battle Gear walked on through
#endif

    std::uint8_t DamageStateSEH(const std::uint8_t* action, std::uint32_t index)
    {
        __try
        {
            const auto* works = *reinterpret_cast<const std::uint8_t* const*>(action + kWorks);
            const auto first = *reinterpret_cast<const std::uint32_t*>(action + kFirstIndex);
            return works ? works[static_cast<std::uint64_t>(index - first) * kWorkSize] : 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    void __fastcall hkCorrectVelocity(std::uint8_t* action, std::uint32_t index, std::uint8_t* controller, const float* step,
                                      const float* turn)
    {
        g_Orig(action, index, controller, step, turn);
        if (!battleGearWeapons::Ridden())
            return;
        const std::uint8_t state = DamageStateSEH(action, index);
        const bool through = state >= kBulletDamage && state <= kBlowDamage;
        if (through)
            BattleGearWalkThrough(index, controller);
#ifdef _DEBUG
        const ULONGLONG now = GetTickCount64();
        if (state != 0 && now >= g_NextLog)
        {
            g_NextLog = now + 250;
            LogDebug("BattleGear hit: a walker's damage action runs state %u, %s", state,
                     through ? "the walk goes on through it" : "the game's own motion plays");
        }
#endif
    }
}

bool Install_WalkerGearDamageActionImpl_CorrectVelocity_Hook()
{
    if (g_Target)
        return true;
    void* target = gAddr.WalkerGearDamageActionImpl_CorrectVelocity ? ResolveGameAddress(gAddr.WalkerGearDamageActionImpl_CorrectVelocity)
                                                                   : nullptr;
    if (!target)
    {
        LogError("BattleGear: walker damage movement address missing on this build, a hit's flinch stops the Battle Gear");
        return false;
    }
    const HookEntry state = CheckHookEntry(target, kCorrectVelocityEntry, sizeof(kCorrectVelocityEntry));
    if (state == HookEntry::Unknown)
    {
        char desc[160] = {};
        DescribeFunctionEntry(target, desc, sizeof(desc));
        LogError("BattleGear: walker damage movement code differs on this build (%s), a hit's flinch stops the Battle Gear", desc);
        return false;
    }
    if (state == HookEntry::ForeignHook)
        LogHookedOnTop("BattleGear: walker damage CorrectVelocity", target);
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hkCorrectVelocity), reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("BattleGear: walker damage movement hook failed, a hit's flinch stops the Battle Gear");
        return false;
    }
    g_Target = target;
    return true;
}

void Uninstall_WalkerGearDamageActionImpl_CorrectVelocity_Hook()
{
    if (!g_Target)
        return;
    DisableAndRemoveHook(g_Target);
    g_Target = nullptr;
    g_Orig = nullptr;
}
