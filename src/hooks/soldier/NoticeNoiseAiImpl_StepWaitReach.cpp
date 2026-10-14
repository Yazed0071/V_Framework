#include "pch.h"
#include "NoticeNoiseAiImpl_StepWaitReach.h"

#include <cstddef>
#include <cstdint>

#include "AddressSet.h"
#include "ComradeTurn.h"
#include "HookUtils.h"
#include "log.h"
#include "MissionCodeGuard.h"

namespace
{
    using StepWaitReach_t = void(__fastcall*)(std::uint8_t* noticeNoiseAi, std::uint32_t soldierIndex, int stepProc,
                                              std::uint8_t* knowledge);

    constexpr int          kStepUpdate    = 3;
    constexpr std::size_t  kKnowledgeStep = 0x2E;   // the step it goes to next
    constexpr std::uint8_t kStepWaitReach = 0x0C;
    constexpr std::uint8_t kStepSearch    = 0x0D;
    constexpr std::uint8_t kStepWaitReport = 0x0F;
    constexpr std::uint8_t kStepEnd       = 0x15;

    StepWaitReach_t g_Orig       = nullptr;
    void*           g_HookTarget = nullptr;

    std::uintptr_t AddressFor(AddressSetRuntime::GameBuild build)
    {
        using GB = AddressSetRuntime::GameBuild;
        switch (build)
        {
        case GB::En_1_0_15_4:
        case GB::En_1_0_15_4a: return 0x141523460ull;
        case GB::Jp_1_0_15_4:
        case GB::Jp_1_0_15_4a: return 0x141523470ull;
        case GB::En_1_0_15_3:  return 0x141523D80ull;
        case GB::Jp_1_0_15_3:  return 0x141523D50ull;
        default:               return 0;
        }
    }

    void __fastcall hk_StepWaitReach(std::uint8_t* noticeNoiseAi, std::uint32_t soldierIndex, int stepProc, std::uint8_t* knowledge)
    {
        g_Orig(noticeNoiseAi, soldierIndex, stepProc, knowledge);
        if (stepProc != kStepUpdate || !knowledge || MissionCodeGuard::ShouldBypassHooks())
            return;

        std::uint8_t& next = knowledge[kKnowledgeStep];
        if (next != kStepWaitReach && next != kStepSearch && next != kStepWaitReport)
            return;   // a new notice took over
        if (!CallComrade::IsWalkingOver(soldierIndex))
            return;

        next = CallComrade::ShouldLeaveOnArrival(soldierIndex) ? kStepEnd : kStepWaitReach;   // no search or report, back to his route as in GZ
    }
}

bool Install_NoticeNoiseAiImpl_StepWaitReach_Hook()
{
    if (g_HookTarget)
        return true;

    const std::uintptr_t address = AddressFor(gGameBuild);
    if (!address)
    {
        LogWarn("CallComradeCommand: NoticeNoiseAiImpl::StepWaitReach unknown for %s, called comrades search after arriving",
                GetGameBuildName(gGameBuild));
        return true;
    }

    void* target = ResolveGameAddress(address);
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hk_StepWaitReach), reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("CallComradeCommand: NoticeNoiseAiImpl::StepWaitReach hook failed at %p, called comrades search after arriving", target);
        return false;
    }
    g_HookTarget = target;
    return true;
}

void Uninstall_NoticeNoiseAiImpl_StepWaitReach_Hook()
{
    if (g_HookTarget)
    {
        DisableAndRemoveHook(g_HookTarget);
        g_HookTarget = nullptr;
        g_Orig       = nullptr;
    }
}
