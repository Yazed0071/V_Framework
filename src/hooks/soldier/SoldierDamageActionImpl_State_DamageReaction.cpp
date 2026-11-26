#include <pch.h>
#include "SoldierDamageActionImpl_State_DamageReaction.h"

#include <Windows.h>
#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "SoldierVoiceType.h"
#include "log.h"

// The damage reaction's motion events say EVD010/EVD030, except for a zombie that is not MSF: he only gets the se_e_zmb
// hit sound 0x5E4D2E17. zmbdm0 is the zombie damage take recorded for the zombie voice and DD voices B/D.

namespace
{
    using State_t     = std::uint64_t(__fastcall*)(void* damageAction, std::uint32_t actionIndex, std::uint32_t proc, void* event);
    using EventType_t = std::uint32_t(__fastcall*)(void* event);
    using EventData_t = void(__fastcall*)(void* event, void* out, std::uint32_t index);
    using Speak_t     = bool(__fastcall*)(void* soundController, std::uint32_t slot, std::uint32_t dialogueEvent, int priority,
                                          std::uint32_t line, float delay);

    constexpr std::uint32_t kProcMotionEvent         = 7;
    constexpr std::uint32_t kVoiceEventType          = 0x5A27160Au;
    constexpr std::uint32_t kHeavyDamageEvent        = 0xB05DD914u;   // the motion event vanilla answers with EVD030
    constexpr std::size_t   kEventDataSlot           = 0x80 / sizeof(void*);
    constexpr std::size_t   kSoldiers                = 0x1B0;
    constexpr std::size_t   kWorks                   = 0x1D0;   // 0xF0 per soldier in this action, from the index at +0x1D8
    constexpr std::size_t   kFirstWorkIndex          = 0x1D8;
    constexpr std::size_t   kWorkSize                = 0xF0;
    constexpr std::size_t   kWorkSoldier             = 0xDC;
    constexpr std::size_t   kSoldierLife             = 0x38;    // 0x40 per soldier at +8
    constexpr std::size_t   kLifeRecords             = 0x08;
    constexpr std::size_t   kLifeRecordSize          = 0x40;
    constexpr std::size_t   kLifeState               = 0x3E;
    constexpr std::uint8_t  kLifeStateDead           = 2;
    constexpr int           kStateMsfBit             = 33;      // in the second state word: an MSF zombie keeps his EVD030
    constexpr std::size_t   kSoldiersSoundController = 0xA8;
    constexpr std::size_t   kSpeakSlot               = 0x20 / sizeof(void*);
    constexpr std::uint32_t kEnemyVoiceEvent         = 0x95EA16B0u;   // DD_vox_ene
    constexpr std::uint32_t kZombieGrunt             = 0x246909E2u;   // zmbdm0
    constexpr int           kDamagePriority          = 3;
    constexpr float         kNoDelay                 = 0.0f;

    constexpr std::uint8_t kStateEntry[] = {
        0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x56, 0x48, 0x8D, 0x6C, 0x24, 0xC9, 0x48, 0x81, 0xEC, 0xA0, 0x00, 0x00, 0x00,
    };

    State_t g_Orig   = nullptr;
    void*   g_Target = nullptr;

