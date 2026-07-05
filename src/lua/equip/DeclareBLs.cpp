#include "pch.h"
#include "DeclareBLs.h"

#include "TppEquipConstRegistry.h"

int __cdecl l_DeclareBLs(lua_State* L)
{
    return TppEquipDeclareForSpace(L, 5); // BL_*
}
