#include "pch.h"

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

    constexpr int          kStepProcTransition        = 3;
    constexpr std::size_t  kKnowledgeStep             = 0x54;
    constexpr std::size_t  kKnowledgeLatchedNotice    = 0x9F;
    constexpr std::uint8_t kStepSurprise              = 9u;
    constexpr std::uint8_t kNoticeWalkerGearSighted   = 0x0Cu;
    constexpr int          kPhaseSneak                = 0;
    constexpr std::uint8_t kStepStarting              = 10u;
    constexpr std::uint8_t kStepSniperMoveSelect      = 11u;
    constexpr std::size_t  kNodeSoldierCore           = 0x1A0;
    constexpr std::size_t  kSoldierCoreCommandPost    = 0xD0;
    constexpr std::size_t  kCommandPostController     = 0x08;
    constexpr std::size_t  kCommandPostGroupId        = 0x30;
    constexpr std::size_t  kGetCpPhaseSlot            = 0x188 / sizeof(void*);
    constexpr int          kPhaseUnknown              = -1;

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

    void __fastcall hk_StepSurprise(void* self, std::uint32_t soldierIndex, int stepProc, std::uint8_t* knowledge)
    {
        MISSION_GUARD_ORIGINAL_VOID(g_OrigStepSurprise, self, soldierIndex, stepProc, knowledge);

        const bool rivalTransition = stepProc == kStepProcTransition && knowledge
            && SoldierFaction_GetRivalTarget(soldierIndex) != 0;
        if (!rivalTransition)
        {
            g_OrigStepSurprise(self, soldierIndex, stepProc, knowledge);
            return;
        }

        const int phaseBefore = ReadCommandPostPhase(self, soldierIndex);
        g_OrigStepSurprise(self, soldierIndex, stepProc, knowledge);

        if (knowledge[kKnowledgeStep] != kStepSurprise)
        {
            const bool radioRequested = phaseBefore <= kPhaseSneak && phaseBefore != kPhaseUnknown
                && knowledge[kKnowledgeLatchedNotice] == kNoticeWalkerGearSighted;
            LogInfo("SoldierFaction: soldier %u surprise ended, base phase %d, base radio %s",
                soldierIndex, phaseBefore, radioRequested ? "requested" : "skipped (base not in normal phase)");
        }
        if (knowledge[kKnowledgeStep] == kStepSniperMoveSelect)
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
