#include <pch.h>
#include "WalkerGear2Impl_ProcessLuaCommand.h"

#include <cstdint>

#include "AddressSet.h"
#include "../fox/gm/GameObjectReinitialize.h"

namespace
{
    using ProcessLuaCommand_t = std::uint64_t(__fastcall*)(void* handler, void* lua, std::uint32_t index, std::uint64_t command);   // compares the full 48-bit command hash

    constexpr std::uint8_t kProcessLuaCommandEntry[] = {
        0x4C, 0x89, 0x4C, 0x24, 0x20, 0x53, 0x55, 0x56, 0x57, 0x48, 0x81, 0xEC, 0x98, 0x00, 0x00, 0x00,
        0x48, 0x8B, 0x05, 0x89, 0x31, 0x44, 0x01, 0x48, 0x33, 0xC4, 0x48, 0x89, 0x44, 0x24, 0x40,
    };
    constexpr std::uint8_t kProcessLuaCommandEntryMask[] = {
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1,
    };

    ProcessLuaCommand_t g_OrigProcessLuaCommand = nullptr;
    void*               g_Target                = nullptr;

    std::uint64_t __fastcall hk_ProcessLuaCommand(void* handler, void* lua, std::uint32_t index, std::uint64_t command)
    {
        if (!GameObjectReinitialize::Claims(static_cast<std::uint32_t>(command)))
            return g_OrigProcessLuaCommand(handler, lua, index, command);
        GameObjectReinitialize::Report(GameObjectReinitialize::WalkerGears(static_cast<std::uint8_t*>(handler), index));
        return 0;
    }
}

bool Install_WalkerGear2Impl_ProcessLuaCommand_Hook()
{
    static_assert(sizeof(kProcessLuaCommandEntry) == sizeof(kProcessLuaCommandEntryMask), "one mask byte per entry byte");
    if (!g_Target)
        g_Target = GameObjectReinitialize::InstallHandlerHook("WalkerGear2Impl::ProcessLuaCommand", gAddr.WalkerGear2Impl_ProcessLuaCommand,
            kProcessLuaCommandEntry, kProcessLuaCommandEntryMask, sizeof(kProcessLuaCommandEntry),
            reinterpret_cast<void*>(&hk_ProcessLuaCommand), reinterpret_cast<void**>(&g_OrigProcessLuaCommand));
    return g_Target != nullptr;
}

void Uninstall_WalkerGear2Impl_ProcessLuaCommand_Hook()
{
    GameObjectReinitialize::UninstallHandlerHook(g_Target);
    g_OrigProcessLuaCommand = nullptr;
}
