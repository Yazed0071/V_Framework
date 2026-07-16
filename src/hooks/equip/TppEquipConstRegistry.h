#pragma once

#include <cstdint>

struct lua_State;

static constexpr int kTppEquipConstSpaceCount = 26;

bool TppEquipConst_Declare(
    int spaceIndex,
    const char* name,
    std::uint32_t& outValue,
    lua_State* L);

int TppEquipDeclareForSpace(lua_State* L, int spaceIndex);

void TppEquipConst_SetValue(const char* name, std::uint32_t value, lua_State* L);
void TppDamageConst_SetValue(const char* name, std::uint32_t value, lua_State* L);

void TppEquipConst_InjectAll(lua_State* L);
void TppDamageConst_InjectAll(lua_State* L);

void TppEquipConst_ResetRuntimeState();
