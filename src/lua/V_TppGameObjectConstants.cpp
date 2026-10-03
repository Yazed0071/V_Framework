#include "pch.h"

extern "C" {
    #include "lua.h"
}

#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "V_TppGameObjectConstants.h"
#include "CallMenuCommandSet_UpdateState.h"
#include "LuaApi.h"
#include "log.h"


namespace
{

    struct ConstEntry { const char* name; lua_Number value; };

    static const ConstEntry V_TppGameObject[] = {
        {"GAME_OVER_GENERAL", 0},
        {"GAME_OVER_PARADOX", 1},
        {"GAME_OVER_STEALTH", 2},
        {"GAME_OVER_CYPRUS",  3},
        { nullptr, 0.0 }
    };
    static const ConstEntry V_TppMbDev[] = {
        {"EQP_OUTFIT_VARIANT_GRADE", 0},
        {"EQP_OUTFIT_VARIANT_NAME", 79},
        { nullptr, 0.0 }
    };
    static const ConstEntry V_PlayerCqcStance[] = {
        {"STAND", 0},
        {"SQUAT", 1},
        { nullptr, 0.0 }
    };
    static const ConstEntry V_TppDataBase[] = {
        {"TAB_MEDICINAL_PLANTS", 0},
        {"TAB_PHOTO",            1},
        {"TAB_BLUEPRINT",        2},
        {"TAB_ANIMAL",           4},
        {"TAB_CODENAMES",        5},
        {"TAB_KEY_ITEM",         7},
        {"TAB_POSTERS",          8},
        { nullptr, 0.0 }
    };
    static const ConstEntry V_TppCallSign[] = {
        {"NONE",     0},
        {"ZULU_1",   1},  {"ZOYA_1",     1},
        {"ZULU_4",   2},  {"ZOYA_4",     2},
        {"ZULU_7",   3},  {"ZOYA_7",     3},
        {"ZULU_2",   4},  {"ZOYA_2",     4},
        {"ZULU_6",   5},  {"ZOYA_6",     5},
        {"ZULU_10",  6},  {"ZOYA_10",    6},
        {"DELTA_2",  7},  {"DMITRY_2",   7},
        {"DELTA_6",  8},  {"DMITRY_6",   8},
        {"DELTA_9",  9},  {"DMITRY_9",   9},
        {"PATROL",  10},
        {"DELTA_1", 11},  {"DMITRY_1",  11},
        {"DELTA_3", 12},  {"DMITRY_3",  12},
        {"DELTA_4", 13},  {"DMITRY_4",  13},
        { nullptr, 0.0 }
    };
    static const ConstEntry V_ReceiverType[] = {
        {"AssaultQuickRefire",                  4},
        {"HandgunQuickRefire",                  5},
        {"HandgunCockEveryShot",                6},
        {"HandgunReloadOneRound",               10},
        {"HandgunLongGunStance",                11},
        {"HandgunRefireAfterAnimation",         15},
        {"MachineGunQuickRefire",               20},
        {"ShotgunPumpReloadOneRoundPlusOne",    21},
        {"ShotgunPumpReloadFullPlusOne",        22},
        {"ShotgunReloadOneRound",               23},
        {"ShotgunPumpReloadOneRound",           24},
        {"ShotgunReloadFull",                   26},
        {"SubMachineGunQuickRefire",            28},
        {"SniperQuickRefire",                   30},
        {"SniperBoltAction",                    32},
        {"SniperBoltActionFastCycle",           34},
        {"SniperQuickRefireTacticalReload",     37},
        {"GrenadeLauncherRefireAfterAnimation", 39},
        {"GrenadeLauncherQuickRefire",          40},
        {"GrenadeLauncherReloadOneRound",       42},
        {"MissileWideReloadAim",                43},
        { nullptr, 0.0 }
    };
    static const ConstEntry V_MagazineType[] = {
        {"HandgunNormal",             1},
        {"HandgunSpeedLoader",        2},
        {"AssaultNormal",             3},
        {"AssaultDualMagazine",       6},
        {"MachineGunNormal",          28},
        {"ShotgunNormal",             29},
        {"SubMachineGunNormal",       33},
        {"SubMachineGunDualMagazine", 35},
        {"SniperNormal",              37},
        {"GrenadeLauncherNormal",     41},
        { nullptr, 0.0 }
    };
    static const ConstEntry V_CallMenuItem[] = {
        {"KNOCK",            CallMenuItems::Knock},
        {"ACTIVE_SONAR",     CallMenuItems::ActiveSonar},
        {"BUDDY_1",          CallMenuItems::Buddy1},
        {"BUDDY_2",          CallMenuItems::Buddy2},
        {"BUDDY_3",          CallMenuItems::Buddy3},
        {"BUDDY_4",          CallMenuItems::Buddy4},
        {"REQUEST_INFO",     CallMenuItems::RequestInfo},
        {"SELL_OUT_COMRADE", CallMenuItems::SellOutComrade},
        {"REQUEST_CRAWL",    CallMenuItems::RequestCrawl},
        {"CALL_COMRADE",     CallMenuItems::CallComrade},
        {"RESCUE_BOY",       CallMenuItems::RescueBoy},
        {"FALSE_RADIO_RESPONSE", CallMenuItems::FalseRadioResponse},
        { nullptr, 0.0 }
    };
    static const ConstEntry V_CallMenuColumn[] = {
        {"KNOCK",         CallMenuItems::KnockColumn},
        {"BUDDY",         CallMenuItems::BuddyColumn},
        {"ACTIVE_SONAR",  CallMenuItems::SonarColumn},
        {"INTERROGATION", CallMenuItems::InterrogationColumn},
        { nullptr, 0.0 }
    };
    static const ConstEntry V_CallMenuCondition[] = {
        {"ALWAYS",      CallMenuItems::Always},
        {"HOLDING",     CallMenuItems::Holding},
        {"NOT_HOLDING", CallMenuItems::NotHolding},
        { nullptr, 0.0 }
    };
    static const ConstEntry V_CallMenuIcon[] = {
        {"KNOCK",         CallMenuItems::KnockIcon},
        {"INTERROGATION", CallMenuItems::InterrogationIcon},
        {"BUDDY",         CallMenuItems::BuddyIcon},
        {"SONAR",         CallMenuItems::SonarIcon},
        { nullptr, 0.0 }
    };

