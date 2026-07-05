#include "pch.h"
#include "DeclareMOs.h"

#include "TppEquipConstRegistry.h"

int __cdecl l_DeclareMOs(lua_State* L)
{
    return TppEquipDeclareForSpace(L, 11); // MO_*
}
