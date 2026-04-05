#pragma once

#include <cstdint>

struct lua_State;

// Installs the RegisterConstantEquipId hook.
// Params: none
bool Install_RegisterConstantEquipId_Hook();

// Uninstalls the RegisterConstantEquipId hook.
// Params: none
bool Uninstall_RegisterConstantEquipId_Hook();

// Registers one custom EquipId from Lua and applies it immediately to TppEquip + the hash table.
// Params: L, equipName, outEquipId
bool Register_CustomEquipId_FromLua(lua_State* L, const char* equipName, std::uint32_t& outEquipId);