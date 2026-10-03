#include "pch.h"
#include "DeployControllerImpl_CreateDeployMissionSeedParam.h"

#include "DeployMissionRegistry.h"

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"

#include <cstring>

namespace
{
    using CreateDeployMissionSeedParam_t = void(__fastcall*)(std::uint8_t* iface, void* seedParam,
                                                              const std::uint8_t* record,
                                                              std::uint16_t seed);

    CreateDeployMissionSeedParam_t g_OrigCreateDeployMissionSeedParam = nullptr;
    thread_local std::uint8_t t_CustomRecord[deployMission::kRecordSize];

    std::uint16_t PoolIdOfRecord(std::uint8_t* iface, const std::uint8_t* record)
    {
        const std::uint8_t* first = deployMission::VanillaRecord(iface, deployMission::kFirstPoolId);
        const std::uint8_t* last = deployMission::VanillaRecord(iface, deployMission::kLastPoolId);
        if (!first || !last || record < first || record > last)
            return 0;
        const std::size_t distance = static_cast<std::size_t>(record - first);
        if (distance % deployMission::kRecordSize != 0)
            return 0;
        return static_cast<std::uint16_t>(deployMission::kFirstPoolId + distance / deployMission::kRecordSize);
    }

    void __fastcall hkCreateDeployMissionSeedParam(std::uint8_t* iface, void* seedParam,
                                                   const std::uint8_t* record, std::uint16_t seed)
    {
        const std::uint16_t placeholder = PoolIdOfRecord(iface, record);
        deployMission::MissionView mission;
        if (placeholder != 0 && deployMission::FindLiveMissionByPair(iface, placeholder, seed, mission))
        {
            std::memcpy(t_CustomRecord, mission.record, deployMission::kRecordSize);
            t_CustomRecord[0] = 0;
            g_OrigCreateDeployMissionSeedParam(iface, seedParam, t_CustomRecord, seed);
            return;
        }
        g_OrigCreateDeployMissionSeedParam(iface, seedParam, record, seed);
    }
}

bool Install_DeployControllerImpl_CreateDeployMissionSeedParam_Hook()
{
    void* target = ResolveGameAddress(gAddr.DeployControllerImpl_CreateDeployMissionSeedParam);
    const bool installed = CreateAndEnableHook(target,
        reinterpret_cast<void*>(&hkCreateDeployMissionSeedParam),
        reinterpret_cast<void**>(&g_OrigCreateDeployMissionSeedParam));
    if (!installed)
        LogError("Dispatch: CreateDeployMissionSeedParam hook failed, custom dispatch missions off");
    return installed;
}

bool Uninstall_DeployControllerImpl_CreateDeployMissionSeedParam_Hook()
{
    DisableAndRemoveHook(ResolveGameAddress(gAddr.DeployControllerImpl_CreateDeployMissionSeedParam));
    g_OrigCreateDeployMissionSeedParam = nullptr;
    return true;
}
