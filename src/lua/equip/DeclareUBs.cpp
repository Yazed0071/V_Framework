#include "pch.h"
#include "DeclareUBs.h"

#include "TppEquipConstRegistry.h"

int __cdecl l_DeclareUBs(lua_State* L)
{
    return TppEquipDeclareForSpace(L, 12); // UB_*
}
