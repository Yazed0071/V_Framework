#include "pch.h"
#include "ActionController_SetCommand.h"

#include <cstdint>

#include "AddressSet.h"
#include "ComradeTurn.h"
#include "FakeSurrender.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    using SetCommand_t = void(__fastcall*)(std::uint8_t* actionController, std::uint32_t soldierIndex, std::uint8_t* command);

    SetCommand_t g_Orig       = nullptr;
    void*        g_HookTarget = nullptr;

    void __fastcall hk_SetCommand(std::uint8_t* actionController, std::uint32_t soldierIndex, std::uint8_t* command)
    {
        CallComrade::OnSetCommand(soldierIndex, command);
        FakeSurrender::OnSetCommand(soldierIndex, command);
        g_Orig(actionController, soldierIndex, command);
    }
}

bool Install_ActionController_SetCommand_Hook()
{
    if (g_HookTarget)
        return true;

    if (!gAddr.ActionController_SetCommand)
    {
        LogError("CallComradeCommand: ActionController::SetCommand address missing for %s, called comrades tilt their head instead of turning",
                 GetGameBuildName(gGameBuild));
        return true;
    }

    void* target = ResolveGameAddress(gAddr.ActionController_SetCommand);
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hk_SetCommand),
                             reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("CallComradeCommand: ActionController::SetCommand hook failed at %p, called comrades tilt their head instead of turning", target);
        return false;
    }

    g_HookTarget = target;
    return true;
}

void Uninstall_ActionController_SetCommand_Hook()
{
    CallComrade::StopTurn();
    FakeSurrender::Clear();
    if (g_HookTarget)
    {
        DisableAndRemoveHook(g_HookTarget);
        g_HookTarget = nullptr;
        g_Orig       = nullptr;
    }
}
