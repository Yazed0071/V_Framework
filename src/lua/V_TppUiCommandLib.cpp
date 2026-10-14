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
#include "LuaApi.h"
#include "../hooks/ui/UixUtilityControl.h"
#include "../hooks/ui/LoadingSplash.h"
#include "../hooks/ui/GameOverSplash.h"

namespace
{
    // ---- the game's loading tips and game over menu, read for Lua mods that draw their own versions ----------------

    using GetInstance_t = void* (*)();
    // tips table vfunc +0x10: (table, tip index, out tip id, out title lang id, out body lang id, out page, out page count)
    using GetTipInfo_t = void(__fastcall*)(void* table, std::uint32_t index, std::uint64_t* tipId, std::uint64_t* titleLangId,
                                           std::uint64_t* bodyLangId, std::int32_t* page, std::int32_t* pageCount);

    constexpr std::uint64_t LANG_ID_NONE = 0xB8A0BF169F98ull;   // the tips table's "no text"
    constexpr std::uint64_t ID48_MASK = 0xFFFFFFFFFFFFull;

    // hud::CommonDataManager
    constexpr std::uintptr_t CDM_TIPS_STARTED = 0x157E;
    constexpr std::uintptr_t CDM_TIPS_ENDED   = 0x1586;
    // tpp::ui::LoadingTipsEv
    constexpr std::uintptr_t TIPS_STATE       = 0x1B0;   // 0 splash, 1 waiting for the tips text, 2 active, 3 ending
    constexpr std::uintptr_t TIPS_TABLE       = 0x1D0;
    constexpr std::uintptr_t TIPS_CATEGORY    = 0x93C;   // 4 = loading tips
    constexpr std::uintptr_t TIPS_INDEX       = 0x940;
    constexpr std::uintptr_t TIPS_COUNT       = 0x942;
    constexpr std::uintptr_t TIPS_PAGE_TURN   = 0xA28;   // 0 none, 1 old page out, 2 waiting, 3 new text, 4 new page in, 5 waiting
    constexpr std::uintptr_t TIPS_RESUME      = 0xA31;   // the player may now leave the screen
    constexpr std::uintptr_t TIPS_HIDDEN      = 0xA32;   // another full-screen menu covers the tips
    // tpp::ui::GameOverEvCall
    constexpr std::uintptr_t GO_STATE         = 0x1E0;   // 0..10, see the wiki
    constexpr std::uintptr_t GO_SELECTED      = 0x1E4;
    constexpr std::uintptr_t GO_PREVIOUS      = 0x1E8;
    constexpr std::uintptr_t GO_ITEMS         = 0x1F0;   // GameOverMenuItem*[4], lang id at item +8
    constexpr std::uintptr_t GO_COUNT         = 0x210;
    constexpr std::uintptr_t GO_TYPE          = 0x214;   // 0 normal, 1 Cyprus, 2 time paradox
    constexpr std::uintptr_t GO_ITEM_LANG     = 0x08;
    constexpr std::uintptr_t GAME_OVER_OPEN_EN = 0x142BF2698ull;   // set by GameOver::StartGameOver, cleared when the menu ends
    constexpr std::uintptr_t GAME_OVER_EVENT_VTABLE_EN = 0x14224A3F0ull;

    struct TipsInfo
    {
        bool session = false;
        bool seen = false;   // a tips session ran since the game started
        int  state = 0, category = 0, tipIndex = 0, tipCount = 0, pageTurn = 0;
        bool resume = false, hidden = false, hasTip = false;
        std::uint64_t tipId = LANG_ID_NONE, title = LANG_ID_NONE, body = LANG_ID_NONE;
        std::int32_t page = 0, pageCount = 0;
    };

