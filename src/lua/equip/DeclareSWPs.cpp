#include "pch.h"
#include "DeclareSWPs.h"

#include "TppEquipConstRegistry.h"

int __cdecl l_DeclareSWPs(lua_State* L)
{
    return TppEquipDeclareForSpace(L, 4); // SWP_*
}
