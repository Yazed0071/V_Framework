#include <pch.h>

#include <Windows.h>
#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "SoldierFaction.h"
#include "log.h"

namespace
{
    using StepSurprise_t = void(__fastcall*)(void* self, std::uint32_t soldierIndex, int stepProc, std::uint8_t* knowledge);
    using GetCpPhase_t   = int(__fastcall*)(void* commandPosts, std::uint32_t groupId, std::uint32_t soldierIndex);
    using RequestRadio_t = void(__fastcall*)(void* commandPosts, std::uint32_t groupId, std::uint32_t soldierIndex,
                                             std::uint32_t radioType, std::uint32_t option);

    constexpr int           kStepProcTransition        = 3;
    constexpr std::size_t   kKnowledgeStep             = 0x54;
    constexpr std::size_t   kKnowledgeLatchedNotice    = 0x9F;
    constexpr std::uint8_t  kNoticeWalkerGearSighted   = 0x0Cu;
    constexpr int           kPhaseSneak                = 0;
    constexpr std::uint8_t  kStepStarting              = 10u;
    constexpr std::size_t   kNodeSoldierCore           = 0x1A0;
    constexpr std::size_t   kSoldierCoreCommandPost    = 0xD0;
    constexpr std::size_t   kCommandPostController     = 0x08;
    constexpr std::size_t   kCommandPostGroupId        = 0x30;
    constexpr std::size_t   kGetCpPhaseSlot            = 0x188 / sizeof(void*);
    constexpr std::size_t   kRequestRadioSlot          = 0x1D8 / sizeof(void*);
    constexpr std::uint32_t kRadioSituation            = 0x4Eu;   // "We have a situation", what vanilla calls once the surprise ends
    constexpr int           kPhaseUnknown              = -1;

    StepSurprise_t g_OrigStepSurprise = nullptr;

    int ReadCommandPostPhase(void* self, std::uint32_t soldierIndex)
    {
        __try
        {
            const std::uint8_t* soldierCore = *reinterpret_cast<const std::uint8_t* const*>(
                static_cast<const std::uint8_t*>(self) + kNodeSoldierCore);
            const std::uint8_t* commandPost = *reinterpret_cast<const std::uint8_t* const*>(soldierCore + kSoldierCoreCommandPost);
            void* controller = *reinterpret_cast<void* const*>(commandPost + kCommandPostController);
            const std::uint32_t groupId = *reinterpret_cast<const std::uint32_t*>(commandPost + kCommandPostGroupId);
            GetCpPhase_t getCpPhase = (*reinterpret_cast<GetCpPhase_t* const*>(controller))[kGetCpPhaseSlot];
            return getCpPhase(controller, groupId, soldierIndex);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return kPhaseUnknown;
        }
    }

    bool RequestSituationRadio(void* self, std::uint32_t soldierIndex)
    {
        __try
        {
            const std::uint8_t* soldierCore = *reinterpret_cast<const std::uint8_t* const*>(
                static_cast<const std::uint8_t*>(self) + kNodeSoldierCore);
            const std::uint8_t* commandPost = *reinterpret_cast<const std::uint8_t* const*>(soldierCore + kSoldierCoreCommandPost);
            void* controller = *reinterpret_cast<void* const*>(commandPost + kCommandPostController);
            const std::uint32_t groupId = *reinterpret_cast<const std::uint32_t*>(commandPost + kCommandPostGroupId);
            RequestRadio_t requestRadio = (*reinterpret_cast<RequestRadio_t* const*>(controller))[kRequestRadioSlot];
            requestRadio(controller, groupId, soldierIndex, kRadioSituation, 0);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void __fastcall hk_StepSurprise(void* self, std::uint32_t soldierIndex, int stepProc, std::uint8_t* knowledge)
    {
        MISSION_GUARD_ORIGINAL_VOID(g_OrigStepSurprise, self, soldierIndex, stepProc, knowledge);

        if (!knowledge || SoldierFaction_GetRivalTarget(soldierIndex) == 0)
        {
            g_OrigStepSurprise(self, soldierIndex, stepProc, knowledge);
            return;
        }
        if (stepProc != kStepProcTransition)   // rivals skip the walker gear surprise motion: its command is never sent
            return;

        const int phase = ReadCommandPostPhase(self, soldierIndex);
        if (knowledge[kKnowledgeLatchedNotice] == kNoticeWalkerGearSighted
            && phase != kPhaseUnknown && phase <= kPhaseSneak)
            RequestSituationRadio(self, soldierIndex);
        knowledge[kKnowledgeStep] = kStepStarting;
    }
}

bool Install_CombatAntiBuddyWalkerGearAiImpl_StepSurprise_Hook()
{
    void* target = ResolveGameAddress(gAddr.CombatAntiBuddyWalkerGearAiImpl_StepSurprise);
    if (!target)
        return false;
    return CreateAndEnableHook(target, reinterpret_cast<void*>(&hk_StepSurprise),
                               reinterpret_cast<void**>(&g_OrigStepSurprise));
}

bool Uninstall_CombatAntiBuddyWalkerGearAiImpl_StepSurprise_Hook()
{
    void* target = ResolveGameAddress(gAddr.CombatAntiBuddyWalkerGearAiImpl_StepSurprise);
    if (target)
        DisableAndRemoveHook(target);
    g_OrigStepSurprise = nullptr;
    return true;
}