    bool ReadTipsSEH(TipsInfo& t)
    {
        __try
        {
            if (gAddr.HudCommonDataManager_GetInstance)
            {
                const auto getInstance = reinterpret_cast<GetInstance_t>(ResolveGameAddress(gAddr.HudCommonDataManager_GetInstance));
                const auto* cdm = getInstance ? static_cast<const std::uint8_t*>(getInstance()) : nullptr;
                t.session = cdm && cdm[CDM_TIPS_STARTED] && !cdm[CDM_TIPS_ENDED];
            }
            const auto* ev = static_cast<const std::uint8_t*>(LoadingSplash_TipsEvent());
            if (!ev)
                return true;
            t.seen = true;
            t.state = ev[TIPS_STATE];
            t.category = ev[TIPS_CATEGORY];
            t.tipIndex = *reinterpret_cast<const std::uint16_t*>(ev + TIPS_INDEX);
            t.tipCount = *reinterpret_cast<const std::uint16_t*>(ev + TIPS_COUNT);
            t.pageTurn = ev[TIPS_PAGE_TURN];
            t.resume = ev[TIPS_RESUME] != 0;
            t.hidden = ev[TIPS_HIDDEN] != 0;
            void* table = *reinterpret_cast<void* const*>(ev + TIPS_TABLE);
            // only in state 2, the one state the game itself reads tips in: at a load's start the UI thread rebuilds
            // the table (state 0) while this runs on the Lua thread
            if (t.session && t.state == 2 && t.category == 4 && t.tipIndex < t.tipCount && table)
            {
                const auto getTipInfo = reinterpret_cast<GetTipInfo_t>((*static_cast<void***>(table))[2]);
                getTipInfo(table, static_cast<std::uint8_t>(t.tipIndex), &t.tipId, &t.title, &t.body, &t.page, &t.pageCount);
                t.tipId &= ID48_MASK;   // ids are 48 bits; the game compares them masked too
                t.title &= ID48_MASK;
                t.body &= ID48_MASK;
                t.hasTip = true;
            }
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    struct GameOverInfo
    {
        int  open = -1;   // -1 unknown on this build
        bool seen = false;   // a game over menu ran since the game started
        int  state = 0, selected = 0, previous = 0, count = 0, type = 0;
        std::uint64_t items[4] = {};
    };

    bool ReadGameOverSEH(GameOverInfo& g)
    {
        __try
        {
            const bool en = gGameBuild == ::AddressSetRuntime::GameBuild::En_1_0_15_4 || gGameBuild == ::AddressSetRuntime::GameBuild::En_1_0_15_4a;
            if (en)
                g.open = *static_cast<const std::uint8_t*>(ResolveGameAddress(GAME_OVER_OPEN_EN)) ? 1 : 0;
            void* vtable = nullptr;
            const auto* ev = static_cast<const std::uint8_t*>(GameOverSplash_GameOverEvent(&vtable));
            g.seen = ev != nullptr;
            if (!ev || g.open == 0)   // the event is made anew each mission: read it only while its menu is open
            {
                g.open = 0;
                return true;
            }
            const void* current = *reinterpret_cast<void* const*>(ev);
            if (current != vtable || (en && current != ResolveGameAddress(GAME_OVER_EVENT_VTABLE_EN)))
            {
                g.open = en ? g.open : 0;   // last mission's event is gone and this one's has not been seen yet
                return true;
            }
            g.state = *reinterpret_cast<const std::int32_t*>(ev + GO_STATE);
            g.selected = *reinterpret_cast<const std::int32_t*>(ev + GO_SELECTED);
            g.previous = *reinterpret_cast<const std::int32_t*>(ev + GO_PREVIOUS);
            g.type = *reinterpret_cast<const std::int32_t*>(ev + GO_TYPE);
            const std::uint32_t count = *reinterpret_cast<const std::uint32_t*>(ev + GO_COUNT);
            g.count = count > 4 ? 4 : static_cast<int>(count);
            for (int i = 0; i < g.count; ++i)
            {
                const auto* item = *reinterpret_cast<const std::uint8_t* const*>(ev + GO_ITEMS + i * sizeof(void*));
                g.items[i] = item ? *reinterpret_cast<const std::uint64_t*>(item + GO_ITEM_LANG) & ID48_MASK : 0;
            }
            if (g.open == -1)   // no open flag known on this build: the menu runs in states 1..9, 10 is its end
                g.open = (g.state >= 1 && g.state <= 9) ? 1 : 0;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    void SetNumberField(lua_State* L, const char* key, double value)
    {
        g_lua_pushstring(L, const_cast<char*>(key));
        g_lua_pushnumber(L, value);
        g_lua_settable(L, -3);
    }

    void SetBoolField(lua_State* L, const char* key, bool value)
    {
        g_lua_pushstring(L, const_cast<char*>(key));
        g_lua_pushboolean(L, value ? 1 : 0);
        g_lua_settable(L, -3);
    }

    // GetLoadingTipsInfo() -> table about the game's loading tips screen (nil if it cannot be read on this build):
    // session (true while the tips screen runs), state, tipIndex, tipCount, pageTurn, resumePermitted, hidden, and when
    // a tip is shown: tipId, titleLangId, bodyLangId (for V_UixUtility.GetLangText; text exists from state 2), page, pageCount
    int __cdecl l_GetLoadingTipsInfo(lua_State* L)
    {
        TipsInfo t;
        if (!ReadTipsSEH(t))
            return 0;
        g_lua_createtable(L, 0, 14);
        SetBoolField(L, "session", t.session);
        SetBoolField(L, "seen", t.seen);
        SetNumberField(L, "state", t.state);
        SetNumberField(L, "tipIndex", t.tipIndex);
        SetNumberField(L, "tipCount", t.tipCount);
        SetNumberField(L, "pageTurn", t.pageTurn);
        SetBoolField(L, "resumePermitted", t.resume);
        SetBoolField(L, "hidden", t.hidden);
        if (t.hasTip)
        {
            SetNumberField(L, "tipId", static_cast<double>(t.tipId));
            if (t.title != LANG_ID_NONE)
                SetNumberField(L, "titleLangId", static_cast<double>(t.title));
            if (t.body != LANG_ID_NONE)
                SetNumberField(L, "bodyLangId", static_cast<double>(t.body));
            SetNumberField(L, "page", t.page);
            SetNumberField(L, "pageCount", t.pageCount);
        }
        return 1;
    }

    // GetGameOverInfo() -> table about the game's game over menu (nil if it cannot be read): open (true from
    // StartGameOver until the menu ends; on builds without that flag, while the menu is in states 1..9), state,
    // selected, previous, count, type, and items = { langId, ... } in row order (the row texts, which also tell the rows
    // apart). While open is false the menu fields stay 0: the game makes the menu anew each mission.
    int __cdecl l_GetGameOverInfo(lua_State* L)
    {
        GameOverInfo g;
        if (!ReadGameOverSEH(g))
            return 0;
        g_lua_createtable(L, 0, 8);
        if (g.open >= 0)
            SetBoolField(L, "open", g.open == 1);
        SetBoolField(L, "seen", g.seen);
        SetNumberField(L, "state", g.state);
        SetNumberField(L, "selected", g.selected);
        SetNumberField(L, "previous", g.previous);
        SetNumberField(L, "count", g.count);
        SetNumberField(L, "type", g.type);
        g_lua_pushstring(L, const_cast<char*>("items"));
        g_lua_createtable(L, g.count, 0);
        for (int i = 0; i < g.count; ++i)
        {
            g_lua_pushnumber(L, static_cast<double>(i + 1));
            g_lua_pushnumber(L, static_cast<double>(g.items[i]));
            g_lua_settable(L, -3);
        }
        g_lua_settable(L, -3);
        return 1;
    }

    // LoadResidentUiPack(packPath[, sizeBytes]) -> true once asked for; the pack then stays loaded for the rest of the game
    int __cdecl l_LoadResidentUiPack(lua_State* L)
    {
        const char* path = GetLuaString(L, 1);
        if (!path || !*path)
        {
            LogWarn("V_TppUiCommand.LoadResidentUiPack: no pack path given");
            return 0;
        }
        float size = (LuaType(L, 2) == LUA_TNUMBER) ? GetLuaNumber(L, 2) : 1024.0f * 1024.0f;
        if (size < 64.0f * 1024.0f)
            size = 64.0f * 1024.0f;   // Block::Load itself needs some of the block
        if (size > 32.0f * 1024.0f * 1024.0f)
            size = 32.0f * 1024.0f * 1024.0f;
        g_lua_pushboolean(L, UixUtilityControl::LoadResidentUiPack(path, static_cast<std::uint32_t>(size)) ? 1 : 0);
        return 1;
    }

    // IsResidentUiPackLoaded(packPath) -> true once a pack asked for with LoadResidentUiPack is loaded
    int __cdecl l_IsResidentUiPackLoaded(lua_State* L)
    {
        const char* path = GetLuaString(L, 1);
        g_lua_pushboolean(L, path && *path && UixUtilityControl::IsResidentUiPackLoaded(path) ? 1 : 0);
        return 1;
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
        { "SetMissionTelopSplashTexturePath",         l_SetMissionTelopSplashTexturePath },
        { "UnsetMissionTelopSplashTexturePath",       l_UnsetMissionTelopSplashTexturePath },

        { "SetEquipIconFtexPath",                     l_SetEquipIconFtexPath },
        { "ClearIconFtexPath",                        l_ClearIconFtexPath },
        { "ClearAllIconFtexPaths",                    l_ClearAllIconFtexPaths },
        { "SetMissionEmergency",                      l_SetMissionEmergency },
        { "IsMissionEmergency",                       l_IsMissionEmergency },
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
        { "SetEnemyUnitName",                         l_SetEnemyUnitName },

        { "SetEnemyInformationLangIdForSoldier",      l_SetEnemyInformationLangIdForSoldier },
        { "SetEnemyUnitNameForSoldier",               l_SetEnemyUnitNameForSoldier },

        { "SetAnnounceLogSE",                         l_SetAnnounceLogSE },
        { "RegisterAnnounceLogSfx",                   l_RegisterAnnounceLogSfx },
        { "UnsetAnnounceLogSE",                       l_UnsetAnnounceLogSE },
        { "UnregisterAnnounceLogSfx",                 l_UnregisterAnnounceLogSfx },

        { "SetMissionAcceptWarning",                  l_SetMissionAcceptWarning },

        { "SetMissionMenuHelp",                       l_SetMissionMenuHelp },
        { "ClearMissionMenuHelp",                     l_ClearMissionMenuHelp },
        { "SetEquipLangInfo",                         l_SetEquipLangInfo },

        { "LoadResidentUiPack",                       l_LoadResidentUiPack },
        { "IsResidentUiPackLoaded",                   l_IsResidentUiPackLoaded },
        { "GetLoadingTipsInfo",                       l_GetLoadingTipsInfo },
        { "GetGameOverInfo",                          l_GetGameOverInfo },

        { nullptr,          nullptr }
    };
}

bool Register_V_TppUiCommandLibrary(lua_State* L)
{
    return RegisterLuaLibrary(L, "V_TppUiCommand", g_VTppUiCommandLib);
}
