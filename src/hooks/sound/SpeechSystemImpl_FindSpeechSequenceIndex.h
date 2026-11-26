#pragma once

struct lua_State;

bool Install_SoldierConversations_Hook();
bool Uninstall_SoldierConversations_Hook();

namespace SoldierConversations
{
    void AddRestoredConversations(lua_State* L, int commandIndex);
}
