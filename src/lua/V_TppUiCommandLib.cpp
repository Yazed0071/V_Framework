#include "pch.h"

extern "C" {
    #include "lua.h"
    #include "lauxlib.h"
}

#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"
#include "V_TppUiCommandLib.h"
#include "UiCommandFunctions.h"


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

    static luaL_Reg g_VTppUiCommandLib[] =
    {
        { "SetDefaultEquipBgTexturePath",             l_SetDefaultEquipBgTexturePath },
        { "ClearDefaultEquipBgTexture",               l_ClearDefaultEquipBgTexture },
        { "SetEquipBgTexturePath",                    l_SetEquipBgTexturePath },
        { "ClearEquipBgTexture",                      l_ClearEquipBgTexture },
        { "SetEnemyWeaponBgTexturePath",              l_SetEnemyWeaponBgTexturePath },
        { "ClearEnemyWeaponBgTexture",                l_ClearEnemyWeaponBgTexture },
        { "SetEnemyEquipBgTexturePath",               l_SetEnemyEquipBgTexturePath },
        { "ClearEnemyEquipBgTexture",                 l_ClearEnemyEquipBgTexture },
        { "ClearAllEquipBgTextures",                  l_ClearAllEquipBgTextures },
        { "SetLoadingSplashMainTexturePath",          l_SetLoadingSplashMainTexturePath },
        { "SetLoadingSplashBlurTexturePath",          l_SetLoadingSplashBlurTexturePath },
        { "ClearLoadingSplashTextures",               l_ClearLoadingSplashTextures },
        { "SetGameOverSplashMainTexturePath",         l_SetGameOverSplashMainTexturePath },
        { "SetGameOverSplashBlurTexturePath",         l_SetGameOverSplashBlurTexturePath },
        { "ClearGameOverSplashTextures",              l_ClearGameOverSplashTextures },

        { "SetEquipIdIconFtexPath",                   l_SetEquipIdIconFtexPath },
        { "ClearIconFtexPath",                        l_ClearIconFtexPath },
        { "ClearAllIconFtexPaths",                    l_ClearAllIconFtexPaths },
        { "SetMissionEmergency",                      l_SetMissionEmergency },
        { "IsMissionEmergency",                       l_IsMissionEmergency },
        { "ClearAllMissionEmergencies",               l_ClearAllMissionEmergencies },
        { "SetEmergencyMissionPopup",                 l_SetEmergencyMissionPopup },
        { "SetEmergencyMissionPopupLangId",           l_SetEmergencyMissionPopupLangId },
        { "ClearEmergencyMissionPopupOverride",       l_ClearEmergencyMissionPopupOverride },
        { "ShowMissionIcon",                          l_ShowMissionIcon },
        { "ShowTimeCigaretteUi",                      l_ShowTimeCigaretteUi },
        { "HideTimeCigaretteUi",                      l_HideTimeCigaretteUi },
        { "ShowMbDvcAnnouncePopupReport",             l_ShowMbDvcAnnouncePopupReport },
        { "ShowMbDvcAnnouncePopupReportLangId",       l_ShowMbDvcAnnouncePopupReportLangId },
        { "ShowMbDvcAnnouncePopupReward",             l_ShowMbDvcAnnouncePopupReward },
        { "ShowMbDvcAnnouncePopupRewardLangId",       l_ShowMbDvcAnnouncePopupRewardLangId },

        { "SetEnemyInformationLangId",                l_SetEnemyInformationLangId },
        { "ClearEnemyInformationLangId",              l_ClearEnemyInformationLangId },
        { "SetEnemyUnitName",                         l_SetEnemyUnitName },
        { "ClearEnemyUnitName",                       l_ClearEnemyUnitName },

        { "SetEnemyInformationLangIdForSoldier",      l_SetEnemyInformationLangIdForSoldier },
        { "ClearEnemyInformationLangIdForSoldier",    l_ClearEnemyInformationLangIdForSoldier },
        { "ClearAllEnemyInformationLangIdForSoldiers",l_ClearAllEnemyInformationLangIdForSoldiers },
        { "SetEnemyUnitNameForSoldier",               l_SetEnemyUnitNameForSoldier },
        { "ClearEnemyUnitNameForSoldier",             l_ClearEnemyUnitNameForSoldier },
        { "ClearAllEnemyUnitNameForSoldiers",         l_ClearAllEnemyUnitNameForSoldiers },

        { nullptr,          nullptr }
    };
}

bool Register_V_TppUiCommandLibrary(lua_State* L)
{
    if (!L || !ResolveLuaApi())
        return false;

    g_FoxLuaRegisterLibrary(L, "V_TppUiCommand", g_VTppUiCommandLib);
    Log("[V_FrameWork] Registered library: V_TppUiCommand (L=%p)\n", L);
    return true;
}
