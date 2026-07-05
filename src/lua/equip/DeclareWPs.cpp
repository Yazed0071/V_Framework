#include "pch.h"
#include "DeclareWPs.h"

#include "TppEquipConstRegistry.h"

int __cdecl l_DeclareWPs(lua_State* L)
{
    return TppEquipDeclareForSpace(L, 10); // WP_*
}
