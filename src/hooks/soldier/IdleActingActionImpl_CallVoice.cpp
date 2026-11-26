#include <pch.h>

#include <Windows.h>
#include <array>
#include <cstdint>
#include <mutex>

#include "AddressSet.h"
#include "CpRadioVariants.h"
#include "FeatureSwitch.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "RadioControllerImpl_StepRadioWaitResponse.h"
#include "SoldierRealizedSlot.h"
#include "log.h"

namespace
{
    using CallVoice_t  = void(__fastcall*)(void* self, std::uint32_t slot, std::uint8_t voice);
    using Speak_t      = bool(__fastcall*)(void* soundController, std::uint32_t soldierIndex, std::uint32_t dialogueEvent,
                                           int priority, std::uint32_t line, float delay);

    constexpr std::uint8_t  kVoiceCheckItOut        = 3;            // EVN200, the eager "I'm gonna check it out" reply
    constexpr std::uint32_t kEnemyVoiceEvent        = 0x95EA16B0u;  // DD_vox_ene
    constexpr std::uint32_t kTenseCheckItOutLine    = 0x877AB170u;  // EVN190, the grudging one
    constexpr int           kIdleActingPriority     = 1;
    constexpr float         kIdleActingDelay        = 1.0f;
    constexpr std::size_t   kActionSoldier          = 0x1A8;
    constexpr std::size_t   kSoldierSoundController = 0xA8;
    constexpr std::size_t   kSpeakSlot              = 0x20 / sizeof(void*);
    constexpr std::size_t   kSoldierCount           = 0x200;
    constexpr std::uint32_t kGrudgingFrom           = 2;            // user: his second request since his base was last back to normal

    struct Asked
    {
        std::uint32_t count  = 0;
        std::uint64_t lastMs = 0;
    };

    CallVoice_t                        g_OrigCallVoice = nullptr;
    std::mutex                         g_AskedMutex;
    std::array<Asked, kSoldierCount>   g_Asked{};

    std::uint8_t* SoldierOf(void* action)
    {
        return *reinterpret_cast<std::uint8_t**>(static_cast<std::uint8_t*>(action) + kActionSoldier);
    }

    std::uint32_t AskedAgain(void* action, std::uint32_t slot)   // how many times he has been sent to check since his CP calmed
    {
        const std::uint32_t soldierIndex = SoldierRealizedSlot::SoldierIn(SoldierOf(action), slot);
        std::uint32_t cp = 0;
        std::int32_t phase = 0;
        if (soldierIndex >= kSoldierCount || !CpRadioResponse::CpPhaseOf(static_cast<std::uint16_t>(soldierIndex), cp, phase))
            return 1;
        const std::uint64_t calmedMs = CpRadioVariants_CpCalmedMs(cp);
        const std::uint64_t now = GetTickCount64();
        std::lock_guard<std::mutex> lock(g_AskedMutex);
        Asked& asked = g_Asked[soldierIndex];
        if (asked.lastMs < calmedMs)
            asked.count = 0;
        asked.lastMs = now;
        return ++asked.count;
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

    void __fastcall hk_CallVoice(void* self, std::uint32_t slot, std::uint8_t voice)
    {
        MISSION_GUARD_ORIGINAL_VOID(g_OrigCallVoice, self, slot, voice);

        if (voice == kVoiceCheckItOut && AskedAgain(self, slot) >= kGrudgingFrom
            && FeatureSwitch::IsOn(FeatureSwitch::Feature::SoldierRadioAnswers) && SayLine(self, slot, kTenseCheckItOutLine))
            return;

        g_OrigCallVoice(self, slot, voice);
    }
}

void IdleActingActionImpl_CallVoice_ClearForReload()
{
    std::lock_guard<std::mutex> lock(g_AskedMutex);
    g_Asked.fill(Asked{});
}

bool Install_IdleActingActionImpl_CallVoice_Hook()
{
    void* target = ResolveGameAddress(gAddr.IdleActingActionImpl_CallVoice);
    if (!target)
    {
        LogError("IdleActingVoice: CallVoice address missing on this build, no EVN190 on a second request");
        return false;
    }
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hk_CallVoice), reinterpret_cast<void**>(&g_OrigCallVoice)))
    {
        LogError("IdleActingVoice: CallVoice hook failed, no EVN190 on a second request");
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
    std::lock_guard<std::mutex> lock(g_AskedMutex);
    g_Asked.fill(Asked{});
    return true;
}
