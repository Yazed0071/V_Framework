#include "pch.h"

#include <Windows.h>
#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "SoldierRealizedSlot.h"
#include "log.h"

namespace
{
    using CallVoice_t  = void(__fastcall*)(void* self, std::uint32_t slot, std::uint8_t voice);
    using Speak_t      = bool(__fastcall*)(void* soundController, std::uint32_t soldierIndex, std::uint32_t dialogueEvent,
                                           int priority, std::uint32_t line, float delay);
    using GetCpPhase_t = int(__fastcall*)(void* controller, std::uint32_t groupId, std::uint32_t soldierIndex);

    constexpr std::uint8_t  kVoiceCheckItOut        = 3;            // EVN200, the "Fine, I'll check it out" reply
    constexpr std::uint32_t kEnemyVoiceEvent        = 0x95EA16B0u;  // DD_vox_ene
    constexpr std::uint32_t kTenseCheckItOutLine    = 0x877AB170u;  // EVN190
    constexpr int           kIdleActingPriority     = 1;
    constexpr float         kIdleActingDelay        = 1.0f;
    constexpr int           kPhaseCaution           = 1;
    constexpr int           kPhaseUnknown           = -1;
    constexpr std::size_t   kActionSoldier          = 0x1A8;
    constexpr std::size_t   kSoldierSoundController = 0xA8;
    constexpr std::size_t   kSoldierCommandPost     = 0xD0;
    constexpr std::size_t   kCommandPostController  = 0x08;
    constexpr std::size_t   kCommandPostGroupId     = 0x30;
    constexpr std::size_t   kGetCpPhaseSlot         = 0x188 / sizeof(void*);
    constexpr std::size_t   kSpeakSlot              = 0x20 / sizeof(void*);

    CallVoice_t g_OrigCallVoice = nullptr;

    std::uint8_t* SoldierOf(void* action)
    {
        return *reinterpret_cast<std::uint8_t**>(static_cast<std::uint8_t*>(action) + kActionSoldier);
    }

    int CommandPostPhase(void* action, std::uint32_t soldierIndex)
    {
        __try
        {
            std::uint8_t* commandPost = *reinterpret_cast<std::uint8_t**>(SoldierOf(action) + kSoldierCommandPost);
            void* controller = *reinterpret_cast<void**>(commandPost + kCommandPostController);
            const std::uint32_t groupId = *reinterpret_cast<std::uint32_t*>(commandPost + kCommandPostGroupId);
            GetCpPhase_t getCpPhase = (*reinterpret_cast<GetCpPhase_t* const*>(controller))[kGetCpPhaseSlot];
            return getCpPhase(controller, groupId, soldierIndex);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return kPhaseUnknown;
        }
    }

    bool SayLine(void* action, std::uint32_t slot, std::uint32_t line)
    {
        __try
        {
            void* soundController = *reinterpret_cast<void**>(SoldierOf(action) + kSoldierSoundController);
            Speak_t speak = (*reinterpret_cast<Speak_t* const*>(soundController))[kSpeakSlot];
            return speak(soundController, slot, kEnemyVoiceEvent, kIdleActingPriority, line, kIdleActingDelay);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool InCaution(void* action, std::uint32_t slot)
    {
        const std::uint32_t soldierIndex = SoldierRealizedSlot::SoldierIn(SoldierOf(action), slot);
        return soldierIndex != SoldierRealizedSlot::kNotRealized && CommandPostPhase(action, soldierIndex) == kPhaseCaution;
    }

    void __fastcall hk_CallVoice(void* self, std::uint32_t slot, std::uint8_t voice)
    {
        MISSION_GUARD_ORIGINAL_VOID(g_OrigCallVoice, self, slot, voice);

        if (voice == kVoiceCheckItOut && InCaution(self, slot) && SayLine(self, slot, kTenseCheckItOutLine))
            return;

        g_OrigCallVoice(self, slot, voice);
    }
}

bool Install_IdleActingActionImpl_CallVoice_Hook()
{
    void* target = ResolveGameAddress(gAddr.IdleActingActionImpl_CallVoice);
    if (!target)
    {
        LogError("IdleActingVoice: CallVoice address missing on this build, caution EVN190 reply off");
        return false;
    }
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hk_CallVoice), reinterpret_cast<void**>(&g_OrigCallVoice)))
    {
        LogError("IdleActingVoice: CallVoice hook failed, caution EVN190 reply off");
        return false;
    }
    return true;
}

bool Uninstall_IdleActingActionImpl_CallVoice_Hook()
{
    void* target = ResolveGameAddress(gAddr.IdleActingActionImpl_CallVoice);
    if (target)
        DisableAndRemoveHook(target);
    g_OrigCallVoice = nullptr;
    return true;
}
