#include "pch.h"
#include "DeclareScopeUIs.h"

#include "TppEquipConstRegistry.h"

int __cdecl l_DeclareScopeUIs(lua_State* L)
{
    return TppEquipDeclareForSpace(L, 19); // SCOPE_UI_*
}
