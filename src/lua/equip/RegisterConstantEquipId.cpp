#include "pch.h"
#include "RegisterConstantEquipId.h"

#include "TppEquipConstRegistry.h"

int __cdecl l_RegisterConstantEquipId(lua_State* L)
{
    return TppEquipDeclareForSpace(L, 0); // EQP_*
}
