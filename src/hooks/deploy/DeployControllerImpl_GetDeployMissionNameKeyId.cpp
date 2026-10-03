#include "pch.h"
#include "DeployControllerImpl_GetDeployMissionNameKeyId.h"

#include "DeployMissionRegistry.h"

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    using GetDeployMissionNameKeyId_t = std::uint16_t(__fastcall*)(std::uint8_t* impl, std::uint16_t id);

    GetDeployMissionNameKeyId_t g_OrigGetDeployMissionNameKeyId = nullptr;

    std::uint16_t __fastcall hkGetDeployMissionNameKeyId(std::uint8_t* impl, std::uint16_t id)
    {
        deployMission::MissionView mission;
        if (deployMission::FindMission(id, mission))
            return mission.Category();
        return g_OrigGetDeployMissionNameKeyId(impl, id);
    }
}

bool Install_DeployControllerImpl_GetDeployMissionNameKeyId_Hook()
{
    void* target = ResolveGameAddress(gAddr.DeployControllerImpl_GetDeployMissionNameKeyId);
    const bool installed = CreateAndEnableHook(target,
        reinterpret_cast<void*>(&hkGetDeployMissionNameKeyId),
        reinterpret_cast<void**>(&g_OrigGetDeployMissionNameKeyId));
    if (!installed)
        LogError("Dispatch: GetDeployMissionNameKeyId hook failed, custom dispatch missions off");
    return installed;
}

bool Uninstall_DeployControllerImpl_GetDeployMissionNameKeyId_Hook()
{
    DisableAndRemoveHook(ResolveGameAddress(gAddr.DeployControllerImpl_GetDeployMissionNameKeyId));
    g_OrigGetDeployMissionNameKeyId = nullptr;
    return true;
}
