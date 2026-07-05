#include "pch.h"
#include "DeclareRCs.h"

#include "TppEquipConstRegistry.h"

int __cdecl l_DeclareRCs(lua_State* L)
{
    return TppEquipDeclareForSpace(L, 15); // RC_*
}
