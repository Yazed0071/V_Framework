#include <pch.h>
#include "UavImpl_ProcessLuaCommand.h"

#include <cstdint>

#include "AddressSet.h"
#include "log.h"
#include "../fox/gm/GameObjectReinitialize.h"

namespace
{
    constexpr std::uint8_t kProcessLuaCommandEntry[] = {
        0x48, 0x89, 0x6C, 0x24, 0x18, 0x4C, 0x89, 0x4C, 0x24, 0x20, 0x56, 0x57, 0x41, 0x56, 0x48, 0x83,
        0xEC, 0x30, 0x41, 0x8B, 0xE8, 0x48, 0x8B, 0xF2, 0x4C, 0x8B, 0xF1,
    };

    GameObjectReinitialize::LuaCommand_t g_OrigProcessLuaCommand = nullptr;
    void*                                g_Target                = nullptr;

    std::uint64_t __fastcall hk_ProcessLuaCommand(void* handler, void* lua, std::uint32_t index, std::uint32_t command)
    {
        LogDebug("UavRouteHandles: Lua command 0x%08X for UAV %u", command, index);
        if (!GameObjectReinitialize::Claims(command))
            return g_OrigProcessLuaCommand(handler, lua, index, command);
        GameObjectReinitialize::Report(GameObjectReinitialize::Uavs(static_cast<std::uint8_t*>(handler), index));
        return 0;
    }
}

bool Install_UavImpl_ProcessLuaCommand_Hook()
{
    if (!g_Target)
        g_Target = GameObjectReinitialize::InstallHandlerHook("UavImpl::ProcessLuaCommand", gAddr.UavImpl_ProcessLuaCommand,
            kProcessLuaCommandEntry, nullptr, sizeof(kProcessLuaCommandEntry),
            reinterpret_cast<void*>(&hk_ProcessLuaCommand), reinterpret_cast<void**>(&g_OrigProcessLuaCommand));
    return g_Target != nullptr;
}

void Uninstall_UavImpl_ProcessLuaCommand_Hook()
{
    GameObjectReinitialize::UninstallHandlerHook(g_Target);
    g_OrigProcessLuaCommand = nullptr;
}
