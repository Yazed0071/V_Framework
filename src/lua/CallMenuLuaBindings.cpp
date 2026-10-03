#include "pch.h"
#include "CallMenuLuaBindings.h"

extern "C" {
    #include "lua.h"
}

#include <Windows.h>
#include <cstdint>
#include <cstring>
#include <string>

#include "CallMenuCommandSet_UpdateState.h"
#include "FoxHashes.h"
#include "LuaApi.h"
#include "log.h"

namespace
{
    constexpr int           kLuaNil         = 0;
    constexpr int           kLuaNumber      = 3;
    constexpr int           kLuaString      = 4;
    constexpr int           kLuaTable       = 5;
    constexpr std::uint64_t kStringIdMask   = 0xFFFFFFFFFFFFull;   // lang labels are 48-bit string ids
    constexpr int           kSheetCells     = 4;                   // an icon .ftex is a 2x2 sheet

    std::uint64_t LangLabel(const char* key)
    {
        return FoxHashes::StrCode64(key) & kStringIdMask;
    }

    bool ReadItem(lua_State* L, int index, const char* function, int& item)
    {
        if (LuaType(L, index) == kLuaString)
        {
            const char* key = GetLuaString(L, index);
            item = CallMenuItems::FindItemByKey(key ? key : "");
            if (item == CallMenuItems::kNoItem)
            {
                LogWarn("%s: no item has the key '%s', ignored", function, key ? key : "");
                return false;
            }
            return true;
        }

        if (!LuaIsNumber(L, index))
        {
            LogWarn("%s: item must be a V_CallMenuItem value, an AddCallMenuItem id or an item key, ignored", function);
            return false;
        }

        item = GetLuaInt(L, index);
        if (!CallMenuItems::IsKnownItem(item))
        {
            LogWarn("%s: item %d doesn't exist, ignored", function, item);
            return false;
        }
        return true;
    }

    bool ReadRowToTake(lua_State* L, int index, const char* function, int& target)
    {
        target = GetLuaInt(L, index);
        if (CallMenuItems::HasFixedRow(target))
            return true;

        LogWarn("%s: inPlaceOf %d has no row of its own to take, ignored", function, target);
        return false;
    }

    bool ReadStringField(lua_State* L, const char* field, const char*& value)
    {
        LuaGetField(L, 1, field);
        const bool isString = LuaType(L, -1) == kLuaString;
        value = isString ? GetLuaString(L, -1) : nullptr;
        LuaPop(L, 1);
        return isString;
    }

    bool ReadNumberField(lua_State* L, const char* field, int& value)
    {
        LuaGetField(L, 1, field);
        const bool isNumber = LuaType(L, -1) == kLuaNumber;
        if (isNumber)
            value = GetLuaInt(L, -1);
        LuaPop(L, 1);
        return isNumber;
    }

    bool IsFieldSet(lua_State* L, const char* field)
    {
        LuaGetField(L, 1, field);
        const bool isSet = LuaType(L, -1) != kLuaNil;
        LuaPop(L, 1);
        return isSet;
    }

    void ReadVoices(lua_State* L, std::vector<std::uint32_t>& voices)
    {
        LuaGetField(L, 1, "voice");
        if (LuaType(L, -1) == kLuaTable)
        {
            const int count = static_cast<int>(LuaObjLen(L, -1));
            for (int i = 1; i <= count; ++i)
            {
                LuaRawGetI(L, -1, i);
                const int type = LuaType(L, -1);
                if (type == kLuaNumber)
                    voices.push_back(static_cast<std::uint32_t>(GetLuaInt64(L, -1)));
                else if (type == kLuaString)
                    voices.push_back(FoxHashes::FNVHash32(GetLuaString(L, -1)));
                else
                    LogWarn("AddCallMenuItem: voice entry %d is not a voice label or id, skipped", i);
                LuaPop(L, 1);
            }
        }
        LuaPop(L, 1);
    }

    bool IsSavableKey(const char* key)
    {
        for (const char* p = key; *p; ++p)
            if (*p == '"' || *p == '\\' || *p == '\n' || *p == '\r')
                return false;
        return key[0] != '\0';
    }

