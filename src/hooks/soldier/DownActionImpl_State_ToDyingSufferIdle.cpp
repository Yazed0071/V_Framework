#include <pch.h>
#include "DownActionImpl_State_ToDyingSufferIdle.h"

#include <Windows.h>
#include <cstdint>
#include <cstdio>

#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "SoldierRealizedSlot.h"
#include "SoldierVoiceType.h"
#include "log.h"

namespace
{
    using State_t = std::uint64_t(__fastcall*)(void* downAction, std::uint32_t actionIndex, std::uint32_t proc, void* data);
    using Speak_t = bool(__fastcall*)(void* soundController, std::uint32_t slot, std::uint32_t dialogueEvent, int priority,
                                      std::uint32_t line, float delay);

    constexpr std::uint32_t kProcEnter               = 1;
    constexpr std::size_t   kSoldiers                = 0x78;
    constexpr std::size_t   kWorks                   = 0x90;   // 0x30 per soldier in this action, from the index at +0x98
    constexpr std::size_t   kFirstWorkIndex          = 0x98;
    constexpr std::size_t   kWorkSize                = 0x30;
    constexpr std::size_t   kWorkSoldier             = 0x24;
    constexpr std::size_t   kSoldiersSoundController = 0xA8;
    constexpr std::size_t   kSpeakSlot               = 0x20 / sizeof(void*);
    constexpr std::uint32_t kEnemyVoiceEvent         = 0x95EA16B0u;  // DD_vox_ene
    constexpr std::uint32_t kAbdomenGroan            = 0x1534F4C5u;  // EVD250, pressing on a badly wounded abdomen and groaning
    constexpr std::uint32_t kSitUpGasp               = 0x1334F1FFu;  // EVD270, moving despite near-death agony
    constexpr int           kDamagePriority          = 3;            // what vanilla gives the damage lines
    constexpr float         kNoDelay                 = 0.0f;

    constexpr std::uint8_t kStateEntry[] = {   // both states open the same way
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x8B, 0xC2, 0x48, 0x8B, 0xF9, 0x2B, 0x81, 0x98, 0x00, 0x00, 0x00,
    };

    State_t g_OrigSuffer    = nullptr;
    State_t g_OrigSitUp     = nullptr;
    void*   g_SufferTarget  = nullptr;
    void*   g_SitUpTarget   = nullptr;

