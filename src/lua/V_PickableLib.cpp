#include "pch.h"

extern "C" {
    #include "lua.h"
    #include "lauxlib.h"
}

#include <cstdint>

#include "V_PickableLib.h"
#include <TppPickableRuntime.h>
#include "LuaApi.h"

namespace
{
    static bool ResolvePickableLocatorIndex(lua_State* L, const char* locatorName, std::uint32_t& outIndex)
    {
        outIndex = 0;

        if (!locatorName || !*locatorName)
            return false;

        if (!ResolveLuaApi() || !g_lua_pcall || !g_lua_settop)
            return false;

        const int top = GetLuaTop(L);

        LuaGetField(L, LUA_GLOBALSINDEX_51, "TppPickable");
        if (LuaType(L, -1) != LUA_TTABLE)
        {
            g_lua_settop(L, top);
            return false;
        }

        LuaGetField(L, -1, "GetLocatorIndex");
        if (LuaType(L, -1) != LUA_TFUNCTION)
        {
            g_lua_settop(L, top);
            return false;
        }

        PushLuaString(L, locatorName);
        if (g_lua_pcall(L, 1, 1, 0) != 0 || !LuaIsNumber(L, -1))
        {
            g_lua_settop(L, top);
            return false;
        }

        const long long locatorIndex =
            static_cast<long long>(GetLuaInt64(L, -1));
        g_lua_settop(L, top);

        if (locatorIndex < 0 || locatorIndex > 0xFFFF)
            return false;

        outIndex = static_cast<std::uint32_t>(locatorIndex);
        return true;
    }


    struct PickableFieldName
    {
        const char* key;
        std::uint32_t fieldId;
    };

    static const PickableFieldName g_PickableFieldNames[] =
    {
        { "equipId",        kTppPickableFieldEquipId },
        { "countRaw",       kTppPickableFieldCountRaw },
        { "secondCountRaw", kTppPickableFieldSecondCountRaw },
        { "countMax",       kTppPickableFieldCountMax },
        { "secondCountMax", kTppPickableFieldSecondCountMax },
        { "infoType",       kTppPickableFieldInfoType },
        { "flags",          kTppPickableFieldFlags },
    };

    static bool ApplyPickableInfoTable(lua_State* L, int tableIdx, std::uint32_t locatorIndex)
    {
        if (LuaType(L, tableIdx) != LUA_TTABLE)
            return false;

        bool applied = false;
        for (const PickableFieldName& field : g_PickableFieldNames)
        {
            LuaGetField(L, tableIdx, field.key);
            if (LuaIsNumber(L, -1))
            {
                applied = Set_TppPickableFieldByIndex(
                              locatorIndex,
                              field.fieldId,
                              static_cast<std::uint32_t>(GetLuaInt64(L, -1))) || applied;
            }
            LuaPop(L, 1);
        }
        return applied;
    }

    static int PushPickableInfoResult(lua_State* L, std::uint32_t locatorIndex)
    {
        std::uint16_t words[8] = {};
        if (!Get_TppPickableInfoWordsByIndex(locatorIndex, words) ||
            !ResolveLuaApi() || !g_lua_createtable || !g_lua_settable)
        {
            PushLuaNil(L);
            return 1;
        }

        g_lua_createtable(L, 0, 8);

        const auto pushField = [L](const char* key, float value)
        {
            PushLuaString(L, key);
            PushLuaNumber(L, value);
            g_lua_settable(L, -3);
        };

        pushField("locatorIndex",   static_cast<float>(locatorIndex));
        pushField("equipId",        static_cast<float>(words[0] & 0x7FF));
        pushField("countRaw",       static_cast<float>(words[2]));
        pushField("secondCountRaw", static_cast<float>(words[3]));
        pushField("countMax",       static_cast<float>(words[4]));
        pushField("secondCountMax", static_cast<float>(words[5]));
        pushField("infoType",       static_cast<float>(words[6] & 0xFF));
        pushField("flags",          static_cast<float>(words[7]));
        return 1;
    }


    static int __cdecl l_SetCountRawByLocatorIndex(lua_State* L)
    {
        const int locatorIndex = GetLuaInt(L, 1);
        const int countRaw = GetLuaInt(L, 2);

        const bool ok = Set_TppPickableCountRawByIndex(
            static_cast<std::uint32_t>(locatorIndex),
            static_cast<std::uint32_t>(countRaw));

        PushLuaBool(L, ok);
        return 1;
    }


    static int __cdecl l_GetCountRawByLocatorIndex(lua_State* L)
    {
        const int locatorIndex = GetLuaInt(L, 1);

        std::uint16_t countRaw = 0;
        const bool ok = Get_TppPickableCountRawByIndex(
            static_cast<std::uint32_t>(locatorIndex),
            countRaw);

        if (!ok)
        {
            PushLuaNil(L);
            return 1;
        }

        PushLuaNumber(L, static_cast<float>(countRaw));
        return 1;
    }


    static int __cdecl l_SetCountRawByLocatorName(lua_State* L)
    {
        const char* name = GetLuaString(L, 1);
        const int countRaw = GetLuaInt(L, 2);

        std::uint32_t locatorIndex = 0;
        const bool ok =
            ResolvePickableLocatorIndex(L, name, locatorIndex) &&
            Set_TppPickableCountRawByIndex(locatorIndex, static_cast<std::uint32_t>(countRaw));

        PushLuaBool(L, ok);
        return 1;
    }