    bool IsHeavyDamageEvent(void* event)
    {
        __try
        {
            void** vtable = *static_cast<void***>(event);
            if (reinterpret_cast<EventType_t>(vtable[0])(event) != kVoiceEventType)
                return false;
            alignas(16) std::uint8_t data[64] = {};
            reinterpret_cast<EventData_t>(vtable[kEventDataSlot])(event, data, 0);
            return *reinterpret_cast<const std::uint32_t*>(data) == kHeavyDamageEvent;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    const std::uint8_t* SilentZombie(const void* damageAction, std::uint32_t actionIndex)   // the soldiers block, or null
    {
        __try
        {
            const auto* action = static_cast<const std::uint8_t*>(damageAction);
            const std::uint8_t* soldiers = *reinterpret_cast<const std::uint8_t* const*>(action + kSoldiers);
            const std::uint8_t* works = *reinterpret_cast<const std::uint8_t* const*>(action + kWorks);
            const std::uint32_t first = *reinterpret_cast<const std::uint32_t*>(action + kFirstWorkIndex);
            const std::uint32_t soldierIndex = *reinterpret_cast<const std::uint32_t*>(works + (actionIndex - first) * kWorkSize + kWorkSoldier);
            const std::uint8_t* life = *reinterpret_cast<const std::uint8_t* const*>(
                *reinterpret_cast<const std::uint8_t* const*>(soldiers + kSoldierLife) + kLifeRecords) + soldierIndex * kLifeRecordSize;
            if (life[kLifeState] == kLifeStateDead || !SoldierVoiceType::IsZombie(soldiers, soldierIndex))
                return nullptr;
            const std::uint64_t* state = SoldierVoiceType::StateOf(soldiers, soldierIndex);
            if (!state || ((state[1] >> kStateMsfBit) & 1ull) != 0 || !SoldierVoiceType::HasZombieGrunt(SoldierVoiceType::Of(soldiers, soldierIndex)))
                return nullptr;
            return soldiers;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    bool Speak(const std::uint8_t* soldiers, std::uint32_t slot)
    {
        __try
        {
            void* soundController = *reinterpret_cast<void* const*>(soldiers + kSoldiersSoundController);
            const auto speak = (*reinterpret_cast<Speak_t* const*>(soundController))[kSpeakSlot];
            return speak(soundController, slot, kEnemyVoiceEvent, kDamagePriority, kZombieGrunt, kNoDelay);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    std::uint64_t __fastcall hk_StateDamageReaction(void* damageAction, std::uint32_t actionIndex, std::uint32_t proc, void* event)
    {
        const std::uint8_t* zombieSoldiers = nullptr;
        if (proc == kProcMotionEvent && event && !MissionCodeGuard::ShouldBypassHooks() && IsHeavyDamageEvent(event))
            zombieSoldiers = SilentZombie(damageAction, actionIndex);
        const std::uint64_t result = g_Orig(damageAction, actionIndex, proc, event);
        if (zombieSoldiers)
        {
            const bool said = Speak(zombieSoldiers, actionIndex);   // the slot vanilla speaks EVD030 on
            LogDebug("VoiceDebug: zombie in slot %u hit hard, zmbdm0 %s", actionIndex, said ? "said" : "not taken");
        }
        return result;
    }
}

bool Install_SoldierDamageActionImpl_State_DamageReaction_Hook()
{
    if (g_Target)
        return true;
    if (!gAddr.SoldierDamageActionImpl_State_DamageReaction)
    {
        LogWarn("SoldierVoiceSwap: soldier damage reaction unknown for %s, zombies hit hard stay silent", GetGameBuildName(gGameBuild));
        return true;
    }
    void* fn = ResolveGameAddress(gAddr.SoldierDamageActionImpl_State_DamageReaction);
    const HookEntry state = CheckHookEntry(fn, kStateEntry, sizeof(kStateEntry));
    if (state == HookEntry::Unknown)
    {
        char bytes[160] = {};
        DescribeFunctionEntry(fn, bytes, sizeof(bytes));
        LogError("SoldierVoiceSwap: soldier damage reaction differs on %s (%s), zombies hit hard stay silent", GetGameBuildName(gGameBuild), bytes);
        return false;
    }
    if (state == HookEntry::ForeignHook)
        LogHookedOnTop("SoldierVoiceSwap: soldier damage reaction", fn);
    if (!CreateAndEnableHook(fn, reinterpret_cast<void*>(&hk_StateDamageReaction), reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("SoldierVoiceSwap: soldier damage reaction hook failed at %p, zombies hit hard stay silent", fn);
        g_Orig = nullptr;
        return false;
    }
    g_Target = fn;
    return true;
}

void Uninstall_SoldierDamageActionImpl_State_DamageReaction_Hook()
{
    if (g_Target)
        DisableAndRemoveHook(g_Target);
    g_Target = nullptr;
    g_Orig   = nullptr;
}
