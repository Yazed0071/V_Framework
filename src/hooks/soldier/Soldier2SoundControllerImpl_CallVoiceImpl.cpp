#include <pch.h>
#include "Soldier2SoundControllerImpl_CallVoiceImpl.h"

#include <Windows.h>
#include <atomic>
#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "CpRadioVariants.h"
#include "MissionCodeGuard.h"
#include "SoldierCombatVoice.h"
#include "log.h"

namespace
{
    using CallVoiceImpl_t = bool(__fastcall*)(void* self, std::uint32_t slot, std::uint32_t soldierIndex, std::uint32_t dialogueEvent,
                                              std::uint32_t priority, std::uint32_t line, bool flag, float delay);

    constexpr std::uint32_t kEnemyStateVoiceEvent = 0xE3BD7BDDu;  // the enemy dialogue event for breaths and groans (real name unknown)
    constexpr std::uint32_t kYawn                 = 0xEE81CE9Au;  // its key of the yawn the soldier motions play (real name unknown)
    constexpr std::uint32_t kSecondYawn           = 0xEE81CE99u;  // its key of the second yawn take (real name unknown)

    constexpr std::uint8_t kCallVoiceImplEntry[] = {
        0x48, 0x89, 0x6C, 0x24, 0x20, 0x57, 0x41, 0x54, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x40, 0x48, 0x8B,
        0xE9, 0x48, 0x8B, 0x89, 0xC8, 0x00, 0x00, 0x00, 0x44, 0x8B, 0xE2, 0x48, 0x8B, 0x01, 0xB2, 0x15,
    };

    CallVoiceImpl_t            g_Orig   = nullptr;
    void*                      g_Target = nullptr;
    std::atomic<std::uint32_t> g_Roll{ 0x6B43A9B5u };

    bool CoinFlip()
    {
        std::uint32_t roll = g_Roll.load(std::memory_order_relaxed);
        roll ^= roll << 13;
        roll ^= roll >> 17;
        roll ^= roll << 5;
        g_Roll.store(roll, std::memory_order_relaxed);
        return (roll & 1) != 0;
    }

    bool __fastcall hk_CallVoiceImpl(void* self, std::uint32_t slot, std::uint32_t soldierIndex, std::uint32_t dialogueEvent,
                                     std::uint32_t priority, std::uint32_t line, bool flag, float delay)
    {
        if (line == kYawn && dialogueEvent == kEnemyStateVoiceEvent && !MissionCodeGuard::ShouldBypassHooks() && CoinFlip())
        {
            line = kSecondYawn;
        }
#ifdef _DEBUG
        SoldierCombatVoice_ProbeVoiceCall(soldierIndex, dialogueEvent, priority, line);   // TEMP probe: what cuts EVE020 off
        std::uint32_t surveyCp = 0;
        if (CpRadioVariants_ProbeSurveyCp(soldierIndex, surveyCp))   // TEMP probe: what a survey team says during its sweep
            LogDebug("SurveyProbe: cp %u: soldier %u says 0x%08X (event 0x%08X, priority %u)", surveyCp, soldierIndex, line, dialogueEvent, priority);
#endif
        return g_Orig(self, slot, soldierIndex, dialogueEvent, priority, line, flag, delay);
    }
}

bool Install_Soldier2SoundControllerImpl_CallVoiceImpl_Hook()
{
    if (g_Target)
        return true;
    if (!gAddr.Soldier2SoundControllerImpl_CallVoiceImpl)
    {
        LogWarn("SoldierVoiceSwap: CallVoiceImpl unknown for %s, soldiers keep their one yawn", GetGameBuildName(gGameBuild));
        return true;
    }

    void* target = ResolveGameAddress(gAddr.Soldier2SoundControllerImpl_CallVoiceImpl);
    const HookEntry state = CheckHookEntry(target, kCallVoiceImplEntry, sizeof(kCallVoiceImplEntry));
    if (state == HookEntry::Unknown)
    {
        char entry[160] = {};
        DescribeFunctionEntry(target, entry, sizeof(entry));
        LogError("SoldierVoiceSwap: CallVoiceImpl differs on %s (%s), soldiers keep their one yawn", GetGameBuildName(gGameBuild), entry);
        return false;
    }
    if (state == HookEntry::ForeignHook)
        LogHookedOnTop("SoldierVoiceSwap: Soldier2SoundControllerImpl::CallVoiceImpl", target);

    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hk_CallVoiceImpl), reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("SoldierVoiceSwap: CallVoiceImpl hook failed at %p, soldiers keep their one yawn", target);
        g_Orig = nullptr;
        return false;
    }
    g_Target = target;
    return true;
}

void Uninstall_Soldier2SoundControllerImpl_CallVoiceImpl_Hook()
{
    if (g_Target)
        DisableAndRemoveHook(g_Target);
    g_Target = nullptr;
    g_Orig   = nullptr;
}
