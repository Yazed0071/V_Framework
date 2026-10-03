#include "pch.h"

#include <Windows.h>
#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "SoldierFaction.h"

namespace
{
    using ShouldSleep_t = bool(__fastcall*)(void* self, std::uint32_t soldierIndex, void* const* context);

    ShouldSleep_t g_OrigShouldSleep = nullptr;

    bool __fastcall hk_ShouldSleep(void* self, std::uint32_t soldierIndex, void* const* context)
    {
        MISSION_GUARD_ORIGINAL_RET(g_OrigShouldSleep, self, soldierIndex, context);

        if (SoldierFaction_ShouldLeaveBody(self, soldierIndex, context, SoldierFactionBodyNode::DownComrade))
            return true;
        return g_OrigShouldSleep(self, soldierIndex, context);
    }
}

bool Install_NoticeDownComradeAiImpl_ShouldSleep_Hook()
{
    void* target = ResolveGameAddress(gAddr.NoticeDownComradeAiImpl_ShouldSleep);
    if (!target)
        return false;
    return CreateAndEnableHook(target, reinterpret_cast<void*>(&hk_ShouldSleep),
                               reinterpret_cast<void**>(&g_OrigShouldSleep));
}

bool Uninstall_NoticeDownComradeAiImpl_ShouldSleep_Hook()
{
    void* target = ResolveGameAddress(gAddr.NoticeDownComradeAiImpl_ShouldSleep);
    if (target)
        DisableAndRemoveHook(target);
    g_OrigShouldSleep = nullptr;
    return true;
}
