#include <pch.h>
#include "TppVehicle2_LuaCommands.h"

#include <cstdint>

#include "AddressSet.h"
#include "CustomVehicles.h"
#include "../fox/gm/GameObjectReinitialize.h"

namespace
{
    constexpr std::uint8_t kLuaCommandsEntry[] = {
        0x40, 0x53, 0x55, 0x56, 0x57, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x50, 0x48, 0x8B, 0x05, 0x6E, 0x8D,
        0x01, 0x02, 0x48, 0x33, 0xC4, 0x48, 0x89, 0x44, 0x24, 0x48, 0x45, 0x33, 0xF6, 0x45, 0x8B, 0xD0,
    };
    constexpr std::uint8_t kLuaCommandsEntryMask[] = {
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0,
        0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    };

    GameObjectReinitialize::LuaCommand_t g_OrigLuaCommands = nullptr;
    void*                                g_Target          = nullptr;

    std::uint64_t __fastcall hk_LuaCommands(void* vehicles, void* lua, std::uint32_t index, std::uint32_t command)
    {
        if (GameObjectReinitialize::Claims(command))
        {
            GameObjectReinitialize::Report(GameObjectReinitialize::Vehicles(static_cast<std::uint8_t*>(vehicles), lua, index, g_OrigLuaCommands));
            return 0;
        }
        if (!CustomVehicles::IsSpawnCommand(command))
            return g_OrigLuaCommands(vehicles, lua, index, command);
        lua_State* L = static_cast<lua_State*>(lua);
        const CustomVehicles::SpawnEdit edit = CustomVehicles::BeginSpawnCommand(L);
        const std::uint64_t result = g_OrigLuaCommands(vehicles, lua, index, command);
        CustomVehicles::EndSpawnCommand(L, edit);
        return result;
    }
}

bool Install_TppVehicle2_LuaCommands_Hook()
{
    static_assert(sizeof(kLuaCommandsEntry) == sizeof(kLuaCommandsEntryMask), "one mask byte per entry byte");
    if (!g_Target)
        g_Target = GameObjectReinitialize::InstallHandlerHook("TppVehicle2_LuaCommands", gAddr.TppVehicle2_LuaCommands,
            kLuaCommandsEntry, kLuaCommandsEntryMask, sizeof(kLuaCommandsEntry),
            reinterpret_cast<void*>(&hk_LuaCommands), reinterpret_cast<void**>(&g_OrigLuaCommands));
    return g_Target != nullptr;
}

void Uninstall_TppVehicle2_LuaCommands_Hook()
{
    GameObjectReinitialize::UninstallHandlerHook(g_Target);
    g_OrigLuaCommands = nullptr;
}
