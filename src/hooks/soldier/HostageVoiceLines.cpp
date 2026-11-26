#include <pch.h>
#include "HostageVoiceLines.h"

#include <Windows.h>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "log.h"

namespace
{
    using CallVoiceImpl_t    = bool(__fastcall*)(void* soundController, std::uint32_t slot, std::uint32_t hostageIndex, std::uint32_t dialogueEvent,
                                                 std::uint32_t priority, std::uint32_t line, float repeatWindow, bool flag);
    using UpdateSleepVoice_t = void(__fastcall*)(void* actionCore, std::uint32_t slot, std::uint32_t eliIndex);
    using StateCarried_t     = void(__fastcall*)(void* carriedAction, std::uint32_t slot, int proc, void* arg);
    using StateSmokeStart_t  = void(__fastcall*)(void* damageAction, std::uint32_t slot, int proc, void* arg);
    using EliSay_t           = void(__fastcall*)(void* soundController, std::uint32_t slot, std::uint32_t dialogueEvent, std::uint32_t priority,
                                                 std::uint32_t line);
    using EliSaying_t        = std::uint32_t(__fastcall*)(void* soundController, std::uint32_t slot);

    const std::uint8_t kCallVoiceImplEntry[]    = { 0x48, 0x89, 0x5C, 0x24, 0x18, 0x55, 0x57, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x40,
                                                    0xF3, 0x0F, 0x10, 0x8C, 0x24, 0x90, 0x00, 0x00, 0x00 };  // movss xmm1,[rsp+0x90] = the repeat window
    const std::uint8_t kUpdateSleepVoiceEntry[] = { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x30,
                                                    0x4C, 0x8B, 0x89, 0x80, 0x00, 0x00, 0x00 };             // mov r9,[rcx+0x80] = Eli's system
    const std::uint8_t kStateCarriedEntry[]     = { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18,
                                                    0x57, 0x48, 0x83, 0xEC, 0x50, 0x48, 0x8B, 0xA9, 0x90, 0x00, 0x00, 0x00 };
    const std::uint8_t kStateSmokeStartEntry[]  = { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x30,
                                                    0x8B, 0xDA, 0x8B, 0xF2, 0x48, 0x8B, 0xF9, 0x2B, 0x99, 0x90, 0x00, 0x00, 0x00 };

    constexpr std::uint32_t kMisstepLine        = 0x9F6624FDu;  // POWV_0171 "Wh-whoa!"
    constexpr std::uint32_t kFallToDeathLine    = 0xA066266Au;  // POWV_0165
    constexpr std::uint32_t kMisstepFallLine    = 0x9F6624FEu;  // POWV_0172 "Agh! Aaaaaaaagh!", the misstep's second half
    constexpr std::uint32_t kNoMisstepFallEvent = 0x58388E09u;  // the one hostage voice set without POWV_0172
    constexpr std::uint64_t kMisstepFallMs      = 3000;

    constexpr std::uint32_t kEliEvent           = 0x57488340u;  // vox_eli
    constexpr std::uint32_t kEliTranqHitLine    = 0x3F50203Eu;  // LIQV_0190 "Hrk! Uhhhhhhh..."
    constexpr std::uint32_t kEliDozingLine      = 0x3F50203Fu;  // LIQV_0191 "Mmm? ...Mmmmm..."
    constexpr std::uint32_t kEliSleepLine       = 0x3E525D22u;  // LIQV_0200, the sleeping breath loop
    constexpr std::uint32_t kEliWakingLine      = 0x3F525E95u;  // LIQV_0210 "Mmh... Nnngh..."
    constexpr std::uint32_t kEliLiftedLine      = 0x38501539u;  // LIQV_0100 "Hnng... agh!"
    constexpr std::uint32_t kEliCarriedLine     = 0x37501346u;  // LIQV_0110 "Huh... huh... huh..."
    constexpr std::uint32_t kEliCoughLine       = 0x40502191u;  // LIQV_0180 "*cough* *cough*", the other take of his LIQV_0390 smoke line
    constexpr std::uint32_t kEliPriority        = 4;
    constexpr std::uint64_t kEliDozingMs        = 2500;
    constexpr std::uint64_t kEliCarriedBreathMs = 5000;

    constexpr std::size_t kActionCoreSystem   = 0x80;
    constexpr std::size_t kCarriedSystem      = 0x60;
    constexpr std::size_t kDamageSystem       = 0x68;
    constexpr int         kSmokeEnter         = 1;   // the proc that says LIQV_0390 and flags him as in smoke
    constexpr std::size_t kSystemSound        = 0x70;
    constexpr std::size_t kEliSaySlot         = 0x10 / sizeof(void*);
    constexpr std::size_t kEliSayingSlot      = 0x28 / sizeof(void*);
    constexpr std::uint64_t kCarryGapMs       = 2000;   // no carried state for this long: the next one is a new lift

