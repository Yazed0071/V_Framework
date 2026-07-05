#include "pch.h"
#include "DeclareMZs.h"

#include "TppEquipConstRegistry.h"

int __cdecl l_DeclareMZs(lua_State* L)
{
    return TppEquipDeclareForSpace(L, 8); // MZ_*
}
