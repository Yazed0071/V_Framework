#include "pch.h"
#include "DeclareSWPTypes.h"

#include "TppEquipConstRegistry.h"

int __cdecl l_DeclareSWPTypes(lua_State* L)
{
    return TppEquipDeclareForSpace(L, 2); // SWP_TYPE_*
}
