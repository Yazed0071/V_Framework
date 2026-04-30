#pragma once

#include <cstddef>
#include <cstdint>

struct lua_State;

namespace CamoufTable
{

    static constexpr std::size_t kMaxCamoTypes     = 117;
    static constexpr std::size_t kMaxMaterialTypes = 82;


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


    bool Set_CamoValue   (std::int32_t camoType, std::int32_t materialType, std::int32_t value);
    bool Clone_CamoRow   (std::int32_t dstCamoType, std::int32_t srcCamoType);
    bool ImportCamoRow   (std::int32_t camoType, const std::int32_t* values, std::size_t count);


    bool ImportCamoTable (const std::int32_t* values,
                          std::size_t rowCount,
                          std::size_t colCount);

    std::int32_t Get_CamoValue(std::int32_t camoType, std::int32_t materialType);


    bool PushCamoTableToGame(lua_State* L);

    bool HasCustomTable();
    void Reset();
}
