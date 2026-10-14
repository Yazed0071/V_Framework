#include "pch.h"

#include <Windows.h>
#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "Soldier2Impl_ProcessSignal.h"
#include "SoldierFaction.h"

namespace
{
    using CheckNoiseNoticeSoldier_t = void(__fastcall*)(void* noticeController, std::uint32_t soldierIndex, void* unused,
                                                        const void* noise);

    CheckNoiseNoticeSoldier_t g_OrigCheckNoiseNoticeSoldier = nullptr;

    void __fastcall hk_CheckNoiseNoticeSoldier(void* noticeController, std::uint32_t soldierIndex, void* unused,
                                               const void* noise)
    {
        MISSION_GUARD_ORIGINAL_VOID(g_OrigCheckNoiseNoticeSoldier, noticeController, soldierIndex, unused, noise);

        if (CallComrade::IsDeafToNoise(noticeController, soldierIndex, noise))
            return;
        if (SoldierFaction_ShouldIgnoreNoise(noticeController, soldierIndex, noise))
            return;
        g_OrigCheckNoiseNoticeSoldier(noticeController, soldierIndex, unused, noise);
    }
}

bool Install_NoticeControllerImpl_CheckNoiseNoticeSoldier_Hook()
{
    void* target = ResolveGameAddress(gAddr.NoticeControllerImpl_CheckNoiseNoticeSoldier);
    if (!target)
        return false;
    return CreateAndEnableHook(target, reinterpret_cast<void*>(&hk_CheckNoiseNoticeSoldier),
                               reinterpret_cast<void**>(&g_OrigCheckNoiseNoticeSoldier));
}

bool Uninstall_NoticeControllerImpl_CheckNoiseNoticeSoldier_Hook()
{
    void* target = ResolveGameAddress(gAddr.NoticeControllerImpl_CheckNoiseNoticeSoldier);
    if (target)
        DisableAndRemoveHook(target);
    g_OrigCheckNoiseNoticeSoldier = nullptr;
    return true;
}
