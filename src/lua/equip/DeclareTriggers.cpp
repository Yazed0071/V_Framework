#include "pch.h"
#include "DeclareTriggers.h"

#include "TppEquipConstRegistry.h"

int __cdecl l_DeclareTriggers(lua_State* L)
{
    return TppEquipDeclareForSpace(L, 24); // TRIGGER_*
}
