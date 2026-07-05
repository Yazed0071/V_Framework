#include "pch.h"
#include "DeclareBulletTypes.h"

#include "TppEquipConstRegistry.h"

int __cdecl l_DeclareBulletTypes(lua_State* L)
{
    return TppEquipDeclareForSpace(L, 22); // BULLET_TYPE_*
}
