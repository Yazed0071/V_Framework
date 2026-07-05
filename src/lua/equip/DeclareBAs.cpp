#include "pch.h"
#include "DeclareBAs.h"

#include "TppEquipConstRegistry.h"

int __cdecl l_DeclareBAs(lua_State* L)
{
    return TppEquipDeclareForSpace(L, 16); // BA_*
}