    constexpr std::uint32_t kHostageSlots = 0x200;
    constexpr std::uint32_t kEliSlots     = 8;

    CallVoiceImpl_t    g_OrigCallVoiceImpl    = nullptr;
    UpdateSleepVoice_t g_OrigUpdateSleepVoice = nullptr;
    StateCarried_t     g_OrigStateCarried     = nullptr;
    StateSmokeStart_t  g_OrigStateSmokeStart  = nullptr;
    void*              g_CallVoiceImplTarget    = nullptr;
    void*              g_UpdateSleepVoiceTarget = nullptr;
    void*              g_StateCarriedTarget     = nullptr;
    void*              g_StateSmokeStartTarget  = nullptr;

    std::atomic<std::uint64_t> g_MisstepMs[kHostageSlots];
    std::atomic<std::uint64_t> g_EliDozingUntilMs[kEliSlots];
    std::atomic<std::uint64_t> g_EliNextBreathMs[kEliSlots];
    std::atomic<std::uint64_t> g_EliCarriedMs[kEliSlots];

    struct BuildSites
    {
        std::uintptr_t callVoiceImpl;      // Hostage2SoundControllerImpl::CallVoiceImpl
        std::uintptr_t updateSleepVoice;   // liquid ActionCoreImpl::UpdateSleepVoice
        std::uintptr_t stateCarried;       // liquid CarriedActionImpl::State_Carried
        std::uintptr_t stateSmokeStart;    // liquid DamageActionImpl::State_SmokeStart
    };


    const BuildSites* SitesFor()
    {
        static const BuildSites sites = { gAddr.Hostage2SoundControllerImpl_CallVoiceImpl, gAddr.LiquidActionCoreImpl_UpdateSleepVoice, gAddr.LiquidCarriedActionImpl_State_Carried, gAddr.LiquidDamageActionImpl_State_SmokeStart };
        return &sites;
    }

