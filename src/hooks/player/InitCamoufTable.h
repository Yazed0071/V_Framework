#pragma once

#include <cstddef>
#include <cstdint>

struct lua_State;

namespace CamoufTable
{
    // Fixed table dimensions (game enums).
    static constexpr std::size_t kMaxCamoTypes     = 117;  // 0..116  OLIVEDRAB..QUIET
    static constexpr std::size_t kMaxMaterialTypes = 82;   // 0..81   MTR_IRON_A..MTR_WATE_C

    // Lua callbacks (resolved at runtime through SetLuaFunctions Bind).
    struct Deps
    {
        void (*LuaCreateTable)(lua_State* L, int narr, int nrec) = nullptr;
        void (*LuaPushString) (lua_State* L, const char* value)  = nullptr;
        void (*LuaPushNumber) (lua_State* L, float value)        = nullptr;
        void (*LuaSetTable)   (lua_State* L, int idx)            = nullptr;
        int  (*LuaGetTop)     (lua_State* L)                     = nullptr;
        void (*LuaSetTop)     (lua_State* L, int idx)            = nullptr;
    };

    void Bind(const Deps& deps);

    // Mutators: after each call, if the camo system object is resolved, the
    // full 117x82 table is pushed to the engine.
    bool Set_CamoValue   (std::int32_t camoType, std::int32_t materialType, std::int32_t value);
    bool Clone_CamoRow   (std::int32_t dstCamoType, std::int32_t srcCamoType);
    bool ImportCamoRow   (std::int32_t camoType, const std::int32_t* values, std::size_t count);

    // Bulk import: replaces the entire 117x82 table from a row-major flat
    // array. The caller is expected to flatten any 2D source. The table is
    // pushed to the engine ONCE at the end (via the SetLuaFunctions Lua
    // bridge call site) — much cheaper than 117 individual ImportCamoRow
    // calls each triggering its own push.
    //
    // `rowCount` / `colCount` cap the read; rows past kMaxCamoTypes or
    // columns past kMaxMaterialTypes are ignored. Missing cells fill with 0.
    bool ImportCamoTable (const std::int32_t* values,
                          std::size_t rowCount,
                          std::size_t colCount);

    std::int32_t Get_CamoValue(std::int32_t camoType, std::int32_t materialType);

    // Push current table to the engine (via CamoSystemObject vtable[1]). Safe
    // to call when no mutation happened; does nothing if never modified.
    bool PushCamoTableToGame(lua_State* L);

    bool HasCustomTable();
    void Reset();
}
