#include "pch.h"
#include "DeployControllerImpl_GetDeployMissionNameMessageId.h"

#include "DeployMissionRegistry.h"

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    using GetDeployMissionNameMessageId_t = std::uint64_t* (__fastcall*)(std::uint8_t* iface,
                                                                          std::uint64_t* out,
                                                                          std::uint16_t id);

    GetDeployMissionNameMessageId_t g_OrigGetDeployMissionNameMessageId = nullptr;

    std::uint64_t* __fastcall hkGetDeployMissionNameMessageId(std::uint8_t* iface, std::uint64_t* out,
                                                              std::uint16_t id)
    {
        deployMission::MissionView mission;
        if (!out || !deployMission::FindMission(id, mission))
            return g_OrigGetDeployMissionNameMessageId(iface, out, id);

        if (mission.nameHash != 0)
        {
            *out = mission.nameHash;
            return out;
        }

        const std::uint16_t categoryTemplate =
            deployMission::VanillaPoolIdWithCategory(iface, mission.Category());
        return g_OrigGetDeployMissionNameMessageId(iface, out, categoryTemplate != 0 ? categoryTemplate : id);
    }
}

bool Install_DeployControllerImpl_GetDeployMissionNameMessageId_Hook()
{
    void* target = ResolveGameAddress(gAddr.DeployControllerImpl_GetDeployMissionNameMessageId);
    const bool installed = CreateAndEnableHook(target,
        reinterpret_cast<void*>(&hkGetDeployMissionNameMessageId),
        reinterpret_cast<void**>(&g_OrigGetDeployMissionNameMessageId));
    if (!installed)
        LogError("Dispatch: GetDeployMissionNameMessageId hook failed, custom dispatch missions off");
    return installed;
}

bool Uninstall_DeployControllerImpl_GetDeployMissionNameMessageId_Hook()
{
    DisableAndRemoveHook(ResolveGameAddress(gAddr.DeployControllerImpl_GetDeployMissionNameMessageId));
    g_OrigGetDeployMissionNameMessageId = nullptr;
    return true;
}
