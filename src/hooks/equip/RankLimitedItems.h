#pragma once

#include <cstdint>
#include <string>

struct lua_State;

namespace RankLimitedItems
{
    void Register(std::int32_t equipId, const std::string& resultLangId);
    void Unregister(std::int32_t equipId);
    void Pump();
    void ClearForNewMission();
    void EnsureResultHookInstalled(lua_State* L);

    int __cdecl Lua_GetUsedRankLimitedItemLangIds(lua_State* L);
}
