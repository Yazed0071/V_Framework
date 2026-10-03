#include "pch.h"

#include <cstdint>
#include <string>

#include "LuaApi.h"
#include "log.h"
#include "V_HelicopterLib.h"

#include "../hooks/sound/HeliVoice.h"
#include "../hooks/heli/HeliSoundController.h"
#include "../hooks/heli/FieldTaxiMenu.h"
#include "../hooks/outfit/HeadMotion.h"
#include "../hooks/player/AccSeatIdleMotion.h"

namespace
{
    static int __cdecl l_SetEnableHeliVoice(lua_State* L)
    {
        const bool isEnable = GetLuaBool(L, 1);
        const char* voiceEvt = GetLuaString(L, 2);
        const char* radioEvt = GetLuaString(L, 3);

        if (isEnable && (!voiceEvt || !*voiceEvt || !radioEvt || !*radioEvt))
        {
            LogWarn("SetEnableHeliVoice: enable=true needs non-empty voice and radio events, ignored");
            PushLuaBool(L, false);
            return 1;
        }

        const bool ok = SetEnableHeliVoice(isEnable,
                                           voiceEvt ? voiceEvt : "",
                                           radioEvt ? radioEvt : "");
        PushLuaBool(L, ok);
        return 1;
    }

    static int __cdecl l_PilotCallVoice(lua_State* L)
    {
        const std::uint32_t voiceId = GetLuaFnvHash32Arg(L, 1);
        PushLuaBool(L, Play_PilotCallVoice(voiceId, 0u, 0u, 0u));
        return 1;
    }

    static int __cdecl l_PilotCallRadio(lua_State* L)
    {
        const std::uint32_t line1 = GetLuaFnvHash32Arg(L, 1);
        if (GetLuaTop(L) >= 2 && (LuaIsString(L, 2) || LuaIsNumber(L, 2)))
        {
            const std::uint32_t line2 = GetLuaFnvHash32Arg(L, 2);
            PushLuaBool(L, Play_PilotCallVoice(line2, 0, 2, line1));
        }
        else
        {
            PushLuaBool(L, Play_PilotCallVoice(line1, 0, 1, 0));
        }
        return 1;
    }

    static int __cdecl l_SetFieldTaxiMissionEnabled(lua_State* L)
    {
        const unsigned int code    = static_cast<unsigned int>(GetLuaNumber(L, 1));
        const bool         enabled = (GetLuaTop(L) >= 2) ? GetLuaBool(L, 2) : true;
        FieldTaxi_SetMissionEnabled(code, enabled);
        return 0;
    }

    static int __cdecl l_SetTaxiLandingZoneHidden(lua_State* L)
    {
        const unsigned int lzNameHash = GetLuaStrCode32Arg(L, 1);
        const bool         hidden     = (GetLuaTop(L) >= 2) ? GetLuaBool(L, 2) : true;
        FieldTaxi_SetTaxiLandingZoneHidden(lzNameHash, hidden);
        return 0;
    }

    static int __cdecl l_SetTaxiRideState(lua_State* L)
    {
        FieldTaxi_SetTaxiRideState(static_cast<unsigned int>(GetLuaNumber(L, 1)));
        return 0;
    }

    static int __cdecl l_ResetTaxiState(lua_State* /*L*/)
    {
        FieldTaxi_ResetTaxiState();
        return 0;
    }

    static int __cdecl l_SetPlayerDropSeatPose(lua_State* L)
    {
        std::uint64_t clip = 0;
        const char* text = (GetLuaTop(L) >= 1 && LuaIsString(L, 1)) ? GetLuaString(L, 1) : nullptr;
        if (text && *text)
        {
            std::string path = text;
            const std::size_t slash = path.find_last_of('/');
            if (slash != std::string::npos && path.find('.', slash) == std::string::npos)
                path += ".gani";
            clip = outfit::headmotion::ParseClipId(path.c_str());
            if (!clip)
            {
                LogWarn("SetPlayerDropSeatPose: '%s' is not a clip path or id, ignored", text);
                PushLuaBool(L, false);
                return 1;
            }
        }
        const bool anyMission = GetLuaTop(L) < 2 || !LuaIsNumber(L, 2);
        const std::uint32_t mission = anyMission ? 0 : static_cast<std::uint32_t>(GetLuaInt(L, 2));
        PushLuaBool(L, AccSeatIdleMotion_SetDropSeatPose(clip, mission, anyMission));
        return 1;
    }

    static luaL_Reg g_VHelicopterLib[] =
    {
        { "SetEnableHeliVoice",                    l_SetEnableHeliVoice },
        { "PilotCallVoice",                        l_PilotCallVoice },
        { "PilotCallRadio",                        l_PilotCallRadio },

        { "SetFieldTaxiMissionEnabled",            l_SetFieldTaxiMissionEnabled },
        { "SetTaxiLandingZoneHidden",              l_SetTaxiLandingZoneHidden },
        { "SetTaxiRideState",                      l_SetTaxiRideState },
        { "ResetTaxiState",                        l_ResetTaxiState },

        { "SetPlayerDropSeatPose",                 l_SetPlayerDropSeatPose },

        { nullptr, nullptr }
    };
}

bool Register_V_HelicopterLibrary(lua_State* L)
{
    return RegisterLuaLibrary(L, "V_Helicopter", g_VHelicopterLib);
}
