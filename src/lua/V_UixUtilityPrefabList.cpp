#include <pch.h>

#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#include "LuaApi.h"
#include "log.h"
#include "FoxHashes.h"
#include "V_UixUtilityPrefabList.h"

#include "../hooks/ui/PrefabListControl.h"
#include "../hooks/ui/UixUtilityControl.h"
#include "../hooks/ui/UixUtilityDev.h"

namespace
{
    using PrefabListControl::RowText;

    constexpr int LUA_T_NIL     = 0;
    constexpr int LUA_T_NUMBER  = 3;
    constexpr int LUA_T_STRING  = 4;
    constexpr int LUA_T_TABLE   = 5;

    constexpr std::uint64_t ID48_MASK = 0xFFFFFFFFFFFFull;

    bool Present(lua_State* L, int idx)
    {
        const int type = LuaType(L, idx);
        return type != LUA_T_NIL && type != -1;
    }

    std::uint64_t Name64(lua_State* L, int idx)
    {
        const int type = LuaType(L, idx);
        if (type == LUA_T_NUMBER)
            return GetLuaInt64(L, idx);
        if (type != LUA_T_STRING)
            return 0;
        const char* s = GetLuaString(L, idx);
        if (s && s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
            return std::strtoull(s + 2, nullptr, 16);
        return s && *s ? FoxHashes::StrCode64(s) : 0;
    }

    std::string Text(lua_State* L, int idx)
    {
        const int type = LuaType(L, idx);
        const char* s = type == LUA_T_STRING || type == LUA_T_NUMBER ? GetLuaString(L, idx) : nullptr;
        return s ? s : "";
    }

    std::uint32_t WindowByName(std::uint64_t name)
    {
        using namespace UixUtilityControl;
        void* fn = Method(dev::FindWindow);
        void* window = nullptr;
        auto find = [&]() { window = reinterpret_cast<void*(__fastcall*)(void**, std::uint64_t)>(fn)(Utility(), name); };
        if (fn && name)
            Guarded(find);
        return HandleFor(Kind::Window, window, window);
    }

    int PushNumber(lua_State* L, double value)
    {
        g_lua_pushnumber(L, value);
        return 1;
    }

    int Integer(lua_State* L, const char* field, int fallback)
    {
        LuaGetField(L, 1, field);
        const int value = LuaType(L, -1) == LUA_T_NUMBER ? static_cast<int>(GetLuaNumber(L, -1)) : fallback;
        LuaPop(L, 1);
        return value;
    }

    std::uint64_t Name(lua_State* L, const char* field)
    {
        LuaGetField(L, 1, field);
        const std::uint64_t name = Name64(L, -1) & ID48_MASK;
        LuaPop(L, 1);
        return name;
    }

    std::vector<std::uint64_t> Names(lua_State* L, const char* field)   // one name, or a table of names
    {
        std::vector<std::uint64_t> names;
        LuaGetField(L, 1, field);
        const int idx = GetLuaTop(L);
        const int type = LuaType(L, idx);
        if (type == LUA_T_STRING || type == LUA_T_NUMBER)
            names.push_back(Name64(L, idx) & ID48_MASK);
        else if (type == LUA_T_TABLE)
        {
            const int count = static_cast<int>(LuaObjLen(L, idx));
            for (int i = 1; i <= count && i <= PrefabListControl::kMaxColumns; ++i)
            {
                LuaRawGetI(L, idx, i);
                names.push_back(Name64(L, GetLuaTop(L)) & ID48_MASK);
                LuaPop(L, 1);
            }
        }
        LuaPop(L, 1);
        return names;
    }

    RowText ReadRow(lua_State* L, int idx)   // a string, or a table of strings, one per text node
    {
        RowText row;
        const int type = LuaType(L, idx);
        if (type == LUA_T_STRING || type == LUA_T_NUMBER)
            row.push_back(Text(L, idx));
        else if (type == LUA_T_TABLE)
        {
            const int columns = static_cast<int>(LuaObjLen(L, idx));
            for (int c = 1; c <= columns && c <= PrefabListControl::kMaxColumns; ++c)
            {
                LuaRawGetI(L, idx, c);
                row.push_back(Text(L, GetLuaTop(L)));
                LuaPop(L, 1);
            }
        }
        return row;
    }

    std::vector<RowText> ReadRows(lua_State* L, int idx)
    {
        std::vector<RowText> rows;
        if (LuaType(L, idx) != LUA_T_TABLE)
            return rows;
        const int count = static_cast<int>(LuaObjLen(L, idx));
        for (int i = 1; i <= count && i <= PrefabListControl::kMaxRows; ++i)
        {
            LuaRawGetI(L, idx, i);
            rows.push_back(ReadRow(L, GetLuaTop(L)));
            LuaPop(L, 1);
        }
        return rows;
    }
}

namespace UixUtilityPrefabList
{
    int __cdecl l_SetupPrefabList(lua_State* L)
    {
        if (!PrefabListControl::IsAvailable())
            return 0;
        if (LuaType(L, 1) != LUA_T_TABLE)
        {
            LogWarnOnce("V_UixUtility.SetupPrefabList: argument must be a table {window=..., list=..., visible=..., rows={...}, rowModel=..., rowText=...}");
            return 0;
        }

        PrefabListControl::SetupRequest request;
        LuaGetField(L, 1, "window");
        if (LuaType(L, -1) == LUA_T_NUMBER)
            request.window = static_cast<std::uint32_t>(GetLuaInt64(L, -1));
        else if (LuaType(L, -1) == LUA_T_STRING)
            request.window = WindowByName(Name64(L, -1) & ID48_MASK);
        LuaPop(L, 1);
        request.list = Name(L, "list");
        if (!request.window || !request.list)
        {
            LogWarnOnce("V_UixUtility.SetupPrefabList: window (a handle or a window name) and list (a child window name) are required");
            return 0;
        }

        request.visible = Integer(L, "visible", 0);
        request.count = Integer(L, "count", -1);
        request.cursor = Integer(L, "cursor", 1);
        request.top = Integer(L, "top", 0);
        request.flags = static_cast<std::uint32_t>(Integer(L, "flags", 0));

        LuaGetField(L, 1, "rows");
        request.rows = ReadRows(L, GetLuaTop(L));
        LuaPop(L, 1);
        LuaGetField(L, 1, "title");
        if (Present(L, -1))
            request.title = ReadRow(L, GetLuaTop(L));
        LuaPop(L, 1);

        request.rowModel = Name(L, "rowModel");
        request.rowTextNodes = Names(L, "rowText");
        request.titleModel = Name(L, "titleModel");
        request.titleTextNodes = Names(L, "titleText");

        const std::uint32_t list = PrefabListControl::Setup(request);
        if (!list)
            return 0;
        return PushNumber(L, static_cast<double>(list));
    }

    int __cdecl l_SetPrefabListRows(lua_State* L)
    {
        if (!PrefabListControl::IsAvailable() || LuaType(L, 1) != LUA_T_NUMBER)
            return 0;
        const std::uint32_t list = static_cast<std::uint32_t>(GetLuaInt64(L, 1));
        PrefabListControl::SetRows(list, ReadRows(L, 2));
        return 0;
    }

    int __cdecl l_GetPrefabListCursor(lua_State* L)
    {
        if (!PrefabListControl::IsAvailable() || LuaType(L, 1) != LUA_T_NUMBER)
            return 0;
        int absolute = 0, top = 0;
        if (!PrefabListControl::GetCursor(static_cast<std::uint32_t>(GetLuaInt64(L, 1)), absolute, top))
            return 0;
        PushNumber(L, absolute);
        PushNumber(L, top);
        return 2;
    }

    int __cdecl l_ClosePrefabList(lua_State* L)
    {
        if (!PrefabListControl::IsAvailable() || LuaType(L, 1) != LUA_T_NUMBER)
            return 0;
        PrefabListControl::Close(static_cast<std::uint32_t>(GetLuaInt64(L, 1)));
        return 0;
    }
}
