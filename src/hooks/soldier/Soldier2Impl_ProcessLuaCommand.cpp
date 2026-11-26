#include <pch.h>
#include "Soldier2Impl_ProcessLuaCommand.h"

#include <cstdint>

#include "AddressSet.h"
#include "../fox/gm/GameObjectReinitialize.h"

namespace
{
    constexpr std::uint8_t kProcessLuaCommandEntry[] = {
        0x48, 0x8B, 0xC4, 0x4C, 0x89, 0x48, 0x20, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41,
        0x56, 0x41, 0x57, 0x48, 0x8D, 0xA8, 0xE8, 0xFC,
    };

    GameObjectReinitialize::LuaCommand_t g_OrigProcessLuaCommand = nullptr;
    void*                                g_Target                = nullptr;

    std::uint64_t __fastcall hk_ProcessLuaCommand(void* handler, void* lua, std::uint32_t index, std::uint32_t command)
    {
        if (!GameObjectReinitialize::Claims(command))
            return g_OrigProcessLuaCommand(handler, lua, index, command);
        GameObjectReinitialize::Report(GameObjectReinitialize::Soldiers(static_cast<std::uint8_t*>(handler), lua, index, g_OrigProcessLuaCommand));
        return 0;
    }
}

bool Install_Soldier2Impl_ProcessLuaCommand_Hook()
{
    if (!g_Target)
        g_Target = GameObjectReinitialize::InstallHandlerHook("Soldier2Impl::ProcessLuaCommand", gAddr.Soldier2Impl_ProcessLuaCommand,
            kProcessLuaCommandEntry, nullptr, sizeof(kProcessLuaCommandEntry),
            reinterpret_cast<void*>(&hk_ProcessLuaCommand), reinterpret_cast<void**>(&g_OrigProcessLuaCommand));
    return g_Target != nullptr;
}

void Uninstall_Soldier2Impl_ProcessLuaCommand_Hook()
{
    GameObjectReinitialize::UninstallHandlerHook(g_Target);
    g_OrigProcessLuaCommand = nullptr;
}
