#include "pch.h"
#include "MotherBaseManagementSystemImpl_SetupAfterRestoreFromSVars.h"

#include "DeployMissionRegistry.h"

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    using SetupAfterRestoreFromSVars_t = void(__fastcall*)(void* managementSystem);

    SetupAfterRestoreFromSVars_t g_OrigSetupAfterRestoreFromSVars = nullptr;

    void __fastcall hkSetupAfterRestoreFromSVars(void* managementSystem)
    {
        g_OrigSetupAfterRestoreFromSVars(managementSystem);
        deployMission::OnRestorePoint();
    }
}

bool Install_MotherBaseManagementSystemImpl_SetupAfterRestoreFromSVars_Hook()
{
    void* target = ResolveGameAddress(gAddr.MotherBaseManagementSystemImpl_SetupAfterRestoreFromSVars);
    const bool installed = CreateAndEnableHook(target,
        reinterpret_cast<void*>(&hkSetupAfterRestoreFromSVars),
        reinterpret_cast<void**>(&g_OrigSetupAfterRestoreFromSVars));
    if (!installed)
        LogError("Dispatch: SetupAfterRestoreFromSVars hook failed, custom dispatch missions off");
    return installed;
}

bool Uninstall_MotherBaseManagementSystemImpl_SetupAfterRestoreFromSVars_Hook()
{
    DisableAndRemoveHook(ResolveGameAddress(gAddr.MotherBaseManagementSystemImpl_SetupAfterRestoreFromSVars));
    g_OrigSetupAfterRestoreFromSVars = nullptr;
    return true;
}
