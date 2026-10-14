#include "pch.h"
#include "NoticeNoiseAiImpl_CheckTargetPositionUpdate.h"

#include <cstdint>

#include "AddressSet.h"
#include "ComradeTurn.h"
#include "HookUtils.h"
#include "log.h"
#include "MissionCodeGuard.h"

namespace
{
    using CheckTargetPositionUpdate_t = bool(__fastcall*)(std::uint8_t* noticeNoiseAi, std::uint32_t soldierIndex, void* knowledge);

    CheckTargetPositionUpdate_t g_Orig       = nullptr;
    void*                       g_HookTarget = nullptr;

    std::uintptr_t AddressFor(AddressSetRuntime::GameBuild build)   // 15.3 bodies are re-encoded; not found there yet
    {
        using GB = AddressSetRuntime::GameBuild;
        switch (build)
        {
        case GB::En_1_0_15_4:
        case GB::En_1_0_15_4a: return 0x14151E850ull;
        case GB::Jp_1_0_15_4:
        case GB::Jp_1_0_15_4a: return 0x14151EB80ull;
        default:               return 0;
        }
    }

    bool __fastcall hk_CheckTargetPositionUpdate(std::uint8_t* noticeNoiseAi, std::uint32_t soldierIndex, void* knowledge)
    {
        if (!MissionCodeGuard::ShouldBypassHooks() && CallComrade::IsWalkingOver(soldierIndex))
            return false;   // he heads for where the shout came from, not after the caller
        return g_Orig(noticeNoiseAi, soldierIndex, knowledge);
    }
}

bool Install_NoticeNoiseAiImpl_CheckTargetPositionUpdate_Hook()
{
    if (g_HookTarget)
        return true;

    const std::uintptr_t address = AddressFor(gGameBuild);
    if (!address)
    {
        LogWarn("CallComradeCommand: NoticeNoiseAiImpl::CheckTargetPositionUpdate unknown for %s, called comrades follow the caller",
                GetGameBuildName(gGameBuild));
        return true;
    }

    void* target = ResolveGameAddress(address);
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hk_CheckTargetPositionUpdate), reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("CallComradeCommand: NoticeNoiseAiImpl::CheckTargetPositionUpdate hook failed at %p, called comrades follow the caller", target);
        return false;
    }
    g_HookTarget = target;
    return true;
}

void Uninstall_NoticeNoiseAiImpl_CheckTargetPositionUpdate_Hook()
{
    if (g_HookTarget)
    {
        DisableAndRemoveHook(g_HookTarget);
        g_HookTarget = nullptr;
        g_Orig       = nullptr;
    }
}
