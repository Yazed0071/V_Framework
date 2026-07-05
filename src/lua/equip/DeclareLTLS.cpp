#include "pch.h"
#include "DeclareLTLS.h"

#include "TppEquipConstRegistry.h"

int __cdecl l_DeclareLTLS(lua_State* L)
{
    return TppEquipDeclareForSpace(L, 9); // LT_* / LS_*
}
