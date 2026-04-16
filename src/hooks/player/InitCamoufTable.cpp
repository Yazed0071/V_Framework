#include "pch.h"
#include <array>
#include <cstdint>
#include <cstring>

#include "log.h"
#include "AddressSet.h"
#include "HookUtils.h"
#include "InitCamoufTable.h"

extern "C" {
    #include "lua.h"
}

namespace
{
    static constexpr std::size_t kMaxCamoTypes = 117;
    static constexpr std::size_t kMaxMaterialTypes = 82;

    using CamoRow = std::array<std::int32_t, kMaxMaterialTypes>;
    using CamoTable = std::array<CamoRow, kMaxCamoTypes>;

    static CamoTable g_CustomCamoTable{};
    static bool g_HaveCustomCamoTable = false;

    static bool IsValidCamoType(std::uint32_t camoType)
    {
        return camoType < kMaxCamoTypes;
    }

    static bool IsValidMaterialType(std::uint32_t materialType)
    {
        return materialType < kMaxMaterialTypes;
    }
}

std::int32_t Get_CamoValue(std::uint32_t camoType, std::uint32_t materialType)
{
    if (!IsValidCamoType(camoType) || !IsValidMaterialType(materialType))
        return 0;

    return g_CustomCamoTable[camoType][materialType];
}

bool Set_CamoValue(std::uint32_t camoType, std::uint32_t materialType, std::int32_t value)
{
    if (!IsValidCamoType(camoType) || !IsValidMaterialType(materialType))
        return false;

    g_CustomCamoTable[camoType][materialType] = value;
    g_HaveCustomCamoTable = true;

    Log("[CamoTable] SetCamoValue: camo=%u material=%u value=%d\n",
        camoType, materialType, value);
    return true;
}

bool Clone_CamoRow(std::uint32_t dstCamoType, std::uint32_t srcCamoType)
{
    if (!IsValidCamoType(dstCamoType) || !IsValidCamoType(srcCamoType))
        return false;

    g_CustomCamoTable[dstCamoType] = g_CustomCamoTable[srcCamoType];
    g_HaveCustomCamoTable = true;

    Log("[CamoTable] CloneCamoRow: dst=%u src=%u\n", dstCamoType, srcCamoType);
    return true;
}

bool ImportCamoRow(std::uint32_t camoType, const std::int32_t* values, std::size_t count)
{
    if (!IsValidCamoType(camoType) || !values)
        return false;

    const std::size_t toCopy = (count < kMaxMaterialTypes) ? count : kMaxMaterialTypes;

    for (std::size_t i = 0; i < toCopy; ++i)
        g_CustomCamoTable[camoType][i] = values[i];

    for (std::size_t i = toCopy; i < kMaxMaterialTypes; ++i)
        g_CustomCamoTable[camoType][i] = 0;

    g_HaveCustomCamoTable = true;
    return true;
}

std::size_t GetCamoTypeCount()
{
    return kMaxCamoTypes;
}

std::size_t GetMaterialTypeCount()
{
    return kMaxMaterialTypes;
}

bool HasCustomCamoTable()
{
    return g_HaveCustomCamoTable;
}

bool PushCamoTableToGame(lua_State* L)
{
    if (!L || !g_HaveCustomCamoTable)
        return false;

    // Resolve Lua API functions from the game
    using lua_createtable_t = void(__fastcall*)(lua_State*, int, int);
    using lua_pushnumber_t = void(__fastcall*)(lua_State*, double);
    using lua_rawset_t = void(__fastcall*)(lua_State*, int);
    using lua_gettop_t = int(__fastcall*)(lua_State*);
    using lua_settop_t = void(__fastcall*)(lua_State*, int);

    auto createtable = reinterpret_cast<lua_createtable_t>(ResolveGameAddress(gAddr.lua_createtable));
    auto pushnumber = reinterpret_cast<lua_pushnumber_t>(ResolveGameAddress(gAddr.lua_pushnumber));
    auto rawset = reinterpret_cast<lua_rawset_t>(ResolveGameAddress(gAddr.lua_rawset));
    auto gettop = reinterpret_cast<lua_gettop_t>(ResolveGameAddress(gAddr.lua_gettop));
    auto settop = reinterpret_cast<lua_settop_t>(ResolveGameAddress(gAddr.lua_settop));

    if (!createtable || !pushnumber || !rawset || !gettop || !settop)
    {
        Log("[CamoTable] PushToGame: failed to resolve Lua API\n");
        return false;
    }

    // Resolve the camo system object: DAT_142c1be48 is a pointer to the object
    if (gAddr.CamoSystemObject == 0)
    {
        Log("[CamoTable] PushToGame: CamoSystemObject address not set\n");
        return false;
    }

    void** camoSystemPtr = reinterpret_cast<void**>(ResolveGameAddress(gAddr.CamoSystemObject));
    if (!camoSystemPtr || !*camoSystemPtr)
    {
        Log("[CamoTable] PushToGame: CamoSystemObject is null\n");
        return false;
    }

    void* camoSystem = *camoSystemPtr;
    void** vtable = *reinterpret_cast<void***>(camoSystem);
    if (!vtable || !vtable[1])
    {
        Log("[CamoTable] PushToGame: vtable[1] is null\n");
        return false;
    }

    // vtable[1] is the function that reads the Lua camo table: (object, lua_State*)
    using InitCamoufVtable_t = void(__fastcall*)(void* self, lua_State* L);
    auto initCamouf = reinterpret_cast<InitCamoufVtable_t>(vtable[1]);

    const int topBefore = gettop(L);

    // Build the 117x82 Lua table on the stack
    createtable(L, static_cast<int>(kMaxCamoTypes), 0);
    const int outerIdx = gettop(L);

    for (std::size_t row = 0; row < kMaxCamoTypes; ++row)
    {
        pushnumber(L, static_cast<double>(row + 1));
        createtable(L, static_cast<int>(kMaxMaterialTypes), 0);
        const int innerIdx = gettop(L);

        for (std::size_t col = 0; col < kMaxMaterialTypes; ++col)
        {
            pushnumber(L, static_cast<double>(col + 1));
            pushnumber(L, static_cast<double>(g_CustomCamoTable[row][col]));
            rawset(L, innerIdx);
        }

        rawset(L, outerIdx);
    }

    // Call the engine's camo init: vtable[1](camoSystem, L)
    // The table is at stack top — the function reads it from there
    __try
    {
        initCamouf(camoSystem, L);
        Log("[CamoTable] PushToGame: applied %zu rows to engine\n", kMaxCamoTypes);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        Log("[CamoTable] PushToGame: exception during engine call\n");
        settop(L, topBefore);
        return false;
    }

    // Clean up the Lua stack
    settop(L, topBefore);
    return true;
}
