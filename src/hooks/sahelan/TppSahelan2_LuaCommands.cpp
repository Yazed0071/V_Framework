#include <pch.h>
#include "TppSahelan2_LuaCommands.h"

#include <cstdint>

#include "AddressSet.h"
#include "../fox/gm/GameObjectReinitialize.h"

namespace
{
    constexpr std::uint8_t kLuaCommandsEntry[] = {
        0x4C, 0x89, 0x4C, 0x24, 0x20, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41,
        0x57, 0x48, 0x8D, 0x6C, 0x24, 0xE1, 0x48, 0x81,
    };

    GameObjectReinitialize::LuaCommand_t g_OrigLuaCommands = nullptr;
    void*                                g_Target          = nullptr;

    std::uint64_t __fastcall hk_LuaCommands(void* handler, void* lua, std::uint32_t index, std::uint32_t command)
    {
        if (!GameObjectReinitialize::Claims(command))
            return g_OrigLuaCommands(handler, lua, index, command);
        GameObjectReinitialize::Report(GameObjectReinitialize::Sahelans(static_cast<std::uint8_t*>(handler), index));
        return 0;
    }
}

bool Install_TppSahelan2_LuaCommands_Hook()
{
    if (!g_Target)
        g_Target = GameObjectReinitialize::InstallHandlerHook("TppSahelan2_LuaCommands", gAddr.TppSahelan2_LuaCommands,
            kLuaCommandsEntry, nullptr, sizeof(kLuaCommandsEntry),
            reinterpret_cast<void*>(&hk_LuaCommands), reinterpret_cast<void**>(&g_OrigLuaCommands));
    return g_Target != nullptr;
}

void Uninstall_TppSahelan2_LuaCommands_Hook()
{
    GameObjectReinitialize::UninstallHandlerHook(g_Target);
    g_OrigLuaCommands = nullptr;
}
