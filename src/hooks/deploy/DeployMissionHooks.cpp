#include "pch.h"
#include "DeployMissionHooks.h"

#include "CombatDeployCallbackImpl_RefreshMissionIconInGlobe.h"
#include "CombatDeployCallbackImpl_RefreshMissionList.h"
#include "CombatDeployMissionListRecordCallFunc_Refresh.h"
#include "DeployControllerImpl_AddLogMission.h"
#include "DeployControllerImpl_CompletedDeployMissionOffline.h"
#include "DeployControllerImpl_CreateDeployMissionSeedParam.h"
#include "DeployControllerImpl_GenerateDeployMission.h"
#include "DeployControllerImpl_GenerateDeployMissionInfoOffline.h"
#include "DeployControllerImpl_GetDeployMissionCaptionMessageId.h"
#include "DeployControllerImpl_GetDeployMissionNameKeyId.h"
#include "DeployControllerImpl_GetDeployMissionNameMessageId.h"
#include "DeployControllerImpl_GetNextRandomMissionId.h"
#include "DeployControllerImpl_RefreshMissionList.h"
#include "DeployControllerImpl_RegisterDeployMissionParam.h"
#include "MotherBaseManagementSystemImpl_SetupAfterRestoreFromSVars.h"
#include "PoolRewardControllerImpl_GeneratePoolRewardsInfo.h"
#include "PoolRewardControllerImpl_RegisterPoolRewardParamDeploy.h"
#include "DeployMissionRegistry.h"
#include "MbTopMenuSelectorEv_UpdateMenuNotice.h"

#include "AddressSet.h"
#include "log.h"

#include <atomic>
#include <cstdint>
#include <iterator>

namespace
{
    enum class InstallState { NotAttempted, Installed, Failed };

    std::atomic<InstallState> g_State{ InstallState::NotAttempted };

    struct HookEntry
    {
        const char*      field;
        const uintptr_t* address;
        bool (*install)();
        bool (*uninstall)();
    };

