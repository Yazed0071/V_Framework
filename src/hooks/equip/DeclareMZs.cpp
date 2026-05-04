#include "pch.h"
#include "DeclareMZs.h"

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

    constexpr std::uint32_t kFirstCustomMzId = 0x20;

    DeclareMZs::Deps g_Deps{};

    std::mutex g_DeclareMZsMutex;
    std::unordered_map<std::string, std::uint32_t> g_DeclaredMZs;
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

    static void DeclareMzInLua(lua_State* L, const char* name, std::uint32_t mzId)
    {
        if (!EnsureTppEquipTable(L))
            return;

        g_Deps.LuaPushString(L, name);
        g_Deps.PushLuaNumber(L, static_cast<float>(mzId));
        g_Deps.LuaSetTable(L, -3);

        g_Deps.LuaPop(L, 1);
    }

    static int FindHighestDeclaredMzValue(lua_State* L, int tableIndex)
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
                    key[0] == 'M' &&
                    key[1] == 'Z' &&
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

    static std::uint32_t FindNextFreeMzId(lua_State* L)
    {
        if (!EnsureTppEquipTable(L))
            return kFirstCustomMzId;

        const int tppEquipIndex = g_Deps.GetLuaTop(L);
        const int highestMz = FindHighestDeclaredMzValue(L, tppEquipIndex);

        g_Deps.LuaPop(L, 1);

        std::uint32_t nextMzId = kFirstCustomMzId;
        if (highestMz >= 0)
        {
            const std::uint32_t candidate = static_cast<std::uint32_t>(highestMz + 1);
            if (candidate > nextMzId)
                nextMzId = candidate;
        }

        return nextMzId;
    }

    static bool TryGetExistingMzFromLua(lua_State* L, const char* name, std::uint32_t& outMzId)
    {
        outMzId = 0;

        if (!EnsureTppEquipTable(L))
            return false;

        const int tppEquipIndex = g_Deps.GetLuaTop(L);
        g_Deps.LuaGetField(L, tppEquipIndex, name);

        const bool found = (g_Deps.LuaType(L, -1) == LUA_TNUMBER_51);
        if (found)
            outMzId = static_cast<std::uint32_t>(g_Deps.GetLuaInt(L, -1));

        g_Deps.LuaPop(L, 2);
        return found;
    }
}

namespace DeclareMZs
{
    void Bind(const Deps& deps)
    {
        g_Deps = deps;
    }

    int __cdecl Lua_DeclareMZs(lua_State* L)
    {
        if (!L)
        {
            Log("[DeclareMZs] FAILED: lua_State is null\n");
            return 1;
        }

        if (!ValidateDeps() || !g_Deps.ResolveLuaApi || !g_Deps.ResolveLuaApi())
        {
            Log("[DeclareMZs] FAILED: missing deps\n");
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

        std::uint32_t mzId = 0;

        {
            std::lock_guard<std::mutex> lock(g_DeclareMZsMutex);

            const auto it = g_DeclaredMZs.find(name);
            if (it != g_DeclaredMZs.end())
            {
                mzId = it->second;
            }
            else
            {
                if (TryGetExistingMzFromLua(L, name, mzId))
                {
                    g_DeclaredMZs.emplace(name, mzId);
                }
                else
                {
                    mzId = FindNextFreeMzId(L);
                    g_DeclaredMZs.emplace(name, mzId);

                    Log("[DeclareMZs] Added custom MZ '%s' => 0x%X (%u)\n",
                        name, mzId, mzId);
                }
            }
        }

        DeclareMzInLua(L, name, mzId);
        g_Deps.PushLuaNumber(L, static_cast<float>(mzId));
        return 1;
    }
}
