#include "pch.h"
#include "DeployControllerImpl_CompletedDeployMissionOffline.h"

#include "DeployControllerImpl_AddLogMission.h"
#include "DeployMissionRegistry.h"

#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "log.h"

namespace
{
    using CompletedDeployMissionOffline_t = void(__fastcall*)(std::uint8_t* iface, std::uint8_t* missionInfo,
                                                               void* seedParam, std::uint8_t firstCompletion);

    constexpr std::size_t kInfoRank          = 0x03;
    constexpr std::size_t kInfoMissionId     = 0xA4;
    constexpr std::size_t kInfoSlot          = 0xA6;
    constexpr std::size_t kNewestNOffset     = 0x1E60;
    constexpr std::size_t kOpenedRareOffset  = 0x1E70;
    constexpr std::size_t kNormalWinsOffset  = 0x1E78;
    constexpr std::size_t kNormalCountOffset = 0x1E90;
    constexpr std::size_t kDirtyOffset       = 0x79D8;

    CompletedDeployMissionOffline_t g_OrigCompletedDeployMissionOffline = nullptr;

    struct Completion
    {
        std::uint16_t id = 0;
        std::uint8_t  slot = 0;
        std::uint8_t  rank = 0;
        std::uint16_t normalCursorBefore = 0;
    };

    bool ReadCompletion(std::uint8_t* iface, std::uint8_t* missionInfo, Completion& out)
    {
        return deployMission::ReadEngine(missionInfo + kInfoMissionId, out.id)
            && deployMission::ReadEngine(missionInfo + kInfoSlot, out.slot)
            && deployMission::ReadEngine(missionInfo + kInfoRank, out.rank)
            && deployMission::ReadEngine(deployMission::EnginePointerAt<std::uint16_t>(iface, kNewestNOffset),
                                         out.normalCursorBefore);
    }

    void RestoreVanillaCursor(std::uint8_t* iface, const Completion& completion)
    {
        std::uint16_t cursor = 0;
        if (!deployMission::ReadEngine(deployMission::EnginePointerAt<std::uint16_t>(iface, kNewestNOffset), cursor))
            return;
        if (cursor >= deployMission::kVanillaIdBound)
            deployMission::AdoptCustomCursor(iface, false, cursor, completion.normalCursorBefore);
    }

    bool RecordNormalWinAndKeepRotating(std::uint8_t* iface)
    {
        std::uint8_t* wins = deployMission::EnginePointerAt<std::uint8_t>(iface, kNormalWinsOffset);
        std::uint8_t* openedRare = deployMission::EnginePointerAt<std::uint8_t>(iface, kOpenedRareOffset);
        std::uint16_t normalCount16 = 0;
        std::uint32_t normalCount32 = 0;
        std::uint8_t winCount = 0;
        if (!deployMission::ReadEngine(wins, winCount)
            || !deployMission::ReadEngine(iface + kNormalCountOffset, normalCount16)
            || !deployMission::ReadEngine(iface + kNormalCountOffset, normalCount32))
            return false;

        const std::uint16_t next = static_cast<std::uint16_t>(winCount + 1);
        const std::uint8_t stored = static_cast<std::uint8_t>(next < normalCount16 ? next : normalCount16);
        deployMission::WriteEngine(wins, stored);
        if (stored < normalCount32)
            return true;

        const std::uint8_t opened = 1;
        deployMission::WriteEngine(openedRare, opened);
        return false;
    }

    void ContinueCustomRotation(std::uint8_t* iface, const Completion& completion)
    {
        deployMission::MissionView mission;
        if (!deployMission::FindMission(completion.id, mission) || mission.Rarity() == deployMission::kRarityNone)
            return;
        if (completion.slot == deployMission::kTutorialSlot || MissionCodeGuard::ShouldBypassHooks())
            return;

        std::uint8_t openedRare = 0;
        if (!deployMission::ReadEngine(deployMission::EnginePointerAt<std::uint8_t>(iface, kOpenedRareOffset), openedRare)
            || openedRare != 0)
            return;

        bool win = false;
        if (!TakeLoggedDeployResult(completion.slot, win))
            return;

        if (!win)
        {
            const std::uint16_t seed = deployMission::CallCreateSeed(iface);
            deployMission::CallGenerate(iface, completion.slot, completion.id, seed, completion.rank);
        }
        else if (RecordNormalWinAndKeepRotating(iface))
        {
            const std::uint16_t from = deployMission::EffectiveRingPosition(iface, false);
            const std::uint16_t next = deployMission::NextInRing(iface, false, from);
            if (next > from)
            {
                deployMission::CommitRingPick(iface, false, next);
                const std::uint16_t seed = deployMission::CallCreateSeed(iface);
                deployMission::CallGenerate(iface, completion.slot, next, seed, completion.rank);
            }
        }

        const std::uint8_t dirty = 1;
        deployMission::WriteEngine(iface + kDirtyOffset, dirty);
    }

    void __fastcall hkCompletedDeployMissionOffline(std::uint8_t* iface, std::uint8_t* missionInfo,
                                                    void* seedParam, std::uint8_t firstCompletion)
    {
        Completion completion;
        const bool known = missionInfo && ReadCompletion(iface, missionInfo, completion);

        g_OrigCompletedDeployMissionOffline(iface, missionInfo, seedParam, firstCompletion);

        if (!known)
            return;
        RestoreVanillaCursor(iface, completion);
        if (!deployMission::IsCustomId(completion.id))
            return;
        deployMission::MarkCompleted(completion.id);

        deployMission::MissionView mission;
        if (deployMission::FindMission(completion.id, mission) && mission.revengeMissionId != 0
            && !MissionCodeGuard::ShouldBypassHooks())
            deployMission::CallSetRevengeCleared(iface, mission.revengeMissionId);

        ContinueCustomRotation(iface, completion);
    }
}

bool Install_DeployControllerImpl_CompletedDeployMissionOffline_Hook()
{
    void* target = ResolveGameAddress(gAddr.DeployControllerImpl_CompletedDeployMissionOffline);
    const bool installed = CreateAndEnableHook(target,
        reinterpret_cast<void*>(&hkCompletedDeployMissionOffline),
        reinterpret_cast<void**>(&g_OrigCompletedDeployMissionOffline));
    if (!installed)
        LogError("Dispatch: CompletedDeployMissionOffline hook failed, custom dispatch missions off");
    return installed;
}

bool Uninstall_DeployControllerImpl_CompletedDeployMissionOffline_Hook()
{
    DisableAndRemoveHook(ResolveGameAddress(gAddr.DeployControllerImpl_CompletedDeployMissionOffline));
    g_OrigCompletedDeployMissionOffline = nullptr;
    return true;
}
