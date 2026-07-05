#include "pch.h"
#include "DeclareBarrelLengths.h"

#include "TppEquipConstRegistry.h"

int __cdecl l_DeclareBarrelLengths(lua_State* L)
{
    return TppEquipDeclareForSpace(L, 20); // BARREL_LENGTH_*
}