    static const ConstEntry V_NoticeKind[] = {   // the soldier's own notice types, named after the check that raises them
        {"DISCOVERED",          0x00},
        {"SUSPICIOUS_ON_GUARD", 0x05},
        {"SAW_HOSTILE_ANIMAL",  0x0A},
        {"SAW_FULTON",          0x14},
        {"SUSPICIOUS",          0x19},
        {"SAW_DECOY",           0x1B},
        {"SAW_SUPPLY_DROP",     0x24},
        {"HEARD_NOISE",         0x32},
        {"CALLED_OVER",         0x33},
        {"CALL_FOR_HELP",       0x34},
        {"SAW_ANIMAL",          0x37},
        { nullptr, 0.0 }
    };

    luaL_Reg g_NoFunctions[] = { { nullptr, nullptr } };

    void RegisterConstantTable(lua_State* L, const char* tableName, const ConstEntry* entries)
    {
        if (!L || !ResolveLuaApi())
            return;

        const int top = g_lua_gettop(L);
        RegisterLuaLibrary(L, tableName, g_NoFunctions);   // the global table is read-only while the game boots; only the library register lifts that, and it leaves the table on top
        if (g_lua_gettop(L) != top + 1 || g_lua_type(L, -1) != LUA_TTABLE)
        {
            g_lua_settop(L, top);
            LogError("LuaConstants: %s not registered, scripts that use it will fail", tableName);
            return;
        }

        for (const ConstEntry* e = entries; e->name; ++e)
        {
            g_lua_pushstring(L, const_cast<char*>(e->name));
            g_lua_pushnumber(L, e->value);
            g_lua_settable(L, -3);
        }
        g_lua_settop(L, top);
    }
}

void Register_V_TppGameObjectConstants(lua_State* L)
{
    RegisterConstantTable(L, "V_TppGameObject", V_TppGameObject);
}

void Register_V_TppMbDevConstants(lua_State* L)
{
    RegisterConstantTable(L, "V_TppMbDev", V_TppMbDev);
}

void Register_V_PlayerCqcStanceConstants(lua_State* L)
{
    RegisterConstantTable(L, "V_PlayerCqcStance", V_PlayerCqcStance);
}

bool IsListableDataBaseTab(std::int32_t tab)
{
    for (const auto& e : V_TppDataBase)
    {
        if (!e.name) break;
        if (static_cast<std::int32_t>(e.value) == tab)
            return true;
    }
    return false;
}

void Register_V_TppDataBaseConstants(lua_State* L)
{
    RegisterConstantTable(L, "V_TppDataBase", V_TppDataBase);
}

void Register_V_TppCallSignConstants(lua_State* L)
{
    RegisterConstantTable(L, "V_TppCallSign", V_TppCallSign);
}

void Register_V_ReceiverTypeConstants(lua_State* L)
{
    RegisterConstantTable(L, "V_ReceiverType", V_ReceiverType);
}

void Register_V_MagazineTypeConstants(lua_State* L)
{
    RegisterConstantTable(L, "V_MagazineType", V_MagazineType);
}

void Register_V_CallMenuConstants(lua_State* L)
{
    RegisterConstantTable(L, "V_CallMenuItem", V_CallMenuItem);
    RegisterConstantTable(L, "V_CallMenuColumn", V_CallMenuColumn);
    RegisterConstantTable(L, "V_CallMenuCondition", V_CallMenuCondition);
    RegisterConstantTable(L, "V_CallMenuIcon", V_CallMenuIcon);
}

void Register_V_NoticeConstants(lua_State* L)
{
    RegisterConstantTable(L, "V_NoticeKind", V_NoticeKind);
}
