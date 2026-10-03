#include "pch.h"
#include "DeployControllerImpl_GenerateDeployMissionInfoOffline.h"

#include "CombatDeployCallbackImpl_RefreshMissionList.h"
#include "DeployMissionRegistry.h"

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"

#include <cstring>

namespace
{
    using GenerateDeployMissionInfoOffline_t = void(__fastcall*)(std::uint8_t* iface,
                                                                  std::uint8_t* missionInfo,
                                                                  std::uint8_t slot);

    constexpr std::size_t kInfoRewardId  = 0xA2;
    constexpr std::size_t kInfoMissionId = 0xA4;
    constexpr std::size_t kInfoNameId    = 0xA8;

    GenerateDeployMissionInfoOffline_t g_OrigGenerateDeployMissionInfoOffline = nullptr;

    void ApplyCustomMission(std::uint8_t* missionInfo, const deployMission::MissionView& mission)
    {
        std::uint8_t record[deployMission::kRecordSize] = {};
        std::memcpy(record, mission.record, deployMission::kRecordSize);
        record[0] = 0;
        deployMission::CopyEngineMemory(missionInfo, record, deployMission::kRecordSize);

        const std::uint16_t rewardId = mission.rewardRow;
        deployMission::WriteEngine(missionInfo + kInfoRewardId, rewardId);
        deployMission::WriteEngine(missionInfo + kInfoMissionId, mission.id);
        deployMission::WriteEngine(missionInfo + kInfoNameId, mission.id);

        if (mission.rewardRow != 0)
            deployMission::EnsureRewardRowsWritten();
    }

    void __fastcall hkGenerateDeployMissionInfoOffline(std::uint8_t* iface, std::uint8_t* missionInfo,
                                                       std::uint8_t slot)
    {
        g_OrigGenerateDeployMissionInfoOffline(iface, missionInfo, slot);

        deployMission::MissionView mission;
        if (missionInfo && deployMission::FindLiveMissionInSlot(iface, slot, mission))
            ApplyCustomMission(missionInfo, mission);
        if (missionInfo)
            ArrangeMissionListRow(missionInfo);
    }
}

bool Install_DeployControllerImpl_GenerateDeployMissionInfoOffline_Hook()
{
    void* target = ResolveGameAddress(gAddr.DeployControllerImpl_GenerateDeployMissionInfoOffline);
    const bool installed = CreateAndEnableHook(target,
        reinterpret_cast<void*>(&hkGenerateDeployMissionInfoOffline),
        reinterpret_cast<void**>(&g_OrigGenerateDeployMissionInfoOffline));
    if (!installed)
        LogError("Dispatch: GenerateDeployMissionInfoOffline hook failed, custom dispatch missions off");
    return installed;
}

bool Uninstall_DeployControllerImpl_GenerateDeployMissionInfoOffline_Hook()
{
    DisableAndRemoveHook(ResolveGameAddress(gAddr.DeployControllerImpl_GenerateDeployMissionInfoOffline));
    g_OrigGenerateDeployMissionInfoOffline = nullptr;
    return true;
}
