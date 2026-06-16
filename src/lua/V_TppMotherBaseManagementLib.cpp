#include "pch.h"

extern "C" {
    #include "lua.h"
    #include "lauxlib.h"
}

#include "log.h"
#include "LuaApi.h"
#include "V_TppMotherBaseManagementLib.h"
#include "ChangeLocationMenu.h"
#include "GetPhotoAdditionalTextLangId.h"

namespace
{
    // V_TppMotherBaseManagement.AddToChangeLocationMenu({ locationCode1, locationCode2, ... })
    static int __cdecl l_AddToChangeLocationMenu(lua_State* L)
    {
        if (LuaType(L, -1) != LUA_TTABLE)
        {
            Log("[V_TppMotherBaseManagement] AddToChangeLocationMenu expected a table\n");
            return 0;
        }

        for (g_lua_pushnil(L); g_lua_next(L, -2); LuaPop(L, 1))
        {
            if (LuaType(L, -1) == LUA_TNUMBER)
                AddLocationIdToChangeLocationMenu(static_cast<unsigned short>(GetLuaInt(L, -1)));
        }
        return 0;
    }

    // V_TppMotherBaseManagement.AddPhotoAdditionalText({ {missionCode=, photoId=, photoType=, targetTypeLangId=""}, ... })
    static int __cdecl l_AddPhotoAdditionalText(lua_State* L)
    {
        if (LuaType(L, -1) != LUA_TTABLE)
        {
            Log("[V_TppMotherBaseManagement] AddPhotoAdditionalText expected a table\n");
            return 0;
        }

        for (g_lua_pushnil(L); g_lua_next(L, -2); LuaPop(L, 1))
        {
            if (LuaType(L, -1) != LUA_TTABLE)
                continue;

            unsigned short missionCode = 0xFFFF;
            unsigned char photoId = 0xFF;
            unsigned char photoType = 0xFF;
            const char* targetTypeLangIdStr = "";

            LuaGetField(L, -1, "missionCode");
            if (LuaType(L, -1) == LUA_TNUMBER)
                missionCode = static_cast<unsigned short>(GetLuaInt(L, -1));
            LuaPop(L, 1);

            LuaGetField(L, -1, "photoId");
            if (LuaType(L, -1) == LUA_TNUMBER)
                photoId = static_cast<unsigned char>(GetLuaInt(L, -1));
            LuaPop(L, 1);

            LuaGetField(L, -1, "photoType");
            if (LuaType(L, -1) == LUA_TNUMBER)
                photoType = static_cast<unsigned char>(GetLuaInt(L, -1));
            LuaPop(L, 1);

            LuaGetField(L, -1, "targetTypeLangId");
            if (LuaType(L, -1) == LUA_TSTRING)
                targetTypeLangIdStr = GetLuaString(L, -1);
            LuaPop(L, 1);

            if (missionCode == 0xFFFF || photoId == 0xFF || photoType == 0xFF)
                continue;

            AddPhotoAdditionalText(missionCode, photoId, photoType, targetTypeLangIdStr);
        }
        return 0;
    }

    static luaL_Reg g_VTppMotherBaseManagementLib[] =
    {
        { "AddToChangeLocationMenu", l_AddToChangeLocationMenu },
        { "AddPhotoAdditionalText",  l_AddPhotoAdditionalText },

        { nullptr, nullptr }
    };
}

bool Register_V_TppMotherBaseManagementLibrary(lua_State* L)
{
    return RegisterLuaLibrary(L, "V_TppMotherBaseManagement", g_VTppMotherBaseManagementLib);
}
