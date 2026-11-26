#include <pch.h>

#include <Windows.h>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <mutex>

#include "HookUtils.h"
#include "log.h"
#include "VIPHoldupHook.h"
#include "VIPImportant.h"
#include "MissionCodeGuard.h"
#include "PlayerAlertPhase.h"
#include "AddressSet.h"

namespace
{


    using State_StandRecoveryHoldup_t =
        void(__fastcall*)(void* self, std::uint32_t actorId, int proc, void* evt);


    static constexpr std::uint32_t HASH_EVENT_VOICE_NOTICE = 0x1077DB8Du;


    static constexpr std::uint32_t HASH_EVENT_HOLDUP_RECOVERY = 0x67926792u;


    static constexpr std::uint32_t HASH_HOLDUP_RECOVERY_VIP = 0x92D098DEu;


    static constexpr std::uint32_t HASH_HOLDUP_RECOVERY_NONVIP_CUSTOM = 0x97D0A0FEu;


    static constexpr ULONGLONG HOLDUP_RECOVERY_DISPATCH_COOLDOWN_MS = 6000ull;


    static constexpr ULONGLONG HOLDUP_VANILLA_SUPPRESS_MS = 4000ull;


    static State_StandRecoveryHoldup_t g_OrigState_StandRecoveryHoldup = nullptr;


    static bool UseCustomNonVipRecoveryNow()
    {
        return PlayerAlertPhase::IsAtOrAbove(PlayerAlertPhase::kCaution);
    }


    struct RecentRecoveryDispatch
    {
        std::uint16_t recoveredGameObjectId = 0xFFFFu;
        ULONGLONG lastDispatchTickMs = 0;
        ULONGLONG suppressVanillaUntilMs = 0;
    };


    struct RecentNonVipRecoverySuppress
    {
        ULONGLONG suppressUntilMs = 0;
    };


    static std::unordered_map<std::uint32_t, RecentRecoveryDispatch> g_RecentDispatchByActor;


    static std::unordered_set<std::uint32_t> g_CustomNonVipRecoveryPendingTargets;


    static std::unordered_map<std::uint32_t, RecentNonVipRecoverySuppress> g_RecentNonVipSuppressByTarget;


    static std::mutex g_HoldupMutex;
}


static std::uint32_t MakeCustomNonVipRecoveryKey(
    std::uint16_t recoveredGameObjectId,
    std::uint16_t recoveredSoldierIndex)
{
    if (recoveredGameObjectId != 0xFFFFu && recoveredGameObjectId != 0u)
        return static_cast<std::uint32_t>(recoveredGameObjectId);

    if (recoveredSoldierIndex != 0xFFFFu && recoveredSoldierIndex != 0u)
        return 0x80000000u | static_cast<std::uint32_t>(recoveredSoldierIndex);

    return 0u;
}


static bool TryResolveHoldupRecoveredTargetIds(
    void* self,
    std::uint32_t actorId,
    std::uint16_t& outRecoveredGameObjectId,
    std::uint16_t& outRecoveredSoldierIndex)
{
    outRecoveredGameObjectId = 0xFFFFu;
    outRecoveredSoldierIndex = 0xFFFFu;

    std::uint16_t recoveredGameObjectId = 0xFFFFu;
    if (!VIPImportant_ReadNoticeComrade(self, actorId, &recoveredGameObjectId, nullptr))
        return false;

    const std::uint16_t recoveredSoldierIndex = SoldierIndexOfGameObjectId(recoveredGameObjectId);

    outRecoveredGameObjectId = recoveredGameObjectId;
    outRecoveredSoldierIndex = recoveredSoldierIndex;

    return recoveredSoldierIndex != 0xFFFFu;
}


static bool ShouldDispatchHoldupRecovery(
    std::uint32_t actorId,
    std::uint16_t recoveredGameObjectId)
{
    if (recoveredGameObjectId == 0xFFFFu)
        return false;

    const ULONGLONG now = GetTickCount64();

    std::lock_guard<std::mutex> lock(g_HoldupMutex);

    auto& entry = g_RecentDispatchByActor[actorId];

    if (entry.recoveredGameObjectId == recoveredGameObjectId)
    {
        const ULONGLONG elapsed = now - entry.lastDispatchTickMs;
        if (elapsed <= HOLDUP_RECOVERY_DISPATCH_COOLDOWN_MS)
            return false;
    }

    entry.recoveredGameObjectId = recoveredGameObjectId;
    entry.lastDispatchTickMs = now;
    entry.suppressVanillaUntilMs = now + HOLDUP_VANILLA_SUPPRESS_MS;
    return true;
}


static bool ShouldSuppressVanillaVoiceNotice(
    std::uint32_t actorId,
    std::uint16_t recoveredGameObjectId)
{
    if (recoveredGameObjectId == 0xFFFFu)
        return false;

    const ULONGLONG now = GetTickCount64();

    std::lock_guard<std::mutex> lock(g_HoldupMutex);

    const auto it = g_RecentDispatchByActor.find(actorId);
    if (it == g_RecentDispatchByActor.end())
        return false;

    if (it->second.recoveredGameObjectId != recoveredGameObjectId)
        return false;

    return now <= it->second.suppressVanillaUntilMs;
}


static bool ShouldDispatchCustomNonVipRecovery(
    std::uint16_t recoveredGameObjectId,
    std::uint16_t recoveredSoldierIndex)
{
    const std::uint32_t key =
        MakeCustomNonVipRecoveryKey(recoveredGameObjectId, recoveredSoldierIndex);

    if (key == 0u)
        return false;

    std::lock_guard<std::mutex> lock(g_HoldupMutex);
    return g_CustomNonVipRecoveryPendingTargets.insert(key).second;
}


