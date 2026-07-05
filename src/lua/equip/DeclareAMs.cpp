#include "pch.h"
#include "DeclareAMs.h"

#include "TppEquipConstRegistry.h"

int __cdecl l_DeclareAMs(lua_State* L)
{
    return TppEquipDeclareForSpace(L, 13); // AM_*
}
