#include <pch.h>
#include "V_VehicleLib.h"

#include "LuaApi.h"
#include "../hooks/vehicle/CustomVehicles.h"

namespace
{
    luaL_Reg g_VehicleLib[] =
    {
        { "RegisterVehicle", CustomVehicles::Lua_RegisterVehicle },
        { "SetSpawnChance",  CustomVehicles::Lua_SetSpawnChance },
        { "SetReliefPack",   CustomVehicles::Lua_SetReliefPack },
        { "IsLoaded",        CustomVehicles::Lua_IsLoaded },
        { nullptr, nullptr }
    };
}

bool Register_V_VehicleLibrary(lua_State* L)
{
    return RegisterLuaLibrary(L, "V_Vehicle", g_VehicleLib);
}
