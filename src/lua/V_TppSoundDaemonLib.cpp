#include "pch.h"

extern "C" {
    #include "lua.h"
    #include "lauxlib.h"
}

#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"
#include "V_TppSoundDaemonLib.h"
#include "SoundDaemonFunctions.h"
#include "LuaApi.h"
#include "../hooks/sound/GameOverMusic.h"

namespace
{
    static int __cdecl l_SetGameOverMusic(lua_State* L)
    {
        const bool isEnable = GetLuaBool(L, 1);
        const int  typeRaw  = GetLuaInt(L, 2);
        const char* playEvt = GetLuaString(L, 3);
        const char* stopEvt = GetLuaString(L, 4);

        if (typeRaw < GAME_OVER_GENERAL || typeRaw > GAME_OVER_CYPRUS)
        {
            Log("[GameOverMusic] SetGameOverMusic: invalid type=%d (expected 0..3)\n", typeRaw);
            PushLuaBool(L, false);
            return 1;
        }

        if (isEnable && (!playEvt || !*playEvt || !stopEvt || !*stopEvt))
        {
            Log("[GameOverMusic] SetGameOverMusic: enable=true requires non-empty play/stop event strings\n");
            PushLuaBool(L, false);
            return 1;
        }

        const bool ok = SetGameOverMusic(isEnable,
                                         static_cast<GAME_OVER_TYPE>(typeRaw),
                                         playEvt ? playEvt : "",
                                         stopEvt ? stopEvt : "");
        PushLuaBool(L, ok);
        return 1;
    }

    static luaL_Reg g_VTppSoundDaemonLib[] =
    {
        // RTPC (raw Wwise)
        { "SetSoldierRtpc",                 l_SetSoldierRtpc },
        { "SetGlobalRtpc",                  l_SetGlobalRtpc },
        { "SetSoldierRtpcById",             l_SetSoldierRtpcById },
        { "SetGlobalRtpcById",              l_SetGlobalRtpcById },
        { "SetRtpcByAkObjId",               l_SetRtpcByAkObjId },
        { "SetRtpcByAkObjIdById",           l_SetRtpcByAkObjIdById },
        { "SetRtpcLoggingEnabled",          l_SetRtpcLoggingEnabled },
        { "IsRtpcLoggingEnabled",           l_IsRtpcLoggingEnabled },

        // RTPC (per-soldier via SoundController)
        { "SetSoldierObjectRtpc",           l_SetSoldierObjectRtpc },
        { "SetSoldierObjectRtpcByName",     l_SetSoldierObjectRtpcByName },

        // Voice pitch
        { "SetGlobalVoicePitch",            l_SetGlobalVoicePitch },
        { "GetGlobalVoicePitch",            l_GetGlobalVoicePitch },
        { "SetPitchByAkObjId",              l_SetPitchByAkObjId },
        { "ClearPitchByAkObjId",            l_ClearPitchByAkObjId },
        { "ClearAllPerAkObjIdPitchBiases",  l_ClearAllPerAkObjIdPitchBiases },
        { "GetSoldierAkObjId",              l_GetSoldierAkObjId },
        { "SetSoldierVoicePitch",           l_SetSoldierVoicePitch },

        { "SetGameOverMusic",               l_SetGameOverMusic },

        { nullptr, nullptr }
    };
}

bool Register_V_TppSoundDaemonLibrary(lua_State* L)
{
    return RegisterLuaLibrary(L, "V_TppSoundDaemon", g_VTppSoundDaemonLib);
}
