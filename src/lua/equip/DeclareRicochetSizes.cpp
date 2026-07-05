#include "pch.h"
#include "DeclareRicochetSizes.h"

#include "TppEquipConstRegistry.h"

int __cdecl l_DeclareRicochetSizes(lua_State* L)
{
    return TppEquipDeclareForSpace(L, 21); // RICOCHET_SIZE_*
}
