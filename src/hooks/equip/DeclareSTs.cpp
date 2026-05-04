#include "pch.h"
#include "DeclareSTs.h"

#include <Windows.h>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

#include "log.h"

namespace
{
    constexpr int LUA_GLOBALSINDEX_51 = -10002;
    constexpr int LUA_TTABLE_51 = 5;
    constexpr int LUA_TSTRING_51 = 4;
    constexpr int LUA_TNUMBER_51 = 3;

    constexpr std::uint32_t kFirstCustomStId = 0x20;

    DeclareSTs::Deps g_Deps{};

    std::mutex g_DeclareSTsMutex;
    std::unordered_map<std::string, std::uint32_t> g_DeclaredSTs;
}

namespace
{
    static bool ValidateDeps()
    {
        return
            g_Deps.ResolveLuaApi &&
            g_Deps.GetLuaTop &&
            g_Deps.LuaGetField &&
            g_Deps.LuaType &&
            g_Deps.LuaPop &&
            g_Deps.GetLuaString &&
            g_Deps.GetLuaInt &&
            g_Deps.PushLuaNumber &&
            g_Deps.LuaPushString &&
            g_Deps.LuaCreateTable &&
            g_Deps.LuaRawSet &&
            g_Deps.LuaSetTable &&
            g_Deps.LuaPushNil &&
            g_Deps.LuaNext;
    }

    static bool EnsureTppEquipTable(lua_State* L)
    {
        g_Deps.LuaGetField(L, LUA_GLOBALSINDEX_51, "TppEquip");
        if (g_Deps.LuaType(L, -1) == LUA_TTABLE_51)
            return true;

        g_Deps.LuaPop(L, 1);

        g_Deps.LuaPushString(L, "TppEquip");
        g_Deps.LuaCreateTable(L, 0, 0);
        g_Deps.LuaRawSet(L, LUA_GLOBALSINDEX_51);

        g_Deps.LuaGetField(L, LUA_GLOBALSINDEX_51, "TppEquip");
        return g_Deps.LuaType(L, -1) == LUA_TTABLE_51;
    }

    static void DeclareStInLua(lua_State* L, const char* name, std::uint32_t stId)
    {
        if (!EnsureTppEquipTable(L))
            return;

        g_Deps.LuaPushString(L, name);
        g_Deps.PushLuaNumber(L, static_cast<float>(stId));
        g_Deps.LuaSetTable(L, -3);

        g_Deps.LuaPop(L, 1);
    }

    static int FindHighestDeclaredStValue(lua_State* L, int tableIndex)
    {
        int highest = -1;

        g_Deps.LuaPushNil(L);
        while (g_Deps.LuaNext(L, tableIndex) != 0)
        {
            if (g_Deps.LuaType(L, -2) == LUA_TSTRING_51 &&
                g_Deps.LuaType(L, -1) == LUA_TNUMBER_51)
            {
                const char* key = g_Deps.GetLuaString(L, -2);
                const int value = g_Deps.GetLuaInt(L, -1);

                if (key &&
                    key[0] == 'S' &&
                    key[1] == 'T' &&
                    key[2] == '_' &&
                    value > highest)
                {
                    highest = value;
                }
            }

            g_Deps.LuaPop(L, 1);
        }

        return highest;
    }

    static std::uint32_t FindNextFreeStId(lua_State* L)
    {
        if (!EnsureTppEquipTable(L))
            return kFirstCustomStId;

        const int tppEquipIndex = g_Deps.GetLuaTop(L);
        const int highestSt = FindHighestDeclaredStValue(L, tppEquipIndex);

        g_Deps.LuaPop(L, 1);

        std::uint32_t nextStId = kFirstCustomStId;
        if (highestSt >= 0)
        {
            const std::uint32_t candidate = static_cast<std::uint32_t>(highestSt + 1);
            if (candidate > nextStId)
                nextStId = candidate;
        }

        return nextStId;
    }

    static bool TryGetExistingStFromLua(lua_State* L, const char* name, std::uint32_t& outStId)
    {
        outStId = 0;

        if (!EnsureTppEquipTable(L))
            return false;

        const int tppEquipIndex = g_Deps.GetLuaTop(L);
        g_Deps.LuaGetField(L, tppEquipIndex, name);

        const bool found = (g_Deps.LuaType(L, -1) == LUA_TNUMBER_51);
        if (found)
            outStId = static_cast<std::uint32_t>(g_Deps.GetLuaInt(L, -1));

        g_Deps.LuaPop(L, 2);
        return found;
    }
}

namespace DeclareSTs
{
    void Bind(const Deps& deps)
    {
        g_Deps = deps;
    }

    int __cdecl Lua_DeclareSTs(lua_State* L)
    {
        if (!L)
        {
            Log("[DeclareSTs] FAILED: lua_State is null\n");
            return 1;
        }

        if (!ValidateDeps() || !g_Deps.ResolveLuaApi || !g_Deps.ResolveLuaApi())
        {
            Log("[DeclareSTs] FAILED: missing deps\n");
            if (g_Deps.PushLuaNumber)
                g_Deps.PushLuaNumber(L, 0.0f);
            return 1;
        }

        const char* name = g_Deps.GetLuaString(L, 1);
        if (!name || !name[0])
        {
            g_Deps.PushLuaNumber(L, 0.0f);
            return 1;
        }

        std::uint32_t stId = 0;

        {
            std::lock_guard<std::mutex> lock(g_DeclareSTsMutex);

            const auto it = g_DeclaredSTs.find(name);
            if (it != g_DeclaredSTs.end())
            {
                stId = it->second;
            }
            else
            {
                if (TryGetExistingStFromLua(L, name, stId))
                {
                    g_DeclaredSTs.emplace(name, stId);
                }
                else
                {
                    stId = FindNextFreeStId(L);
                    g_DeclaredSTs.emplace(name, stId);

                    Log("[DeclareSTs] Added custom ST '%s' => 0x%X (%u)\n",
                        name, stId, stId);
                }
            }
        }

        DeclareStInLua(L, name, stId);
        g_Deps.PushLuaNumber(L, static_cast<float>(stId));
        return 1;
    }
}
