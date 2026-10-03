#include "pch.h"

extern "C" {
    #include "lua.h"
    #include "lauxlib.h"
}

#include <cstdint>

#include "LuaApi.h"
#include "V_PlayerLib.h"
#include "PlayerFunctions.h"
#include "OutfitLuaBindings.h"
#include "CallMenuLuaBindings.h"

namespace
{
    static luaL_Reg g_VTppPlayerLib[] =
    {
        { "SetPlayerVoiceFpkPathForType",             l_SetPlayerVoiceFpkPathForType },
        { "ClearPlayerVoiceFpkPathForType",           l_ClearPlayerVoiceFpkPathForType },
        { "ClearAllPlayerVoiceFpkOverrides",          l_ClearAllPlayerVoiceFpkOverrides },

        { "SetPlayerVoiceTypeForType",                l_SetPlayerVoiceTypeForType },
        { "ClearPlayerVoiceTypeForType",              l_ClearPlayerVoiceTypeForType },
        { "ClearAllPlayerVoiceTypeOverrides",         l_ClearAllPlayerVoiceTypeOverrides },

        { "IsBarrierActive",                          l_IsBarrierActive },

        { "RequestToSetTargetCqcStance",              l_RequestToSetTargetCqcStance },

        { "IsThereEnoughSpaceAroundPlayer",           l_IsThereEnoughSpaceAroundPlayer },
        { "RequestToAttachInDemo",                    l_RequestToAttachInDemo },
        { "ClearAttachInDemo",                        l_ClearAttachInDemo },

        { "SetQuietHoldCqc",                          l_SetQuietHoldCqc },
        { "SetQuietInterrogate",                      l_SetQuietInterrogate },

        { "AddCallMenuItem",                          l_AddCallMenuItem },
        { "GetCallMenuItem",                          l_GetCallMenuItem },
        { "EnableCallMenuItem",                       l_EnableCallMenuItem },
        { "DisableCallMenuItem",                      l_DisableCallMenuItem },
        { "ShowCallMenuItem",                         l_ShowCallMenuItem },
        { "HideCallMenuItem",                         l_HideCallMenuItem },
        { "ResetCallMenuItem",                        l_ResetCallMenuItem },
        { "ResetAllCallMenuItems",                    l_ResetAllCallMenuItems },
        { "SetCallMenuItemInPlaceOf",                 l_SetCallMenuItemInPlaceOf },

        { "RegisterOutfit",                           l_RegisterOutfit },
        { "RegisterHeadOption",                       l_RegisterHeadOption },
        { "ExtendVanillaOutfit",                      l_ExtendVanillaOutfit },
        { "GetOutfitInfo",                            l_GetOutfitInfo },
        { "GetHeadOptionSlot",                        l_GetHeadOptionSlot },
        { "GetHeadOptions",                           l_GetHeadOptions },
        { "PlayHeadMotion",                           l_PlayHeadMotion },
        { "PlayHeadEffect",                           l_PlayHeadEffect },
        { "StopHeadEffect",                           l_StopHeadEffect },
        { "PlayHeadSound",                            l_PlayHeadSound },

        { nullptr,          nullptr }
    };
}

bool Register_V_TppPlayerLibrary(lua_State* L)
{
    return RegisterLuaLibrary(L, "V_Player", g_VTppPlayerLib);
}
