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

namespace
{
    using FoxLuaRegisterLibrary_t = void(__fastcall*)(lua_State* L, const char* libName, luaL_Reg* funcs);

    // EN (mgsvtpp.exe, base 0x140000000) fallback — used only until AddressSet is resolved.
    static constexpr std::uintptr_t BOOTSTRAP_EN_FoxLuaRegisterLibrary = 0x14006B6D0ull;

    static FoxLuaRegisterLibrary_t g_FoxLuaRegisterLibrary = nullptr;

    static bool ResolveLuaApi()
    {
        if (!g_FoxLuaRegisterLibrary)
        {
            const std::uintptr_t addr = gAddr.FoxLuaRegisterLibrary
                ? gAddr.FoxLuaRegisterLibrary
                : BOOTSTRAP_EN_FoxLuaRegisterLibrary;
            g_FoxLuaRegisterLibrary = reinterpret_cast<FoxLuaRegisterLibrary_t>(ResolveGameAddress(addr));
        }
        return g_FoxLuaRegisterLibrary != nullptr;
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

        { nullptr, nullptr }
    };
}

bool Register_V_TppSoundDaemonLibrary(lua_State* L)
{
    if (!L || !ResolveLuaApi())
        return false;

    g_FoxLuaRegisterLibrary(L, "V_TppSoundDaemon", g_VTppSoundDaemonLib);
    Log("[V_FrameWork] Registered library: V_TppSoundDaemon (L=%p)\n", L);
    return true;
}
