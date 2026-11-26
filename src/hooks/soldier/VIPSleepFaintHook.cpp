#include <pch.h>

#include <Windows.h>
#include <atomic>
#include <cstdint>

#include "HookUtils.h"
#include "log.h"
#include "VIPSleepFaintHook.h"
#include "VIPImportant.h"
#include "MissionCodeGuard.h"
#include "AddressSet.h"

namespace
{


    using State_RecoveryTouch_t =
        void(__fastcall*)(void* self, std::uint32_t actorId, std::uint32_t proc, void* evt);


    using State_RecoveryKick_t =
        void(__fastcall*)(void* self, std::uint32_t actorId, std::uint32_t proc, void* evt);


    static constexpr std::uint32_t HASH_EVENT_VOICE_NOTICE = 0x1077DB8Du;


    static constexpr std::uint32_t HASH_SLEEP_WAKE_OFFICER = 0x9CD0A89Cu;


    static State_RecoveryTouch_t g_OrigState_RecoveryTouch = nullptr;
    static State_RecoveryKick_t  g_OrigState_RecoveryKick  = nullptr;


    static std::atomic<unsigned> g_UnresolvedTargetLogs{0};
}


static bool TryInterceptRecoveryWake(
    void* self,
    std::uint32_t actorId,
    std::uint32_t proc,
    void* evt)
{
    if (proc != 6 || evt == nullptr)
        return false;

    if (VIPImportant_EventHash(evt) != HASH_EVENT_VOICE_NOTICE)
        return false;

    std::uint16_t downedGameObjectId = 0xFFFFu;
    const std::uint16_t sleeperIndex =
        VIPImportant_ReadNoticeComrade(self, actorId, &downedGameObjectId, nullptr)
            ? SoldierIndexOfGameObjectId(downedGameObjectId)
            : 0xFFFFu;
    if (sleeperIndex == 0xFFFFu)
    {
        if (g_UnresolvedTargetLogs.fetch_add(1u) < 8u)
            LogWarn("SleepFaint: actor=%u downed soldier not identified, vanilla wake line plays", actorId);

        return false;
    }

    if (!IsVIPSoldier(VIPImportant_NoticeSoldiers(self), sleeperIndex))
        return false;

    VIPImportant_PlayVoiceLine(self, actorId, HASH_SLEEP_WAKE_OFFICER);
    return true;
}


static void __fastcall hkState_RecoveryTouch(
    void* self,
    std::uint32_t actorId,
    std::uint32_t proc,
    void* evt)
{
    MISSION_GUARD_ORIGINAL_VOID(g_OrigState_RecoveryTouch, self, actorId, proc, evt);

    if (TryInterceptRecoveryWake(self, actorId, proc, evt))
        return;

    g_OrigState_RecoveryTouch(self, actorId, proc, evt);
}


static void __fastcall hkState_RecoveryKick(
    void* self,
    std::uint32_t actorId,
    std::uint32_t proc,
    void* evt)
{
    MISSION_GUARD_ORIGINAL_VOID(g_OrigState_RecoveryKick, self, actorId, proc, evt);

    if (TryInterceptRecoveryWake(self, actorId, proc, evt))
        return;

    g_OrigState_RecoveryKick(self, actorId, proc, evt);
}


bool Install_VIPSleepFaint_Hook()
{
    const bool okTouch = CreateAndEnableHook(
        ResolveGameAddress(gAddr.State_RecoveryTouch),
        reinterpret_cast<void*>(&hkState_RecoveryTouch),
        reinterpret_cast<void**>(&g_OrigState_RecoveryTouch));

    const bool okKick = CreateAndEnableHook(
        ResolveGameAddress(gAddr.State_RecoveryKick),
        reinterpret_cast<void*>(&hkState_RecoveryKick),
        reinterpret_cast<void**>(&g_OrigState_RecoveryKick));

    if (!okTouch)
        LogError("SleepFaint: State_RecoveryTouch hook failed, VIP wake line off for touch");
    if (!okKick)
        LogError("SleepFaint: State_RecoveryKick hook failed, VIP wake line off for kick");

    return okTouch && okKick;
}


bool Uninstall_VIPSleepFaint_Hook()
{
    DisableAndRemoveHook(ResolveGameAddress(gAddr.State_RecoveryTouch));
    DisableAndRemoveHook(ResolveGameAddress(gAddr.State_RecoveryKick));

    g_OrigState_RecoveryTouch = nullptr;
    g_OrigState_RecoveryKick  = nullptr;

    return true;
}
