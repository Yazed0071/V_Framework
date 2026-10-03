#include "pch.h"
#include "DeployControllerImpl_AddLogMission.h"

#include "DeployMissionRegistry.h"

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    using AddLogMission_t = void(__fastcall*)(std::uint8_t* impl, std::uint8_t slot, bool win);

    constexpr std::size_t kImplToIface = 0x20;

    struct LoggedResult
    {
        bool         valid = false;
        std::uint8_t slot = 0;
        bool         win = false;
    };

    AddLogMission_t g_OrigAddLogMission = nullptr;
    thread_local LoggedResult t_LastResult;

    void __fastcall hkAddLogMission(std::uint8_t* impl, std::uint8_t slot, bool win)
    {
        std::uint8_t* iface = impl + kImplToIface;
        const std::uint8_t logSlot = deployMission::NextLogSlot(iface);
        if (logSlot != 0)
            deployMission::CopyFactToLogSlot(iface, slot, logSlot);

        t_LastResult.valid = true;
        t_LastResult.slot = slot;
        t_LastResult.win = win;

        g_OrigAddLogMission(impl, slot, win);
    }
}

bool TakeLoggedDeployResult(std::uint8_t slot, bool& win)
{
    if (!t_LastResult.valid || t_LastResult.slot != slot)
        return false;
    win = t_LastResult.win;
    t_LastResult.valid = false;
    return true;
}

bool Install_DeployControllerImpl_AddLogMission_Hook()
{
    void* target = ResolveGameAddress(gAddr.DeployControllerImpl_AddLogMission);
    const bool installed = CreateAndEnableHook(target,
        reinterpret_cast<void*>(&hkAddLogMission),
        reinterpret_cast<void**>(&g_OrigAddLogMission));
    if (!installed)
        LogError("Dispatch: AddLogMission hook failed, custom dispatch missions off");
    return installed;
}

bool Uninstall_DeployControllerImpl_AddLogMission_Hook()
{
    DisableAndRemoveHook(ResolveGameAddress(gAddr.DeployControllerImpl_AddLogMission));
    g_OrigAddLogMission = nullptr;
    return true;
}
