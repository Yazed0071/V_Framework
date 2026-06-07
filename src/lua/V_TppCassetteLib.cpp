#include "pch.h"

extern "C" {
    #include "lua.h"
    #include "lauxlib.h"
}

#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"
#include "V_TppCassetteLib.h"
#include "CassetteFunctions.h"
#include "LuaApi.h"


namespace
{
    static luaL_Reg g_VTppCassetteLib[] =
    {
        { "PlayCassetteTapeByTrackId",                l_PlayCassetteTapeByTrackId },
        { "GetTapeTrackDirectPlayId",                 l_GetTapeTrackDirectPlayId },
        { "GetCassettePlayingTime",                   l_GetCassettePlayingTime },
        { "GetCassettePlayingTrackId",                l_GetCassettePlayingTrackId },
        { "PauseCassette",                            l_PauseCassette },
        { "ResumeCassette",                           l_ResumeCassette },
        { "StopCassette",                             l_StopCassette },
        { "IsCassetteSpeakerEnabled",                 l_IsCassetteSpeakerEnabled },
        { "SetCassetteSpeakerEnabled",                l_SetCassetteSpeakerEnabled },

        { nullptr,          nullptr }
    };
}

bool Register_V_TppCassetteLibrary(lua_State* L)
{
    return RegisterLuaLibrary(L, "V_CassetteCommand", g_VTppCassetteLib);
}
