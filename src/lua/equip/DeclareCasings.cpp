#include "pch.h"
#include "DeclareCasings.h"

#include "TppEquipConstRegistry.h"

int __cdecl l_DeclareCasings(lua_State* L)
{
    return TppEquipDeclareForSpace(L, 7); // CASING_*
}