    bool FindSoldier(const void* downAction, std::uint32_t actionIndex, const std::uint8_t*& soldiers, std::uint32_t& soldierIndex)
    {
        __try
        {
            const auto* action = static_cast<const std::uint8_t*>(downAction);
            const std::uint8_t* works = *reinterpret_cast<const std::uint8_t* const*>(action + kWorks);
            const std::uint32_t first = *reinterpret_cast<const std::uint32_t*>(action + kFirstWorkIndex);
            soldierIndex = *reinterpret_cast<const std::uint32_t*>(works + (actionIndex - first) * kWorkSize + kWorkSoldier);
            soldiers = *reinterpret_cast<const std::uint8_t* const*>(action + kSoldiers);
            return soldiers != nullptr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool Speak(const std::uint8_t* soldiers, std::uint32_t slot, std::uint32_t line)
    {
        __try
        {
            void* soundController = *reinterpret_cast<void* const*>(soldiers + kSoldiersSoundController);
            const auto speak = (*reinterpret_cast<Speak_t* const*>(soundController))[kSpeakSlot];
            return speak(soundController, slot, kEnemyVoiceEvent, kDamagePriority, line, kNoDelay);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool IsGaspVoice(std::uint32_t voiceType)   // ene_d_fml never recorded it
    {
        return voiceType == SoldierVoiceType::kEneAFml || voiceType == SoldierVoiceType::kEneBFml || voiceType == SoldierVoiceType::kEneCFml;
    }

    void SayDyingLine(const void* downAction, std::uint32_t actionIndex, std::uint32_t line)
    {
        if (MissionCodeGuard::ShouldBypassHooks())
            return;
        const std::uint8_t* soldiers = nullptr;
        std::uint32_t soldierIndex = 0;
        if (!FindSoldier(downAction, actionIndex, soldiers, soldierIndex))
            return;
        const std::uint32_t voiceType = SoldierVoiceType::Of(soldiers, soldierIndex);
        if (line == kSitUpGasp ? !IsGaspVoice(voiceType) : !SoldierVoiceType::IsAdult(voiceType))
            return;
        const std::uint32_t slot = SoldierRealizedSlot::Of(soldiers, soldierIndex);
        if (slot == SoldierRealizedSlot::kNotRealized)
            return;
        const bool said = Speak(soldiers, slot, line);
        LogDebug("VoiceDebug: dying soldier %u %s, %s", soldierIndex, line == kSitUpGasp ? "sits up (EVD270)" : "starts suffering (EVD250)",
                 said ? "said" : "not taken");
    }

    std::uint64_t __fastcall hk_StateToDyingSufferIdle(void* downAction, std::uint32_t actionIndex, std::uint32_t proc, void* data)
    {
        const std::uint64_t result = g_OrigSuffer(downAction, actionIndex, proc, data);
        if (proc == kProcEnter)
            SayDyingLine(downAction, actionIndex, kAbdomenGroan);
        return result;
    }

    std::uint64_t __fastcall hk_StateDyingSufferToDyingSitup(void* downAction, std::uint32_t actionIndex, std::uint32_t proc, void* data)
    {
        const std::uint64_t result = g_OrigSitUp(downAction, actionIndex, proc, data);
        if (proc == kProcEnter)
            SayDyingLine(downAction, actionIndex, kSitUpGasp);
        return result;
    }

    bool HookState(std::uintptr_t address, void* detour, void** original, void** target, const char* what)
    {
        void* fn = ResolveGameAddress(address);
        const HookEntry state = CheckHookEntry(fn, kStateEntry, sizeof(kStateEntry));
        if (state == HookEntry::Unknown)
        {
            char bytes[160] = {};
            DescribeFunctionEntry(fn, bytes, sizeof(bytes));
            LogError("SoldierVoiceSwap: the dying soldier's %s state differs on %s (%s), he keeps vanilla's breathing there", what,
                     GetGameBuildName(gGameBuild), bytes);
            return false;
        }
        if (state == HookEntry::ForeignHook)
        {
            char label[96] = {};
            std::snprintf(label, sizeof(label), "SoldierVoiceSwap: the dying soldier's %s state", what);
            LogHookedOnTop(label, fn);
        }
        if (!CreateAndEnableHook(fn, detour, original))
        {
            LogError("SoldierVoiceSwap: the dying soldier's %s state hook failed at %p, he keeps vanilla's breathing there", what, fn);
            *original = nullptr;
            return false;
        }
        *target = fn;
        return true;
    }
}

bool Install_DownActionImpl_State_ToDyingSufferIdle_Hooks()
{
    if (!gAddr.DownActionImpl_State_ToDyingSufferIdle || !gAddr.DownActionImpl_State_DyingSufferToDyingSitup)
    {
        LogWarn("SoldierVoiceSwap: dying soldier states unknown for %s, no EVD250/EVD270", GetGameBuildName(gGameBuild));
        return true;
    }
    bool ok = true;
    if (!g_SufferTarget)
        ok &= HookState(gAddr.DownActionImpl_State_ToDyingSufferIdle, reinterpret_cast<void*>(&hk_StateToDyingSufferIdle),
                        reinterpret_cast<void**>(&g_OrigSuffer), &g_SufferTarget, "suffering");
    if (!g_SitUpTarget)
        ok &= HookState(gAddr.DownActionImpl_State_DyingSufferToDyingSitup, reinterpret_cast<void*>(&hk_StateDyingSufferToDyingSitup),
                        reinterpret_cast<void**>(&g_OrigSitUp), &g_SitUpTarget, "sit-up");
    return ok;
}

void Uninstall_DownActionImpl_State_ToDyingSufferIdle_Hooks()
{
    if (g_SufferTarget)
        DisableAndRemoveHook(g_SufferTarget);
    if (g_SitUpTarget)
        DisableAndRemoveHook(g_SitUpTarget);
    g_SufferTarget = nullptr;
    g_SitUpTarget  = nullptr;
    g_OrigSuffer   = nullptr;
    g_OrigSitUp    = nullptr;
}
