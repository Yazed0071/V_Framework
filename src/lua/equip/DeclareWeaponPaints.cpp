#include "pch.h"
#include "DeclareWeaponPaints.h"

#include "TppEquipConstRegistry.h"

int __cdecl l_DeclareWeaponPaints(lua_State* L)
{
    return TppEquipDeclareForSpace(L, 25); // WEAPON_PAINT_*
}
