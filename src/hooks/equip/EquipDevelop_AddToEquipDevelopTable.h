#pragma once

#include <cstddef>
#include <cstdint>

struct lua_State;

namespace EquipDevelopAdd
{
    struct Deps
    {
        bool (*ResolveLuaApi)() = nullptr;

        int (*GetLuaTop)(lua_State* L) = nullptr;
        int (*LuaType)(lua_State* L, int idx) = nullptr;
        void (*LuaSetTop)(lua_State* L, int idx) = nullptr;

        const char* (*GetLuaString)(lua_State* L, int idx) = nullptr;
        int (*GetLuaInt)(lua_State* L, int idx) = nullptr;
        void (*PushLuaNumber)(lua_State* L, float value) = nullptr;

        void (*LuaPushString)(lua_State* L, const char* value) = nullptr;
        void (*LuaCreateTable)(lua_State* L, int narr, int nrec) = nullptr;
        void (*LuaGetTable)(lua_State* L, int idx) = nullptr;
        void (*LuaSetTable)(lua_State* L, int idx) = nullptr;
    };

    void Bind(const Deps& deps);

    int __cdecl Lua_AddToEquipDevelopTable(lua_State* L);

    bool TryGetFlowIndexForDevelopId(std::uint16_t developId, std::uint16_t& outFlowIndex);

    // Stock-id probes. Return true when the given developId / flowIndex
    // was registered by the vanilla MGSV equip-develop tables during
    // boot (observed via the RegCstDev / RegFlwDev hooks). Used by
    // V_FrameWorkState's auto-allocator to skip any slot vanilla already
    // owns, so mod outfit ids can pack into the lowest free non-vanilla
    // slots without colliding with stock rows.
    //
    // Both return false until the orig boot has had a chance to register
    // its tables (i.e. before the equip-develop hook fires on stock data),
    // which is the conservative default: an id we haven't observed yet
    // is treated as "not vanilla", so allocation can proceed. The
    // EquipDevelopAdd module flushes pending registrations only after
    // both observation flags trip, so by the time mod allocations run
    // these sets are populated.
    bool IsDevelopIdReservedByStock(std::uint32_t developId);
    bool IsFlowIndexReservedByStock(std::uint32_t flowIndex);

    bool Install_TppMotherBaseManagement_EquipDevelopHooks();
    bool Uninstall_TppMotherBaseManagement_EquipDevelopHooks();
}