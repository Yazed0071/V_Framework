#include "pch.h"
#include "DeployControllerImpl_GetNextRandomMissionId.h"

#include "DeployMissionRegistry.h"

#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "log.h"

namespace
{
    using GetNextRandomMissionId_t = std::uint16_t(__fastcall*)(std::uint8_t* impl, std::uint16_t current,
                                                                 std::uint8_t rarity, std::uint8_t altRarity);

    constexpr std::size_t kImplToIface = 0x20;

    GetNextRandomMissionId_t g_OrigGetNextRandomMissionId = nullptr;

    std::uint16_t __fastcall hkGetNextRandomMissionId(std::uint8_t* impl, std::uint16_t current,
                                                      std::uint8_t rarity, std::uint8_t altRarity)
    {
        std::uint8_t* iface = impl + kImplToIface;
        const bool normalBand = rarity == deployMission::kRarityN && altRarity == deployMission::kRarityN;
        if (!normalBand || !deployMission::IsArmed() || MissionCodeGuard::ShouldBypassHooks()
            || !deployMission::HasRotationMissions())
            return g_OrigGetNextRandomMissionId(impl, current, rarity, altRarity);

        const std::uint16_t from = deployMission::EffectiveRingPosition(iface, false);
        return deployMission::NextInRing(iface, false, deployMission::IsCustomId(from) ? from : current);
    }
}

bool Install_DeployControllerImpl_GetNextRandomMissionId_Hook()
{
    void* target = ResolveGameAddress(gAddr.DeployControllerImpl_GetNextRandomMissionId);
    const bool installed = CreateAndEnableHook(target,
        reinterpret_cast<void*>(&hkGetNextRandomMissionId),
        reinterpret_cast<void**>(&g_OrigGetNextRandomMissionId));
    if (!installed)
        LogError("Dispatch: GetNextRandomMissionId hook failed, custom dispatch missions off");
    return installed;
}

bool Uninstall_DeployControllerImpl_GetNextRandomMissionId_Hook()
{
    DisableAndRemoveHook(ResolveGameAddress(gAddr.DeployControllerImpl_GetNextRandomMissionId));
    g_OrigGetNextRandomMissionId = nullptr;
    return true;
}
