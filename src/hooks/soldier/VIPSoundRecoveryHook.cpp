#include <pch.h>

#include <Windows.h>
#include <atomic>
#include <cstdint>

#include "HookUtils.h"
#include "log.h"
#include "VIPSoundRecoveryHook.h"
#include "VIPImportant.h"
#include "MissionCodeGuard.h"
#include "AddressSet.h"

namespace
{


    using State_SoundRecovery_t =
        void(__fastcall*)(void* self, std::uint32_t actorId, int proc, void* evt);


    static constexpr std::uint32_t HASH_EVENT_VOICE_NOTICE = 0x1077DB8Du;


    static constexpr std::uint32_t HASH_SLEEP_WAKE_OFFICER = 0x9CD0A89Cu;


    static constexpr std::uint32_t HASH_HOLDUP_RECOVERY_VIP = 0x92D098DEu;


    static constexpr std::uint8_t kNoticeKindHoldup = 0x0A;


    static State_SoundRecovery_t g_OrigState_SoundRecovery = nullptr;


    static std::atomic<unsigned> g_UnresolvedTargetLogs{0};
}


static void __fastcall hkState_SoundRecovery(
    void* self,
    std::uint32_t actorId,
    int proc,
    void* evt)
{
    MISSION_GUARD_ORIGINAL_VOID(g_OrigState_SoundRecovery, self, actorId, proc, evt);

    if (proc == 6 && evt && VIPImportant_EventHash(evt) == HASH_EVENT_VOICE_NOTICE)
    {
        std::uint16_t downedGameObjectId = 0xFFFFu;
        std::uint8_t noticeKind = 0;
        if (!VIPImportant_ReadNoticeComrade(self, actorId, &downedGameObjectId, &noticeKind))
        {
            if (g_UnresolvedTargetLogs.fetch_add(1u) < 8u)
                LogWarn("SoundRecovery: actor %u: notice record unreadable, vanilla line plays", actorId);
        }
        else
        {
            const std::uint16_t soldierIndex = SoldierIndexOfGameObjectId(downedGameObjectId);

            if (soldierIndex == 0xFFFFu && g_UnresolvedTargetLogs.fetch_add(1u) < 8u)
                LogWarn("SoundRecovery: actor %u: downed soldier unresolved, vanilla line plays",
                    actorId);

            if (soldierIndex != 0xFFFFu && IsVIPSoldier(VIPImportant_NoticeSoldiers(self), soldierIndex))
            {
                const std::uint32_t replacementHash =
                    (noticeKind == kNoticeKindHoldup)
                        ? HASH_HOLDUP_RECOVERY_VIP
                        : HASH_SLEEP_WAKE_OFFICER;

                VIPImportant_PlayVoiceLine(self, actorId, replacementHash);
                return;
            }
        }
    }

    if (g_OrigState_SoundRecovery)
        g_OrigState_SoundRecovery(self, actorId, proc, evt);
}


bool Install_VIPSoundRecovery_Hook()
{
    void* target = ResolveGameAddress(
        gAddr.State_StandEnterRecoverySleepFaintHoldupComradeBySound);

    if (!target)
    {
        LogError("SoundRecovery: no address for this build, VIP recovery lines off");
        return false;
    }

    const bool ok = CreateAndEnableHook(
        target,
        reinterpret_cast<void*>(&hkState_SoundRecovery),
        reinterpret_cast<void**>(&g_OrigState_SoundRecovery));

    if (!ok)
        LogError("SoundRecovery: hook failed @ %p, VIP recovery lines off", target);

    return ok;
}


bool Uninstall_VIPSoundRecovery_Hook()
{
    DisableAndRemoveHook(
        ResolveGameAddress(gAddr.State_StandEnterRecoverySleepFaintHoldupComradeBySound));

    g_OrigState_SoundRecovery = nullptr;

    return true;
}
