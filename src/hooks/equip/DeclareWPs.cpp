#include "pch.h"
#include "DeclareWPs.h"

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

    // Native DeclareWPs uses its own WP_* range.
    // Custom WP ids should begin after the stock WP table, not after EquipIds.
    constexpr std::uint32_t kFirstCustomWpId = 0x203;

    DeclareWPs::Deps g_Deps{};

    std::mutex g_DeclareWPsMutex;
    std::unordered_map<std::string, std::uint32_t> g_DeclaredWPs;
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
            g_Deps.PushLuaNumber &&
            g_Deps.LuaPushString &&
            g_Deps.LuaCreateTable &&
            g_Deps.LuaRawSet &&
            g_Deps.LuaSetTable &&
            g_Deps.LuaPushNil &&
            g_Deps.LuaNext &&
            g_Deps.GetLuaInt;
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

    static void DeclareWpInLua(lua_State* L, const char* name, std::uint32_t wpId)
    {
        if (!EnsureTppEquipTable(L))
            return;

        // stack top is TppEquip
        g_Deps.LuaPushString(L, name);
        g_Deps.PushLuaNumber(L, static_cast<float>(wpId));
        g_Deps.LuaSetTable(L, -3);

        // pop TppEquip
        g_Deps.LuaPop(L, 1);
    }

    static int FindHighestDeclaredWpValue(lua_State* L, int tableIndex)
    {
        int highest = -1;

        g_Deps.LuaPushNil(L);
        while (g_Deps.LuaNext(L, tableIndex) != 0)
        {
            // key at -2, value at -1
            if (g_Deps.LuaType(L, -2) == LUA_TSTRING_51 &&
                g_Deps.LuaType(L, -1) == LUA_TNUMBER_51)
            {
                const char* key = g_Deps.GetLuaString(L, -2);
                const int value = g_Deps.GetLuaInt(L, -1);

                if (key &&
                    key[0] == 'W' &&
                    key[1] == 'P' &&
                    key[2] == '_' &&
                    value > highest)
                {
                    highest = value;
                }
            }

            // pop value, keep key
            g_Deps.LuaPop(L, 1);
        }

        return highest;
    }

    static std::uint32_t FindNextFreeWpId(lua_State* L)
    {
        if (!EnsureTppEquipTable(L))
            return kFirstCustomWpId;

        const int tppEquipIndex = g_Deps.GetLuaTop(L);
        int highestWp = FindHighestDeclaredWpValue(L, tppEquipIndex);

        // pop TppEquip
        g_Deps.LuaPop(L, 1);

        std::uint32_t nextWpId = kFirstCustomWpId;
        if (highestWp >= 0)
        {
            const std::uint32_t candidate = static_cast<std::uint32_t>(highestWp + 1);
            if (candidate > nextWpId)
                nextWpId = candidate;
        }

        return nextWpId;
    }

    static bool TryGetExistingWpFromLua(lua_State* L, const char* name, std::uint32_t& outWpId)
    {
        outWpId = 0;

        if (!EnsureTppEquipTable(L))
            return false;

        const int tppEquipIndex = g_Deps.GetLuaTop(L);
        g_Deps.LuaGetField(L, tppEquipIndex, name);

        const bool found = (g_Deps.LuaType(L, -1) == LUA_TNUMBER_51);
        if (found)
            outWpId = static_cast<std::uint32_t>(g_Deps.GetLuaInt(L, -1));

        // pop field value + TppEquip
        g_Deps.LuaPop(L, 2);
        return found;
    }
}

namespace DeclareWPs
{
    void Bind(const Deps& deps)
    {
        g_Deps = deps;
    }

    int __cdecl Lua_DeclareWPs(lua_State* L)
    {
        if (!L)
        {
            Log("[DeclareWPs] FAILED: lua_State is null\n");
            return 1;
        }

        if (!ValidateDeps() || !g_Deps.ResolveLuaApi || !g_Deps.ResolveLuaApi())
        {
            Log("[DeclareWPs] FAILED: missing deps\n");
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

        std::uint32_t wpId = 0;

        {
            std::lock_guard<std::mutex> lock(g_DeclareWPsMutex);

            const auto it = g_DeclaredWPs.find(name);
            if (it != g_DeclaredWPs.end())
            {
                wpId = it->second;
            }
            else
            {
                // If Lua already has this WP_* constant, reuse it.
                if (TryGetExistingWpFromLua(L, name, wpId))
                {
                    g_DeclaredWPs.emplace(name, wpId);
                }
                else
                {
                    wpId = FindNextFreeWpId(L);
                    g_DeclaredWPs.emplace(name, wpId);

                    Log("[DeclareWPs] Added custom WP '%s' => 0x%X (%u)\n",
                        name, wpId, wpId);
                }
            }
        }

        DeclareWpInLua(L, name, wpId);
        g_Deps.PushLuaNumber(L, static_cast<float>(wpId));
        return 1;
    }
}