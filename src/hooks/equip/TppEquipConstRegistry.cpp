#include "pch.h"
#include "TppEquipConstRegistry.h"

#include <map>
#include <mutex>
#include <string>

#include "log.h"
#include "V_FrameWorkState.h"
#include "LuaApi.h"

namespace
{
    static const char* const kSpaceTags[kTppEquipConstSpaceCount] =
    {
        "EQP",
        "EQP_TYPE",
        "SWP_TYPE",
        "EQP_BLOCK",
        "SWP",
        "BL",
        "BLA",
        "CASING",
        "MZ",
        "LTLS",
        "WP",
        "MO",
        "UB",
        "AM",
        "ST",
        "RC",
        "BA",
        "SK",
        "RETICLE_UI",
        "SCOPE_UI",
        "BARREL_LENGTH",
        "RICOCHET_SIZE",
        "BULLET_TYPE",
        "PENETRATE_LEVEL",
        "TRIGGER",
        "WEAPON_PAINT",
    };

    static constexpr std::int32_t kSpaceEqp            = 0;
    static constexpr std::int32_t kDefaultConstantBase = 0x4000;
    static bool IsValidConstantName(const char* name)
    {
        if (!name || !name[0])
            return false;
        size_t len = 0;
        for (const char* p = name; *p; ++p, ++len)
        {
            const char c = *p;
            const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                            (c >= '0' && c <= '9') || c == '_';
            if (!ok)
                return false;
        }
        return len <= 128;
    }

    static std::mutex g_Mutex;
    static std::map<std::string, std::int32_t> g_Constants;

    static void WriteConstant(lua_State* L, const char* name, std::int32_t value)
    {
        const int top0 = g_lua_gettop(L);
        g_lua_getfield(L, LUA_GLOBALSINDEX_51, const_cast<char*>("TppEquip"));
        if (g_lua_type(L, -1) == LUA_TTABLE)
        {
            g_lua_pushstring(L, const_cast<char*>(name));
            g_lua_pushnumber(L, static_cast<lua_Number>(value));
            g_lua_settable(L, -3);
        }
        g_lua_settop(L, top0);
    }
}

bool TppEquipConst_Declare(
    int spaceIndex,
    const char* name,
    std::uint32_t& outValue,
    lua_State* L)
{
    outValue = 0;
    if (spaceIndex < 0 || spaceIndex >= kTppEquipConstSpaceCount)
        return false;
    if (!IsValidConstantName(name))
    {
        Log("[TppEquipConst] ERROR: invalid constant name '%s' (identifiers only, [A-Za-z0-9_], max 128 chars); rejected.\n",
            name ? name : "(null)");
        return false;
    }

    std::int32_t value = 0;
    if (spaceIndex == kSpaceEqp)
    {
        if (!V_FrameWorkState::ResolveOrCreateEquipId(name, 0, value))
        {
            Log("[TppEquipConst] ERROR: no free native equip slot for '%s'.\n", name);
            return false;
        }
    }
    else if (!V_FrameWorkState::ResolveOrCreateConstantValue(
                 kSpaceTags[spaceIndex], name, kDefaultConstantBase, value))
    {
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        g_Constants[name] = value;
    }

    if (L && ResolveLuaApi())
        WriteConstant(L, name, value);

    outValue = static_cast<std::uint32_t>(value);
#ifdef _DEBUG
    Log("[TppEquipConst] TppEquip.%s = %d (space %s)\n",
        name, value, kSpaceTags[spaceIndex]);
#endif
    return true;
}

void TppEquipConst_InjectAll(lua_State* L)
{
    std::map<std::string, std::int32_t> snapshot;
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        snapshot = g_Constants;
    }
    if (snapshot.empty() || !L || !ResolveLuaApi())
        return;

    for (const auto& kv : snapshot)
        WriteConstant(L, kv.first.c_str(), kv.second);

#ifdef _DEBUG
    Log("[TppEquipConst] injected %u custom constant(s) into L=%p\n",
        static_cast<unsigned>(snapshot.size()), L);
#endif
}

void TppEquipConst_ResetRuntimeState()
{
    std::lock_guard<std::mutex> lock(g_Mutex);
    g_Constants.clear();
}

int TppEquipDeclareForSpace(lua_State* L, int spaceIndex)
{
    const char* name = GetLuaString(L, 1);

    std::uint32_t value = 0;
    if (!name || !name[0] ||
        !TppEquipConst_Declare(spaceIndex, name, value, L))
    {
        PushLuaBool(L, false);
        return 1;
    }

    if (g_lua_pushnumber)
        g_lua_pushnumber(L, static_cast<lua_Number>(value));
    else
        PushLuaBool(L, true);
    return 1;
}
