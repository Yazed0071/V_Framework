#include "pch.h"
#include "DeclarePenetrateLevels.h"

#include "TppEquipConstRegistry.h"

int __cdecl l_DeclarePenetrateLevels(lua_State* L)
{
    return TppEquipDeclareForSpace(L, 23); // PENETRATE_LEVEL_*
}
