#include <pch.h>
#include "SecurityCamera2Impl_ProcessLuaCommand.h"

#include <cstdint>

#include "AddressSet.h"
#include "../fox/gm/GameObjectReinitialize.h"

namespace
{
    constexpr std::uint8_t kProcessLuaCommandEntry[] = {
        0x4C, 0x89, 0x4C, 0x24, 0x20, 0x53, 0x55, 0x56, 0x57, 0x41, 0x56, 0x41, 0x57, 0x48, 0x81, 0xEC,
        0x88, 0x00, 0x00, 0x00,
    };

    GameObjectReinitialize::LuaCommand_t g_OrigProcessLuaCommand = nullptr;
    void*                                g_Target                = nullptr;

    std::uint64_t __fastcall hk_ProcessLuaCommand(void* handler, void* lua, std::uint32_t index, std::uint32_t command)
    {
        if (!GameObjectReinitialize::Claims(command))
            return g_OrigProcessLuaCommand(handler, lua, index, command);
        GameObjectReinitialize::Report(GameObjectReinitialize::SecurityCameras(static_cast<std::uint8_t*>(handler), index));
        return 0;
    }
}

bool Install_SecurityCamera2Impl_ProcessLuaCommand_Hook()
{
    if (!g_Target)
        g_Target = GameObjectReinitialize::InstallHandlerHook("SecurityCamera2Impl::ProcessLuaCommand", gAddr.SecurityCamera2Impl_ProcessLuaCommand,
            kProcessLuaCommandEntry, nullptr, sizeof(kProcessLuaCommandEntry),
            reinterpret_cast<void*>(&hk_ProcessLuaCommand), reinterpret_cast<void**>(&g_OrigProcessLuaCommand));
    return g_Target != nullptr;
}

void Uninstall_SecurityCamera2Impl_ProcessLuaCommand_Hook()
{
    GameObjectReinitialize::UninstallHandlerHook(g_Target);
    g_OrigProcessLuaCommand = nullptr;
}
