#include "pch.h"

#include <cstdint>
#include <string>

#include "LuaApi.h"
#include "log.h"
#include "V_TitleSequenceLib.h"

#include "../hooks/outfit/HeadMotion.h"
#include "../hooks/player/AccIDroidPutAway.h"
#include "../hooks/player/AccSeatIdleMotion.h"

namespace
{
    static int __cdecl l_SetPlayIDroidEndMotion(lua_State* L)
    {
        AccIDroidPutAway_SetEnabled((GetLuaTop(L) >= 1) ? GetLuaBool(L, 1) : true);
        return 0;
    }

    bool ApplySeatIdle(std::uint64_t clip)
    {
        const std::uint64_t before = AccSeatIdleMotion_Replacement();
        const bool ok = AccSeatIdleMotion_Set(clip);
        LogInfo("SetMotion: 0x%016llX (was 0x%016llX, ok %d)", clip, before, ok ? 1 : 0);
        if (ok && AccSeatIdleMotion_Replacement() != before)
            AccIDroidPutAway_RequestSeatIdleRestart();
        return ok;
    }

    static int __cdecl l_SetMotion(lua_State* L)
    {
        const char* text = (GetLuaTop(L) >= 1 && LuaIsString(L, 1)) ? GetLuaString(L, 1) : nullptr;
        if (!text || !*text)
        {
            PushLuaBool(L, ApplySeatIdle(0));
            return 1;
        }
        std::string path = text;
        const std::size_t slash = path.find_last_of('/');
        if (slash != std::string::npos && path.find('.', slash) == std::string::npos)
            path += ".gani";
        const std::uint64_t clip = outfit::headmotion::ParseClipId(path.c_str());
        if (!clip)
        {
            LogWarn("SetMotion: '%s' is not a clip path or id, ignored", text);
            PushLuaBool(L, false);
            return 1;
        }
        PushLuaBool(L, ApplySeatIdle(clip));
        return 1;
    }

    static luaL_Reg g_VTitleSequenceLib[] =
    {
        { "SetPlayIDroidEndMotion",                l_SetPlayIDroidEndMotion },
        { "SetMotion",                             l_SetMotion },

        { nullptr, nullptr }
    };
}

bool Register_V_TitleSequenceLibrary(lua_State* L)
{
    return RegisterLuaLibrary(L, "V_title_sequence", g_VTitleSequenceLib);
}
