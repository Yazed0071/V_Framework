
#include "pch.h"
#include "DeclareSWPs.h"

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

    DeclareSWPs::Deps g_Deps{};

    std::mutex g_DeclareSWPsMutex;
    std::unordered_map<std::string, std::uint32_t> g_DeclaredSWPs;


    std::uint32_t g_NextCustomSwpId = 0x59;
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
            g_Deps.LuaSetTable;
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

    static void DeclareSwpInLua(lua_State* L, const char* name, std::uint32_t swpId)
    {
        if (!EnsureTppEquipTable(L))
            return;


        g_Deps.LuaPushString(L, name);
        g_Deps.PushLuaNumber(L, static_cast<float>(swpId));
        g_Deps.LuaSetTable(L, -3);


        g_Deps.LuaPop(L, 1);
    }
}

namespace DeclareSWPs
{
    void Bind(const Deps& deps)
    {
        g_Deps = deps;
    }

    int __cdecl Lua_DeclareSWPs(lua_State* L)
    {
        if (!L || !ValidateDeps() || !g_Deps.ResolveLuaApi || !g_Deps.ResolveLuaApi())
        {
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

        std::uint32_t swpId = 0;

        {
            std::lock_guard<std::mutex> lock(g_DeclareSWPsMutex);

            const auto it = g_DeclaredSWPs.find(name);
            if (it != g_DeclaredSWPs.end())
            {
                swpId = it->second;
            }
            else
            {
                swpId = g_NextCustomSwpId++;
                g_DeclaredSWPs.emplace(name, swpId);

                Log("[DeclareSWPs] Added custom SWP '%s' => 0x%X (%u)\n",
                    name, swpId, swpId);
            }
        }

        DeclareSwpInLua(L, name, swpId);
        g_Deps.PushLuaNumber(L, static_cast<float>(swpId));
        return 1;
    }
}