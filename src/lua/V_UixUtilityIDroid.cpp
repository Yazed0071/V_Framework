#include <pch.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

#include "LuaApi.h"
#include "log.h"
#include "FoxHashes.h"
#include "V_UixUtilityIDroid.h"

#include "../hooks/ui/MbDvcManageEv_CustomPage.h"
#include "../hooks/ui/MotherBaseDeviceTopMenuTableImpl_CustomItems.h"

namespace
{
    constexpr int LUA_T_NIL     = 0;
    constexpr int LUA_T_NUMBER  = 3;
    constexpr int LUA_T_STRING  = 4;
    constexpr int LUA_T_TABLE   = 5;

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

    std::string FieldText(lua_State* L, int table, const char* field)
    {
        LuaGetField(L, table, field);
        const char* s = LuaType(L, -1) == LUA_T_STRING ? GetLuaString(L, -1) : nullptr;
        std::string text = s ? s : "";
        LuaPop(L, 1);
        return text;
    }

    std::uint64_t FieldName64(lua_State* L, int table, const char* field)
    {
        LuaGetField(L, table, field);
        const std::uint64_t value = Present(L, -1) ? Name64(L, -1) : 0;
        LuaPop(L, 1);
        return value;
    }

    bool IsTexturePath(const std::string& text)
    {
        return text.size() > 5 && _stricmp(text.c_str() + text.size() - 5, ".ftex") == 0;
    }

    float FieldNumber(lua_State* L, int table, const char* field)
    {
        LuaGetField(L, table, field);
        const float value = LuaType(L, -1) == LUA_T_NUMBER ? GetLuaNumber(L, -1) : 0.0f;
        LuaPop(L, 1);
        return value;
    }

    bool ReadMenuEntry(lua_State* L, int table, const char* field, std::uint8_t& entry)
    {
        LuaGetField(L, table, field);
        const int type = LuaType(L, -1);
        bool ok = false;
        if (type == LUA_T_NUMBER)
        {
            const int id = GetLuaInt(L, -1);
            ok = id > 0 && id <= 0xFE;
            entry = static_cast<std::uint8_t>(id);
        }
        else if (type == LUA_T_STRING)
        {
            ok = mbTopMenuCustomItems::VanillaEntryByMenuName(GetLuaString(L, -1), entry);
        }
        LuaPop(L, 1);
        return ok;
    }
}

namespace VUixUtilityIDroid
{
    int __cdecl l_AddIDroidMenuItem(lua_State* L)
    {
        if (LuaType(L, 1) != LUA_T_TABLE)
        {
            LogWarnOnce("V_UixUtility.AddIDroidMenuItem: argument must be a table {parent=..., name=..., label=...}");
            return 0;
        }

        mbTopMenuCustomItems::ItemSpec spec;
        if (!ReadMenuEntry(L, 1, "parent", spec.parent))
        {
            LogWarn("V_UixUtility.AddIDroidMenuItem: parent must be a TppTerminal.MBDVCMENU entry or an item id");
            return 0;
        }
        const std::string iconText = FieldText(L, 1, "icon");
        if (IsTexturePath(iconText))
        {
            spec.iconTexture = FoxHashes::PathCode64Ext(FoxHashes::NormalizeAssetPath(iconText));
            spec.iconU = FieldNumber(L, 1, "iconU");
            spec.iconV = FieldNumber(L, 1, "iconV");
            if (!spec.iconTexture || spec.iconU < 0.0f || spec.iconU >= 1.0f || spec.iconV < 0.0f || spec.iconV >= 1.0f)
            {
                LogWarn("V_UixUtility.AddIDroidMenuItem: icon %s could not be used (iconU and iconV go from 0 to below 1), the item keeps its group's icon",
                        iconText.c_str());
                spec.iconTexture = 0;
            }
        }
        else
        {
            ReadMenuEntry(L, 1, "icon", spec.icon);
        }
        spec.name = FieldText(L, 1, "name");
        spec.label = FieldText(L, 1, "label");
        spec.description = FieldText(L, 1, "description");
        spec.labelLang = FieldName64(L, 1, "labelLang");
        spec.descriptionLang = FieldName64(L, 1, "descriptionLang");
        LuaGetField(L, 1, "group");
        spec.group = GetLuaBool(L, -1);
        LuaPop(L, 1);

        const std::uint8_t id = mbTopMenuCustomItems::AddItem(spec);
        if (!id)
            return 0;
        g_lua_pushnumber(L, id);
        return 1;
    }

    int __cdecl l_RemoveIDroidMenuItem(lua_State* L)
    {
        PushLuaBool(L, LuaType(L, 1) == LUA_T_NUMBER && mbTopMenuCustomItems::RemoveItem(static_cast<std::uint8_t>(GetLuaInt(L, 1))));
        return 1;
    }

    int __cdecl l_OpenIDroidPage(lua_State* L)
    {
        PushLuaBool(L, mbDvcCustomPage::OpenPage(Name64(L, 1)));
        return 1;
    }

    int __cdecl l_CloseIDroidPage(lua_State* L)
    {
        PushLuaBool(L, mbDvcCustomPage::ClosePage(Present(L, 1) ? Name64(L, 1) : 0));
        return 1;
    }

    int __cdecl l_IsIDroidPageOpen(lua_State* L)
    {
        PushLuaBool(L, mbDvcCustomPage::IsPageOpen(Name64(L, 1)));
        return 1;
    }
}
