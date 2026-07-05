#include "pch.h"
#include "DeclareBLAs.h"

#include "TppEquipConstRegistry.h"

int __cdecl l_DeclareBLAs(lua_State* L)
{
    return TppEquipDeclareForSpace(L, 6); // BLA_*
}
