#include "pch.h"
#include "DeclareReticleUIs.h"

#include "TppEquipConstRegistry.h"

int __cdecl l_DeclareReticleUIs(lua_State* L)
{
    return TppEquipDeclareForSpace(L, 18); // RETICLE_UI_*
}