    static int __cdecl l_GetCountRawByLocatorName(lua_State* L)
    {
        const char* name = GetLuaString(L, 1);

        std::uint32_t locatorIndex = 0;
        std::uint16_t countRaw = 0;
        if (!ResolvePickableLocatorIndex(L, name, locatorIndex) ||
            !Get_TppPickableCountRawByIndex(locatorIndex, countRaw))
        {
            PushLuaNil(L);
            return 1;
        }

        PushLuaNumber(L, static_cast<float>(countRaw));
        return 1;
    }


    static int __cdecl l_SetEquipIdByLocatorIndex(lua_State* L)
    {
        const int locatorIndex = GetLuaInt(L, 1);
        const int equipId = GetLuaInt(L, 2);

        PushLuaBool(L, Set_TppPickableFieldByIndex(
            static_cast<std::uint32_t>(locatorIndex),
            kTppPickableFieldEquipId,
            static_cast<std::uint32_t>(equipId)));
        return 1;
    }


    static int __cdecl l_SetEquipIdByLocatorName(lua_State* L)
    {
        const char* name = GetLuaString(L, 1);
        const int equipId = GetLuaInt(L, 2);

        std::uint32_t locatorIndex = 0;
        const bool ok =
            ResolvePickableLocatorIndex(L, name, locatorIndex) &&
            Set_TppPickableFieldByIndex(locatorIndex, kTppPickableFieldEquipId,
                                        static_cast<std::uint32_t>(equipId));

        PushLuaBool(L, ok);
        return 1;
    }


    static int __cdecl l_GetEquipIdByLocatorIndex(lua_State* L)
    {
        const int locatorIndex = GetLuaInt(L, 1);

        std::uint16_t words[8] = {};
        if (!Get_TppPickableInfoWordsByIndex(static_cast<std::uint32_t>(locatorIndex), words))
        {
            PushLuaNil(L);
            return 1;
        }

        PushLuaNumber(L, static_cast<float>(words[0] & 0x7FF));
        return 1;
    }


    static int __cdecl l_GetEquipIdByLocatorName(lua_State* L)
    {
        const char* name = GetLuaString(L, 1);

        std::uint32_t locatorIndex = 0;
        std::uint16_t words[8] = {};
        if (!ResolvePickableLocatorIndex(L, name, locatorIndex) ||
            !Get_TppPickableInfoWordsByIndex(locatorIndex, words))
        {
            PushLuaNil(L);
            return 1;
        }

        PushLuaNumber(L, static_cast<float>(words[0] & 0x7FF));
        return 1;
    }


    static int __cdecl l_SetInfoByLocatorIndex(lua_State* L)
    {
        const int locatorIndex = GetLuaInt(L, 1);

        PushLuaBool(L, ApplyPickableInfoTable(
            L, 2, static_cast<std::uint32_t>(locatorIndex)));
        return 1;
    }


    static int __cdecl l_SetInfoByLocatorName(lua_State* L)
    {
        const char* name = GetLuaString(L, 1);

        std::uint32_t locatorIndex = 0;
        const bool ok =
            ResolvePickableLocatorIndex(L, name, locatorIndex) &&
            ApplyPickableInfoTable(L, 2, locatorIndex);

        PushLuaBool(L, ok);
        return 1;
    }


    static int __cdecl l_GetInfoByLocatorIndex(lua_State* L)
    {
        const int locatorIndex = GetLuaInt(L, 1);
        return PushPickableInfoResult(L, static_cast<std::uint32_t>(locatorIndex));
    }


    static int __cdecl l_GetInfoByLocatorName(lua_State* L)
    {
        const char* name = GetLuaString(L, 1);

        std::uint32_t locatorIndex = 0;
        if (!ResolvePickableLocatorIndex(L, name, locatorIndex))
        {
            PushLuaNil(L);
            return 1;
        }

        return PushPickableInfoResult(L, locatorIndex);
    }


    static luaL_Reg g_VPickableLib[] =
    {
        { "SetCountRawByLocatorIndex", l_SetCountRawByLocatorIndex },
        { "GetCountRawByLocatorIndex", l_GetCountRawByLocatorIndex },
        { "SetCountRawByLocatorName",  l_SetCountRawByLocatorName },
        { "GetCountRawByLocatorName",  l_GetCountRawByLocatorName },
        { "SetEquipIdByLocatorIndex",  l_SetEquipIdByLocatorIndex },
        { "GetEquipIdByLocatorIndex",  l_GetEquipIdByLocatorIndex },
        { "SetEquipIdByLocatorName",   l_SetEquipIdByLocatorName },
        { "GetEquipIdByLocatorName",   l_GetEquipIdByLocatorName },
        { "SetInfoByLocatorIndex",     l_SetInfoByLocatorIndex },
        { "SetInfoByLocatorName",      l_SetInfoByLocatorName },
        { "GetInfoByLocatorIndex",     l_GetInfoByLocatorIndex },
        { "GetInfoByLocatorName",      l_GetInfoByLocatorName },

        { nullptr,          nullptr }
    };
}

bool Register_V_PickableLibrary(lua_State* L)
{
    return RegisterLuaLibrary(L, "V_Pickable", g_VPickableLib);
}