    const HookEntry kHooks[] =
    {
        { "DeployControllerImpl_GenerateDeployMission", &gAddr.DeployControllerImpl_GenerateDeployMission,
          &Install_DeployControllerImpl_GenerateDeployMission_Hook, &Uninstall_DeployControllerImpl_GenerateDeployMission_Hook },
        { "DeployControllerImpl_AddLogMission", &gAddr.DeployControllerImpl_AddLogMission,
          &Install_DeployControllerImpl_AddLogMission_Hook, &Uninstall_DeployControllerImpl_AddLogMission_Hook },
        { "DeployControllerImpl_GenerateDeployMissionInfoOffline", &gAddr.DeployControllerImpl_GenerateDeployMissionInfoOffline,
          &Install_DeployControllerImpl_GenerateDeployMissionInfoOffline_Hook, &Uninstall_DeployControllerImpl_GenerateDeployMissionInfoOffline_Hook },
        { "DeployControllerImpl_CreateDeployMissionSeedParam", &gAddr.DeployControllerImpl_CreateDeployMissionSeedParam,
          &Install_DeployControllerImpl_CreateDeployMissionSeedParam_Hook, &Uninstall_DeployControllerImpl_CreateDeployMissionSeedParam_Hook },
        { "DeployControllerImpl_GetDeployMissionNameMessageId", &gAddr.DeployControllerImpl_GetDeployMissionNameMessageId,
          &Install_DeployControllerImpl_GetDeployMissionNameMessageId_Hook, &Uninstall_DeployControllerImpl_GetDeployMissionNameMessageId_Hook },
        { "DeployControllerImpl_GetDeployMissionCaptionMessageId", &gAddr.DeployControllerImpl_GetDeployMissionCaptionMessageId,
          &Install_DeployControllerImpl_GetDeployMissionCaptionMessageId_Hook, &Uninstall_DeployControllerImpl_GetDeployMissionCaptionMessageId_Hook },
        { "DeployControllerImpl_GetDeployMissionNameKeyId", &gAddr.DeployControllerImpl_GetDeployMissionNameKeyId,
          &Install_DeployControllerImpl_GetDeployMissionNameKeyId_Hook, &Uninstall_DeployControllerImpl_GetDeployMissionNameKeyId_Hook },
        { "DeployControllerImpl_RefreshMissionList", &gAddr.DeployControllerImpl_RefreshMissionList,
          &Install_DeployControllerImpl_RefreshMissionList_Hook, &Uninstall_DeployControllerImpl_RefreshMissionList_Hook },
        { "DeployControllerImpl_GetNextRandomMissionId", &gAddr.DeployControllerImpl_GetNextRandomMissionId,
          &Install_DeployControllerImpl_GetNextRandomMissionId_Hook, &Uninstall_DeployControllerImpl_GetNextRandomMissionId_Hook },
        { "DeployControllerImpl_CompletedDeployMissionOffline", &gAddr.DeployControllerImpl_CompletedDeployMissionOffline,
          &Install_DeployControllerImpl_CompletedDeployMissionOffline_Hook, &Uninstall_DeployControllerImpl_CompletedDeployMissionOffline_Hook },
        { "MotherBaseManagementSystemImpl_SetupAfterRestoreFromSVars", &gAddr.MotherBaseManagementSystemImpl_SetupAfterRestoreFromSVars,
          &Install_MotherBaseManagementSystemImpl_SetupAfterRestoreFromSVars_Hook, &Uninstall_MotherBaseManagementSystemImpl_SetupAfterRestoreFromSVars_Hook },
        { "PoolRewardControllerImpl_GeneratePoolRewardsInfo", &gAddr.PoolRewardControllerImpl_GeneratePoolRewardsInfo,
          &Install_PoolRewardControllerImpl_GeneratePoolRewardsInfo_Hook, &Uninstall_PoolRewardControllerImpl_GeneratePoolRewardsInfo_Hook },
        { "CombatDeployCallbackImpl_RefreshMissionIconInGlobe", &gAddr.CombatDeployCallbackImpl_RefreshMissionIconInGlobe,
          &Install_CombatDeployCallbackImpl_RefreshMissionIconInGlobe_Hook, &Uninstall_CombatDeployCallbackImpl_RefreshMissionIconInGlobe_Hook },
        { "CombatDeployMissionListRecordCallFunc_Refresh", &gAddr.CombatDeployMissionListRecordCallFunc_Refresh,
          &Install_CombatDeployMissionListRecordCallFunc_Refresh_Hook, &Uninstall_CombatDeployMissionListRecordCallFunc_Refresh_Hook },
        { "CombatDeployCallbackImpl_RefreshMissionList", &gAddr.CombatDeployCallbackImpl_RefreshMissionList,
          &Install_CombatDeployCallbackImpl_RefreshMissionList_Hook, &Uninstall_CombatDeployCallbackImpl_RefreshMissionList_Hook },
        { "MbTopMenuSelectorEv_UpdateMenuNotice", &gAddr.MbTopMenuSelectorEv_UpdateMenuNotice,
          &Install_MbTopMenuSelectorEv_UpdateMenuNotice_Hook, &Uninstall_MbTopMenuSelectorEv_UpdateMenuNotice_Hook },
    };

    const char* FirstUnportedField()
    {
        for (const HookEntry& hook : kHooks)
            if (*hook.address == 0)
                return hook.field;
        return nullptr;
    }

    void UninstallFirst(std::size_t count)
    {
        for (std::size_t i = count; i > 0; --i)
            kHooks[i - 1].uninstall();
    }
}

bool Install_DeployMission_Hooks()
{
    if (const char* missing = FirstUnportedField())
    {
        g_State = InstallState::Failed;
        LogError("Dispatch: %s has no address for %s, custom dispatch missions off",
            missing, GetGameBuildName(gGameBuild));
        return true;
    }

    for (std::size_t i = 0; i < std::size(kHooks); ++i)
    {
        if (!kHooks[i].install())
        {
            UninstallFirst(i);
            g_State = InstallState::Failed;
            return false;
        }
    }

    deployMission::Initialize();
    EnsureDeployMissionCaptureInstalled();
    EnsureDeployRewardCaptureInstalled();
    g_State = InstallState::Installed;
    return true;
}

void Uninstall_DeployMission_Hooks()
{
    Uninstall_DeployControllerImpl_RegisterDeployMissionParam_Capture();
    Uninstall_PoolRewardControllerImpl_RegisterPoolRewardParamDeploy_Capture();
    if (g_State == InstallState::Installed)
        UninstallFirst(std::size(kHooks));
    g_State = InstallState::NotAttempted;
}

bool DeployMissionHooksFailed()
{
    return g_State == InstallState::Failed;
}
