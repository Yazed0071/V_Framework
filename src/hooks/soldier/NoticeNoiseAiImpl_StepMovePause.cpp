#include "pch.h"
#include "NoticeNoiseAiImpl_StepMovePause.h"

#include <cstddef>
#include <cstdint>

#include "AddressSet.h"
#include "ComradeTurn.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    using StepMovePause_t = void(__fastcall*)(std::uint8_t* noticeNoiseAi, std::uint32_t soldierIndex,
                                              int stepProc, void* knowledge);

    constexpr std::size_t kNoticeAiSoldier = 0x70;
    constexpr int         kStepUpdate      = 3;

    StepMovePause_t g_Orig       = nullptr;
    void*           g_HookTarget = nullptr;

    void __fastcall hk_StepMovePause(std::uint8_t* noticeNoiseAi, std::uint32_t soldierIndex, int stepProc, void* knowledge)
    {
        if (stepProc == kStepUpdate && noticeNoiseAi)
        {
            std::uint8_t* soldier = *reinterpret_cast<std::uint8_t**>(noticeNoiseAi + kNoticeAiSoldier);
            if (CallComrade::ShouldHoldMovePause(soldier, soldierIndex))
                return;   // keeps the called comrade standing until he has answered
        }

        g_Orig(noticeNoiseAi, soldierIndex, stepProc, knowledge);
    }
}

bool Install_NoticeNoiseAiImpl_StepMovePause_Hook()
{
    if (g_HookTarget)
        return true;

    if (!gAddr.NoticeNoiseAiImpl_StepMovePause)
    {
        LogError("CallComradeCommand: NoticeNoiseAiImpl::StepMovePause address missing for %s, called comrades walk over without stopping to answer",
                 GetGameBuildName(gGameBuild));
        return true;
    }

    void* target = ResolveGameAddress(gAddr.NoticeNoiseAiImpl_StepMovePause);
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hk_StepMovePause),
                             reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("CallComradeCommand: NoticeNoiseAiImpl::StepMovePause hook failed at %p, called comrades walk over without stopping to answer", target);
        return false;
    }

    g_HookTarget = target;
    return true;
}

void Uninstall_NoticeNoiseAiImpl_StepMovePause_Hook()
{
    if (g_HookTarget)
    {
        DisableAndRemoveHook(g_HookTarget);
        g_HookTarget = nullptr;
        g_Orig       = nullptr;
    }
}