    bool EndsWithFtex(const std::string& path)
    {
        constexpr std::size_t kExtLength = 5;
        return path.size() > kExtLength && _stricmp(path.c_str() + path.size() - kExtLength, ".ftex") == 0;
    }

    bool ReadUvPair(lua_State* L, const char* field, float (&uv)[2])
    {
        LuaGetField(L, 1, field);
        bool read = false;
        if (LuaType(L, -1) == kLuaTable)
        {
            float values[2] = {};
            read = true;
            for (int i = 0; i < 2; ++i)
            {
                LuaRawGetI(L, -1, i + 1);
                if (LuaType(L, -1) == kLuaNumber)
                    values[i] = GetLuaNumber(L, -1);
                else
                    read = false;
                LuaPop(L, 1);
            }
            if (read)
            {
                uv[0] = values[0];
                uv[1] = values[1];
            }
        }
        const bool wrongType = !read && LuaType(L, -1) != kLuaNil;
        LuaPop(L, 1);
        if (wrongType)
            LogWarn("AddCallMenuItem: %s must be { u, v }, ignored", field);
        return read;
    }

    void ReadIcon(lua_State* L, CallMenuItems::CustomItem& item)
    {
        LuaGetField(L, 1, "icon");
        const int type = LuaType(L, -1);
        if (type == kLuaNumber)
        {
            const int icon = GetLuaInt(L, -1);
            if (icon > CallMenuItems::NoIcon && icon < CallMenuItems::IconCount)
                item.icon = static_cast<CallMenuItems::Icon>(icon);
            else
                LogWarn("AddCallMenuItem: icon %d is not a V_CallMenuIcon value, no logo", icon);
        }
        else if (type == kLuaString)
        {
            std::string path = GetLuaString(L, -1);
            if (!EndsWithFtex(path))
                path += ".ftex";
            item.iconTexture = FoxHashes::PathCode64Ext(path);
        }
        else if (type != kLuaNil)
        {
            LogWarn("AddCallMenuItem: icon must be a V_CallMenuIcon value or an .ftex path, no logo");
        }
        LuaPop(L, 1);

        int cell = 0;
        if (ReadNumberField(L, "iconCell", cell))
        {
            if (cell >= 0 && cell < kSheetCells)
                item.iconCell = static_cast<std::uint8_t>(cell);
            else
                LogWarn("AddCallMenuItem: iconCell must be 0 to 3, using 0");
        }

        item.hasIconUvShift = ReadUvPair(L, "iconUvShift", item.iconUvShift);
        float repeat[2] = {};
        if (ReadUvPair(L, "iconUvRepeat", repeat))
        {
            if (repeat[0] > 0.0f && repeat[1] > 0.0f)
            {
                item.iconUvRepeat[0] = repeat[0];
                item.iconUvRepeat[1] = repeat[1];
            }
            else
                LogWarn("AddCallMenuItem: iconUvRepeat must be above 0, using { 0.5, 0.5 }");
        }
    }

    template <typename Action>
    int WithItem(lua_State* L, const char* function, Action action)
    {
        int item = CallMenuItems::kNoItem;
        if (ReadItem(L, 1, function, item))
            action(item);
        return 0;
    }
}

