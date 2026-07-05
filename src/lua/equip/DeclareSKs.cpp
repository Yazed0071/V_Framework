#include "pch.h"
#include "DeclareSKs.h"

#include "TppEquipConstRegistry.h"

int __cdecl l_DeclareSKs(lua_State* L)
{
    return TppEquipDeclareForSpace(L, 17); // SK_*
}
