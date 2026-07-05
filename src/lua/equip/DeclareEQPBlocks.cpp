#include "pch.h"
#include "DeclareEQPBlocks.h"

#include "TppEquipConstRegistry.h"

int __cdecl l_DeclareEQPBlocks(lua_State* L)
{
    return TppEquipDeclareForSpace(L, 3); // EQP_BLOCK_*
}
