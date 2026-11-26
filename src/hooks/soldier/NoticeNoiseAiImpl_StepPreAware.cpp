#include <pch.h>
#include "NoticeNoiseAiImpl_StepPreAware.h"

#include <cstddef>
#include <cstdint>

#include "AddressSet.h"
#include "ComradeTurn.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "log.h"

namespace
{
    using StepPreAware_t = void(__fastcall*)(std::uint8_t* noticeNoiseAi, std::uint32_t soldierIndex, int stepProc,
                                             std::uint8_t* knowledge);
    using GetSituation_t = std::uint8_t*(__fastcall*)(void* situations, std::uint32_t owner, std::uint32_t soldierIndex,
                                                      std::uint16_t situation);

    constexpr std::size_t  kNoticeAiSoldier       = 0x70;
    constexpr std::size_t  kSoldierSituations     = 0x90;      // points at {the notice situations, their owner id}
    constexpr std::size_t  kSituationsOwner       = 0x08;
    constexpr std::size_t  kGetSituationVtblIndex = 0x30 / 8;
    constexpr std::size_t  kSituationOnGuard      = 0x10;      // 1: whoever checks this comes weapon ready
    constexpr std::size_t  kKnowledgeSituation    = 0x28;
    constexpr std::size_t  kKnowledgeFlags        = 0x32;
    constexpr std::uint8_t kFlagOnGuard           = 4;         // weapon ready on the way: his notice skill, the dark, a third check

    const std::uint8_t kStepPreAwarePrologue[] = {
        0x48, 0x89, 0x5C, 0x24, 0x18, 0x55, 0x56, 0x57, 0x48, 0x81, 0xEC, 0x10, 0x01, 0x00, 0x00, 0x48,
        0x8B, 0x05, 0x00, 0x00, 0x00, 0x00, 0x48, 0x33, 0xC4, 0x48, 0x89, 0x84, 0x24, 0x00, 0x01, 0x00,
        0x00, 0x48, 0x8B, 0x41, 0x70, 0x49, 0x8B, 0xD9, 0x8B, 0xFA
    };
    const std::uint8_t kStepPreAwareMask[] = {
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1
    };
    static_assert(sizeof(kStepPreAwarePrologue) == sizeof(kStepPreAwareMask));

    StepPreAware_t g_Orig       = nullptr;
    void*          g_HookTarget = nullptr;

    template <typename T>
    T& At(std::uint8_t* base, std::size_t offset)
    {
        return *reinterpret_cast<T*>(base + offset);
    }

    void CalmSituation(std::uint8_t* noticeNoiseAi, std::uint32_t soldierIndex, std::uint8_t* knowledge)
    {
        std::uint8_t* soldier = At<std::uint8_t*>(noticeNoiseAi, kNoticeAiSoldier);
        std::uint8_t* holder  = soldier ? At<std::uint8_t*>(soldier, kSoldierSituations) : nullptr;
        void* situations      = holder ? At<void*>(holder, 0) : nullptr;
        if (!situations)
            return;

        auto getSituation = (*reinterpret_cast<GetSituation_t**>(situations))[kGetSituationVtblIndex];
        std::uint8_t* situation = getSituation(situations, At<std::uint32_t>(holder, kSituationsOwner), soldierIndex,
                                               At<std::uint16_t>(knowledge, kKnowledgeSituation));
        if (situation && At<std::int32_t>(situation, kSituationOnGuard) == 1)
            At<std::int32_t>(situation, kSituationOnGuard) = 0;   // the later steps copy it back into his flags
    }

    void __fastcall hk_StepPreAware(std::uint8_t* noticeNoiseAi, std::uint32_t soldierIndex, int stepProc, std::uint8_t* knowledge)
    {
        const bool calm = noticeNoiseAi && knowledge && !MissionCodeGuard::ShouldBypassHooks() && CallComrade::ComesCalm(soldierIndex);
        if (calm)
        {
            CalmSituation(noticeNoiseAi, soldierIndex, knowledge);
            knowledge[kKnowledgeFlags] &= static_cast<std::uint8_t>(~kFlagOnGuard);
        }
        g_Orig(noticeNoiseAi, soldierIndex, stepProc, knowledge);
        if (calm)
            knowledge[kKnowledgeFlags] &= static_cast<std::uint8_t>(~kFlagOnGuard);
    }
}

bool Install_NoticeNoiseAiImpl_StepPreAware_Hook()
{
    if (g_HookTarget)
        return true;

    if (!gAddr.NoticeNoiseAiImpl_StepPreAware)
    {
        LogError("CallComradeCommand: NoticeNoiseAiImpl::StepPreAware address missing for %s, a comrade called in Sneak can come weapon ready",
                 GetGameBuildName(gGameBuild));
        return true;
    }

    void* target = ResolveGameAddress(gAddr.NoticeNoiseAiImpl_StepPreAware);
    const HookEntry entry = CheckHookEntryMasked(target, kStepPreAwarePrologue, kStepPreAwareMask, sizeof(kStepPreAwarePrologue));
    if (entry == HookEntry::Unknown)
    {
        LogError("CallComradeCommand: NoticeNoiseAiImpl::StepPreAware does not match %s, a comrade called in Sneak can come weapon ready",
                 GetGameBuildName(gGameBuild));
        return false;
    }
    if (entry == HookEntry::ForeignHook)
        LogHookedOnTop("CallComradeCommand: NoticeNoiseAiImpl::StepPreAware", target);
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hk_StepPreAware), reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("CallComradeCommand: NoticeNoiseAiImpl::StepPreAware hook failed at %p, a comrade called in Sneak can come weapon ready", target);
        return false;
    }

    g_HookTarget = target;
    return true;
}

void Uninstall_NoticeNoiseAiImpl_StepPreAware_Hook()
{
    if (g_HookTarget)
    {
        DisableAndRemoveHook(g_HookTarget);
        g_HookTarget = nullptr;
        g_Orig       = nullptr;
    }
}
