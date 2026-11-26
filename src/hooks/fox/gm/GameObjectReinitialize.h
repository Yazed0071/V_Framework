#pragma once

#include <cstddef>
#include <cstdint>

namespace GameObjectReinitialize
{
    enum class Result : std::uint8_t
    {
        NotReached,
        Done,
        PastInstanceCount,
        NotSpawned,
        NotAlive,
        Fault,
    };

    using LuaCommand_t = std::uint64_t(__fastcall*)(void* handler, void* lua, std::uint32_t index, std::uint32_t command);

    void   Arm();
    Result Take();
    bool   Claims(std::uint32_t command);   // true once per Arm, when a type's own Lua command handler gets our Reinitialize
    void   Report(Result result);

    Result SecurityCameras(std::uint8_t* handler, std::uint32_t index);   // index 0x1FF = every instance of the type
    Result Uavs(std::uint8_t* handler, std::uint32_t index);
    Result Vehicles(std::uint8_t* vehicles, void* lua, std::uint32_t index, LuaCommand_t original);
    Result Helis(std::uint8_t* handler, void* lua, std::uint32_t index, LuaCommand_t original);
    Result Soldiers(std::uint8_t* handler, void* lua, std::uint32_t index, LuaCommand_t original);
    Result Hostages(std::uint8_t* handler, void* lua, std::uint32_t index, LuaCommand_t original);
    Result WalkerGears(std::uint8_t* handler, std::uint32_t index);
    Result Sahelans(std::uint8_t* handler, std::uint32_t index);

    bool ClearMarker(std::uint8_t* markers, std::uint32_t index);

    void* InstallHandlerHook(const char* handlerName, std::uintptr_t address, const std::uint8_t* entry, const std::uint8_t* mask,
                             std::size_t entrySize, void* detour, void** original);
    void  UninstallHandlerHook(void*& target);
}
