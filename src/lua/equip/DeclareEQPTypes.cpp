#include "pch.h"
#include "DeclareEQPTypes.h"

#include "TppEquipConstRegistry.h"

int __cdecl l_DeclareEQPTypes(lua_State* L)
{
    return TppEquipDeclareForSpace(L, 1); // EQP_TYPE_*
}