static void BeginCustomNonVipRecoverySuppressWindow(
    std::uint16_t recoveredGameObjectId,
    std::uint16_t recoveredSoldierIndex)
{
    const std::uint32_t key =
        MakeCustomNonVipRecoveryKey(recoveredGameObjectId, recoveredSoldierIndex);

    if (key == 0u)
        return;

    const ULONGLONG now = GetTickCount64();

    std::lock_guard<std::mutex> lock(g_HoldupMutex);
    g_RecentNonVipSuppressByTarget[key].suppressUntilMs = now + HOLDUP_VANILLA_SUPPRESS_MS;
}


static bool ShouldSuppressCustomNonVipRecoveryNow(
    std::uint16_t recoveredGameObjectId,
    std::uint16_t recoveredSoldierIndex)
{
    const std::uint32_t key =
        MakeCustomNonVipRecoveryKey(recoveredGameObjectId, recoveredSoldierIndex);

    if (key == 0u)
        return false;

    const ULONGLONG now = GetTickCount64();

    std::lock_guard<std::mutex> lock(g_HoldupMutex);

    const auto it = g_RecentNonVipSuppressByTarget.find(key);
    if (it == g_RecentNonVipSuppressByTarget.end())
        return false;

    return now <= it->second.suppressUntilMs;
}


static void __fastcall hkState_StandRecoveryHoldup(
    void* self,
    std::uint32_t actorId,
    int proc,
    void* evt)
{
    MISSION_GUARD_ORIGINAL_VOID(g_OrigState_StandRecoveryHoldup, self, actorId, proc, evt);

    if (proc == 6 && evt)
    {
        const bool useCustomNonVipRecovery = UseCustomNonVipRecoveryNow();

        const std::uint32_t eventHash = VIPImportant_EventHash(evt);

        std::uint16_t recoveredGameObjectId = 0xFFFFu;
        std::uint16_t recoveredSoldierIndex = 0xFFFFu;
        const bool resolvedTarget =
            TryResolveHoldupRecoveredTargetIds(
                self,
                actorId,
                recoveredGameObjectId,
                recoveredSoldierIndex);

        const bool isImportant =
            resolvedTarget &&
            IsVIPSoldier(VIPImportant_NoticeSoldiers(self), recoveredSoldierIndex);


        if (useCustomNonVipRecovery &&
            resolvedTarget &&
            !isImportant &&
            (eventHash == HASH_EVENT_HOLDUP_RECOVERY || eventHash == HASH_EVENT_VOICE_NOTICE) &&
            ShouldSuppressCustomNonVipRecoveryNow(recoveredGameObjectId, recoveredSoldierIndex))
        {
            return;
        }

        if (eventHash == HASH_EVENT_HOLDUP_RECOVERY)
        {
            if (isImportant)
            {
                if (ShouldDispatchHoldupRecovery(actorId, recoveredGameObjectId))
                {
                    VIPImportant_PlayVoiceLine(self, actorId, HASH_HOLDUP_RECOVERY_VIP);
                }


                return;
            }


            if (useCustomNonVipRecovery && resolvedTarget)
            {
                if (ShouldDispatchCustomNonVipRecovery(recoveredGameObjectId, recoveredSoldierIndex))
                {
                    VIPImportant_PlayVoiceLine(self, actorId, HASH_HOLDUP_RECOVERY_NONVIP_CUSTOM);
                    BeginCustomNonVipRecoverySuppressWindow(recoveredGameObjectId, recoveredSoldierIndex);
                }
                else
                {
                    BeginCustomNonVipRecoverySuppressWindow(recoveredGameObjectId, recoveredSoldierIndex);
                }


                return;
            }
        }


        if (eventHash == HASH_EVENT_VOICE_NOTICE)
        {
            if (isImportant)
            {
                if (ShouldSuppressVanillaVoiceNotice(actorId, recoveredGameObjectId))
                {
                    return;
                }
            }

            if (useCustomNonVipRecovery &&
                resolvedTarget &&
                !isImportant &&
                ShouldSuppressCustomNonVipRecoveryNow(recoveredGameObjectId, recoveredSoldierIndex))
            {
                return;
            }
        }
    }

    g_OrigState_StandRecoveryHoldup(self, actorId, proc, evt);
}


void VIPHoldup_ClearForReload()
{
    std::lock_guard<std::mutex> lock(g_HoldupMutex);
    g_RecentDispatchByActor.clear();
    g_CustomNonVipRecoveryPendingTargets.clear();
    g_RecentNonVipSuppressByTarget.clear();
}


bool Install_VIPHoldup_Hook()
{
    const bool ok = CreateAndEnableHook(
        ResolveGameAddress(gAddr.State_StandRecoveryHoldup),
        reinterpret_cast<void*>(&hkState_StandRecoveryHoldup),
        reinterpret_cast<void**>(&g_OrigState_StandRecoveryHoldup));

    if (!ok)
        LogError("Holdup: State_StandRecoveryHoldup hook failed, VIP holdup recovery voices off");
    return ok;
}


bool Uninstall_VIPHoldup_Hook()
{
    DisableAndRemoveHook(ResolveGameAddress(gAddr.State_StandRecoveryHoldup));
    g_OrigState_StandRecoveryHoldup = nullptr;

    VIPHoldup_ClearForReload();

    return true;
}
