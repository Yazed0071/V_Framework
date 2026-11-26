#include <pch.h>
#include "InvestigateAiImpl_PreUpdate.h"

#include <Windows.h>
#include <cstddef>
#include <cstdint>

#include "AddressSet.h"
#include "CpRadioVariants.h"
#include "HookUtils.h"
#include "log.h"

#ifdef _DEBUG
namespace
{
    using PreUpdate_t = std::uint64_t(__fastcall*)(void* node, std::uint64_t soldierIndex, std::uint8_t** context);

    constexpr std::size_t kKnowledgeStep = 0x57;   // debug names: 5 Reached, 6 Split1, 7 Split1InRoom, 8 Split2, 9 Split3, 10 Radio, 11 Notice, 12 NoticeInactiveComrade

    constexpr std::uint8_t kPreUpdateEntry[] = {
        0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x57, 0x48, 0x8D, 0xAC, 0x24, 0xB0,
        0xFE, 0xFF, 0xFF, 0x48, 0x81, 0xEC, 0x50, 0x02, 0x00, 0x00,
    };

    PreUpdate_t g_Orig   = nullptr;
    void*       g_Target = nullptr;

    std::uint32_t StepOf(std::uint8_t** context)
    {
        __try
        {
            return context && *context ? (*context)[kKnowledgeStep] : 0xFFu;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0xFFu;
        }
    }

    std::uint64_t __fastcall hk_PreUpdate(void* node, std::uint64_t soldierIndex, std::uint8_t** context)
    {
        const std::uint32_t before = StepOf(context);
        const std::uint64_t result = g_Orig(node, soldierIndex, context);
        const std::uint32_t after = StepOf(context);
        std::uint32_t cp = 0;
        if (before != after && CpRadioVariants_ProbeSurveyCp(static_cast<std::uint32_t>(soldierIndex), cp))
            LogDebug("SurveyProbe: cp %u: soldier %u investigate step %u -> %u", cp, static_cast<std::uint32_t>(soldierIndex), before, after);
        return result;
    }
}
#endif

bool Install_InvestigateAiImpl_PreUpdate_Hook()
{
#ifdef _DEBUG
    if (g_Target || !gAddr.InvestigateAiImpl_PreUpdate)
        return true;
    void* target = ResolveGameAddress(gAddr.InvestigateAiImpl_PreUpdate);
    const HookEntry state = CheckHookEntry(target, kPreUpdateEntry, sizeof(kPreUpdateEntry));
    if (state == HookEntry::Unknown)
    {
        char entry[160] = {};
        DescribeFunctionEntry(target, entry, sizeof(entry));
        LogError("SurveyProbe: InvestigateAiImpl::PreUpdate differs on %s (%s), no step trace", GetGameBuildName(gGameBuild), entry);
        return false;
    }
    if (state == HookEntry::ForeignHook)
        LogHookedOnTop("SurveyProbe: InvestigateAiImpl::PreUpdate", target);
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hk_PreUpdate), reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("SurveyProbe: InvestigateAiImpl::PreUpdate hook failed at %p, no step trace", target);
        g_Orig = nullptr;
        return false;
    }
    g_Target = target;
#endif
    return true;
}

void Uninstall_InvestigateAiImpl_PreUpdate_Hook()
{
#ifdef _DEBUG
    if (g_Target)
        DisableAndRemoveHook(g_Target);
    g_Target = nullptr;
    g_Orig   = nullptr;
#endif
}
