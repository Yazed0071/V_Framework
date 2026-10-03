#include "pch.h"
#include "PoolRewardControllerImpl_GeneratePoolRewardsInfo.h"

#include "DeployMissionRegistry.h"

#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "log.h"

namespace
{
    using GeneratePoolRewardsInfo_t = void(__fastcall*)(void* rewardIface, void* rewardsInfo,
                                                         std::uint8_t forFob, std::uint16_t id,
                                                         std::uint16_t seed, std::uint8_t rank);

    constexpr std::uint16_t kFirstRewardRow = 0x86;

    GeneratePoolRewardsInfo_t g_OrigGeneratePoolRewardsInfo = nullptr;

    void __fastcall hkGeneratePoolRewardsInfo(void* rewardIface, void* rewardsInfo, std::uint8_t forFob,
                                              std::uint16_t id, std::uint16_t seed, std::uint8_t rank)
    {
        if (deployMission::IsCustomId(id) && !MissionCodeGuard::ShouldBypassHooks())
        {
            std::uint8_t row = 0;
            if (deployMission::RewardRowFor(id, row))
            {
                g_OrigGeneratePoolRewardsInfo(rewardIface, rewardsInfo, forFob, row, seed, rank);
                return;
            }
        }
        else if (id >= kFirstRewardRow && id < deployMission::kFirstCustomId)
        {
            deployMission::EnsureRewardRowsWritten();
        }
        g_OrigGeneratePoolRewardsInfo(rewardIface, rewardsInfo, forFob, id, seed, rank);
    }
}

bool Install_PoolRewardControllerImpl_GeneratePoolRewardsInfo_Hook()
{
    void* target = ResolveGameAddress(gAddr.PoolRewardControllerImpl_GeneratePoolRewardsInfo);
    const bool installed = CreateAndEnableHook(target,
        reinterpret_cast<void*>(&hkGeneratePoolRewardsInfo),
        reinterpret_cast<void**>(&g_OrigGeneratePoolRewardsInfo));
    if (!installed)
        LogError("Dispatch: GeneratePoolRewardsInfo hook failed, custom dispatch missions off");
    return installed;
}

bool Uninstall_PoolRewardControllerImpl_GeneratePoolRewardsInfo_Hook()
{
    DisableAndRemoveHook(ResolveGameAddress(gAddr.PoolRewardControllerImpl_GeneratePoolRewardsInfo));
    g_OrigGeneratePoolRewardsInfo = nullptr;
    return true;
}