int __cdecl l_AddCallMenuItem(lua_State* L)
{
    if (LuaType(L, 1) != kLuaTable)
    {
        LogWarn("AddCallMenuItem: takes a table, item not added");
        PushLuaNil(L);
        return 1;
    }

    CallMenuItems::CustomItem item;

    const char* key = nullptr;
    if (!ReadStringField(L, "key", key))
    {
        LogWarn("AddCallMenuItem: key is missing or not a string, item not added");
        PushLuaNil(L);
        return 1;
    }
    if (!IsSavableKey(key))
    {
        LogWarn("AddCallMenuItem: key '%s' is empty or has a quote, backslash or line break, item not added", key);
        PushLuaNil(L);
        return 1;
    }
    item.key = key;

    const char* label = nullptr;
    if (!ReadStringField(L, "label", label))
    {
        LogWarn("AddCallMenuItem: label is missing, item not added");
        PushLuaNil(L);
        return 1;
    }
    item.label = LangLabel(label);

    const char* description = nullptr;
    item.description = ReadStringField(L, "description", description) ? LangLabel(description) : item.label;

    const char* message = nullptr;
    if (ReadStringField(L, "message", message))
        item.message = FoxHashes::StrCode32(message);
    else if (IsFieldSet(L, "message"))
        LogWarn("AddCallMenuItem: message must be a string, ItemSelected sends the item id instead");

    const char* exclusive = nullptr;
    if (ReadStringField(L, "exclusive", exclusive))
        item.exclusive = FoxHashes::StrCode32(exclusive);
    else if (IsFieldSet(L, "exclusive"))
        LogWarn("AddCallMenuItem: exclusive must be a string, item shares pages with other mods");

    int column = -1;
    if (!ReadNumberField(L, "column", column) || column < 0 || column >= CallMenuItems::ColumnCount)
    {
        LogWarn("AddCallMenuItem: column must be a V_CallMenuColumn value, item not added");
        PushLuaNil(L);
        return 1;
    }
    item.column = static_cast<CallMenuItems::Column>(column);

    int when = CallMenuItems::Always;
    if (ReadNumberField(L, "when", when) && (when < 0 || when >= CallMenuItems::ConditionCount))
    {
        LogWarn("AddCallMenuItem: when must be a V_CallMenuCondition value, using ALWAYS");
        when = CallMenuItems::Always;
    }
    item.when = static_cast<CallMenuItems::Condition>(when);

    if (IsFieldSet(L, "inPlaceOf"))
    {
        LuaGetField(L, 1, "inPlaceOf");
        if (!LuaIsNumber(L, -1) || !ReadRowToTake(L, -1, "AddCallMenuItem", item.inPlaceOf))
            item.inPlaceOf = CallMenuItems::kNoItem;
        LuaPop(L, 1);
    }

    ReadIcon(L, item);
    ReadVoices(L, item.voices);

    const int id = CallMenuItems::AddCustomItem(item);
    LogInfo("AddCallMenuItem: '%s' added as item %d, saved as '%s'", label, id, item.key.c_str());
    PushLuaNumber(L, static_cast<float>(id));
    return 1;
}

int __cdecl l_GetCallMenuItem(lua_State* L)
{
    const int item = LuaType(L, 1) == kLuaString ? CallMenuItems::FindItemByKey(GetLuaString(L, 1)) : CallMenuItems::kNoItem;
    if (item == CallMenuItems::kNoItem)
        PushLuaNil(L);
    else
        PushLuaNumber(L, static_cast<float>(item));
    return 1;
}

int __cdecl l_EnableCallMenuItem(lua_State* L)
{
    return WithItem(L, "EnableCallMenuItem", [](int item) { CallMenuItems::SetEnabled(item, true); });
}

int __cdecl l_DisableCallMenuItem(lua_State* L)
{
    return WithItem(L, "DisableCallMenuItem", [](int item) { CallMenuItems::SetEnabled(item, false); });
}

int __cdecl l_ShowCallMenuItem(lua_State* L)
{
    return WithItem(L, "ShowCallMenuItem", [](int item) { CallMenuItems::SetVisible(item, true); });
}

int __cdecl l_HideCallMenuItem(lua_State* L)
{
    return WithItem(L, "HideCallMenuItem", [](int item) { CallMenuItems::SetVisible(item, false); });
}

int __cdecl l_ResetCallMenuItem(lua_State* L)
{
    return WithItem(L, "ResetCallMenuItem", [](int item) { CallMenuItems::Reset(item); });
}

int __cdecl l_ResetAllCallMenuItems(lua_State* L)
{
    UNREFERENCED_PARAMETER(L);
    CallMenuItems::ResetAll();
    return 0;
}

int __cdecl l_SetCallMenuItemInPlaceOf(lua_State* L)
{
    int item = CallMenuItems::kNoItem;
    if (!ReadItem(L, 1, "SetCallMenuItemInPlaceOf", item))
        return 0;

    if (LuaType(L, 2) == kLuaNil)
    {
        CallMenuItems::SetInPlaceOf(item, CallMenuItems::kNoItem);
        return 0;
    }

    int target = CallMenuItems::kNoItem;
    if (!LuaIsNumber(L, 2) || !ReadRowToTake(L, 2, "SetCallMenuItemInPlaceOf", target))
        return 0;

    CallMenuItems::SetInPlaceOf(item, target);
    return 0;
}
