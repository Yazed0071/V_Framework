#include "pch.h"
#include "DeployControllerImpl_GetDeployMissionCaptionMessageId.h"

#include "DeployMissionRegistry.h"

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    using GetDeployMissionCaptionMessageId_t = std::uint64_t* (__fastcall*)(std::uint8_t* iface,
                                                                             std::uint64_t* out,
                                                                             std::uint16_t id);

    GetDeployMissionCaptionMessageId_t g_OrigGetDeployMissionCaptionMessageId = nullptr;

    std::uint64_t* __fastcall hkGetDeployMissionCaptionMessageId(std::uint8_t* iface, std::uint64_t* out,
                                                                 std::uint16_t id)
    {
        deployMission::MissionView mission;
        if (!out || !deployMission::FindMission(id, mission))
            return g_OrigGetDeployMissionCaptionMessageId(iface, out, id);

        if (mission.infoHash != 0)
        {
            *out = mission.infoHash;
            return out;
        }

        const std::uint16_t categoryTemplate =
            deployMission::VanillaPoolIdWithCategory(iface, mission.Category());
        return g_OrigGetDeployMissionCaptionMessageId(iface, out, categoryTemplate != 0 ? categoryTemplate : id);
    }
}

bool Install_DeployControllerImpl_GetDeployMissionCaptionMessageId_Hook()
{
    void* target = ResolveGameAddress(gAddr.DeployControllerImpl_GetDeployMissionCaptionMessageId);
    const bool installed = CreateAndEnableHook(target,
        reinterpret_cast<void*>(&hkGetDeployMissionCaptionMessageId),
        reinterpret_cast<void**>(&g_OrigGetDeployMissionCaptionMessageId));
    if (!installed)
        LogError("Dispatch: GetDeployMissionCaptionMessageId hook failed, custom dispatch missions off");
    return installed;
}

bool Uninstall_DeployControllerImpl_GetDeployMissionCaptionMessageId_Hook()
{
    DisableAndRemoveHook(ResolveGameAddress(gAddr.DeployControllerImpl_GetDeployMissionCaptionMessageId));
    g_OrigGetDeployMissionCaptionMessageId = nullptr;
    return true;
}
