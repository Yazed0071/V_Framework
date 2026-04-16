#pragma once
#include <cstdint>

bool Clone_CamoRow(std::uint32_t dstCamoType, std::uint32_t srcCamoType);
bool Set_CamoValue(std::uint32_t camoType, std::uint32_t materialType, std::int32_t value);
std::int32_t Get_CamoValue(std::uint32_t camoType, std::uint32_t materialType);
bool ImportCamoRow(std::uint32_t camoType, const std::int32_t* values, std::size_t count);
std::size_t GetCamoTypeCount();
std::size_t GetMaterialTypeCount();
bool HasCustomCamoTable();

struct lua_State;

// Pushes the shadow camo table to the game engine via the camo system vtable.
// Call after SetCamoValue/CloneCamoRow/ImportCamoRow to apply changes.
bool PushCamoTableToGame(lua_State* L);
