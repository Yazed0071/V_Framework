#include "pch.h"
#include "DeclareSTs.h"

#include "TppEquipConstRegistry.h"

int __cdecl l_DeclareSTs(lua_State* L)
{
    return TppEquipDeclareForSpace(L, 14); // ST_*
}