    void* EliSound(void* owner, std::size_t systemOffset)
    {
        __try
        {
            const std::uint8_t* system = *reinterpret_cast<const std::uint8_t* const*>(static_cast<const std::uint8_t*>(owner) + systemOffset);
            return system ? *reinterpret_cast<void* const*>(system + kSystemSound) : nullptr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    std::uint32_t EliSaying(void* sound, std::uint32_t slot)
    {
        __try
        {
            return (*reinterpret_cast<EliSaying_t* const*>(sound))[kEliSayingSlot](sound, slot);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    void EliSay(void* sound, std::uint32_t slot, std::uint32_t line)
    {
        __try
        {
            (*reinterpret_cast<EliSay_t* const*>(sound))[kEliSaySlot](sound, slot, kEliEvent, kEliPriority, line);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    bool __fastcall hkCallVoiceImpl(void* soundController, std::uint32_t slot, std::uint32_t hostageIndex, std::uint32_t dialogueEvent,
                                    std::uint32_t priority, std::uint32_t line, float repeatWindow, bool flag)
    {
        if (!MissionCodeGuard::ShouldBypassHooks())
        {
            const std::uint32_t hostage = hostageIndex % kHostageSlots;
            const std::uint64_t now = GetTickCount64();
            if (line == kMisstepLine)
                g_MisstepMs[hostage].store(now, std::memory_order_relaxed);
            else if (line == kFallToDeathLine && dialogueEvent != kNoMisstepFallEvent
                     && now - g_MisstepMs[hostage].load(std::memory_order_relaxed) < kMisstepFallMs)
            {
                line = kMisstepFallLine;
            }
        }
        return g_OrigCallVoiceImpl(soundController, slot, hostageIndex, dialogueEvent, priority, line, repeatWindow, flag);
    }

    void __fastcall hkUpdateSleepVoice(void* actionCore, std::uint32_t slot, std::uint32_t eliIndex)
    {
        void* sound = MissionCodeGuard::ShouldBypassHooks() ? nullptr : EliSound(actionCore, kActionCoreSystem);
        if (!sound)
        {
            g_OrigUpdateSleepVoice(actionCore, slot, eliIndex);
            return;
        }
        const std::uint32_t eli = slot % kEliSlots;
        const std::uint64_t now = GetTickCount64();
        if (now < g_EliDozingUntilMs[eli].load(std::memory_order_relaxed))
            return;   // his dozing-off line plays out before the sleeping breath starts

        const std::uint32_t before = EliSaying(sound, slot);
        g_OrigUpdateSleepVoice(actionCore, slot, eliIndex);
        const std::uint32_t after = EliSaying(sound, slot);
        if (before != kEliSleepLine && after == kEliSleepLine && before != kEliTranqHitLine)
        {
            EliSay(sound, slot, kEliDozingLine);
            g_EliDozingUntilMs[eli].store(now + kEliDozingMs, std::memory_order_relaxed);
        }
        else if (before == kEliSleepLine && after != kEliSleepLine)
        {
            EliSay(sound, slot, kEliWakingLine);
        }
    }

    void __fastcall hkStateCarried(void* carriedAction, std::uint32_t slot, int proc, void* arg)
    {
        g_OrigStateCarried(carriedAction, slot, proc, arg);
        if (MissionCodeGuard::ShouldBypassHooks())
            return;
        void* sound = EliSound(carriedAction, kCarriedSystem);
        if (!sound)
            return;
        const std::uint32_t eli = slot % kEliSlots;
        const std::uint64_t now = GetTickCount64();
        const std::uint64_t lastCarried = g_EliCarriedMs[eli].exchange(now, std::memory_order_relaxed);
        if (EliSaying(sound, slot) == kEliSleepLine)
            return;
        if (now - lastCarried > kCarryGapMs)
        {
            EliSay(sound, slot, kEliLiftedLine);
            g_EliNextBreathMs[eli].store(now + kEliCarriedBreathMs, std::memory_order_relaxed);
        }
        else if (now >= g_EliNextBreathMs[eli].load(std::memory_order_relaxed))
        {
            EliSay(sound, slot, kEliCarriedLine);
            g_EliNextBreathMs[eli].store(now + kEliCarriedBreathMs, std::memory_order_relaxed);
        }
    }

    void __fastcall hkStateSmokeStart(void* damageAction, std::uint32_t slot, int proc, void* arg)
    {
        g_OrigStateSmokeStart(damageAction, slot, proc, arg);
        if (proc != kSmokeEnter || MissionCodeGuard::ShouldBypassHooks() || (GetTickCount64() & 1) == 0)
            return;
        void* sound = EliSound(damageAction, kDamageSystem);
        if (!sound)
            return;
        EliSay(sound, slot, kEliCoughLine);
    }

    void* InstallOne(const char* what, std::uintptr_t address, const std::uint8_t* entry, std::size_t entrySize,
                     void* detour, void** original)
    {
        void* target = ResolveGameAddress(address);
        const HookEntry state = CheckHookEntry(target, entry, entrySize);
        if (state == HookEntry::Unknown)
        {
            LogError("HostageVoice: %s entry mismatch on %s, its lines are off", what, GetGameBuildName(gGameBuild));
            return nullptr;
        }
        if (state == HookEntry::ForeignHook)
            LogHookedOnTop(what, target);
        if (!CreateAndEnableHook(target, detour, original))
        {
            LogError("HostageVoice: %s hook failed at %p, its lines are off", what, target);
            return nullptr;
        }
        return target;
    }

    void RemoveOne(void*& target)
    {
        if (target)
            DisableAndRemoveHook(target);
        target = nullptr;
    }
}

bool Install_HostageVoiceLines_Hooks()
{
    const BuildSites* sites = SitesFor();
    if (!sites)
    {
        LogWarn("HostageVoice: addresses unknown for %s, no restored prisoner and Eli lines", GetGameBuildName(gGameBuild));
        return true;
    }

    if (!g_CallVoiceImplTarget)
        g_CallVoiceImplTarget = InstallOne("Hostage2SoundControllerImpl::CallVoiceImpl", sites->callVoiceImpl, kCallVoiceImplEntry,
                                           sizeof(kCallVoiceImplEntry), reinterpret_cast<void*>(&hkCallVoiceImpl),
                                           reinterpret_cast<void**>(&g_OrigCallVoiceImpl));
    if (!g_UpdateSleepVoiceTarget)
        g_UpdateSleepVoiceTarget = InstallOne("liquid ActionCoreImpl::UpdateSleepVoice", sites->updateSleepVoice, kUpdateSleepVoiceEntry,
                                              sizeof(kUpdateSleepVoiceEntry), reinterpret_cast<void*>(&hkUpdateSleepVoice),
                                              reinterpret_cast<void**>(&g_OrigUpdateSleepVoice));
    if (!g_StateCarriedTarget)
        g_StateCarriedTarget = InstallOne("liquid CarriedActionImpl::State_Carried", sites->stateCarried, kStateCarriedEntry,
                                          sizeof(kStateCarriedEntry), reinterpret_cast<void*>(&hkStateCarried),
                                          reinterpret_cast<void**>(&g_OrigStateCarried));
    if (!g_StateSmokeStartTarget)
        g_StateSmokeStartTarget = InstallOne("liquid DamageActionImpl::State_SmokeStart", sites->stateSmokeStart, kStateSmokeStartEntry,
                                             sizeof(kStateSmokeStartEntry), reinterpret_cast<void*>(&hkStateSmokeStart),
                                             reinterpret_cast<void**>(&g_OrigStateSmokeStart));
    return true;
}

void Uninstall_HostageVoiceLines_Hooks()
{
    RemoveOne(g_StateSmokeStartTarget);
    RemoveOne(g_StateCarriedTarget);
    RemoveOne(g_UpdateSleepVoiceTarget);
    RemoveOne(g_CallVoiceImplTarget);
}
