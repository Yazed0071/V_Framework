#pragma once

struct lua_State;

namespace battleGearCamera
{
    // on the Lua owner thread after a game lua_pcall: a far third-person camera while the player rides the Battle Gear
    void Update(lua_State* L);
}
