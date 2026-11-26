#include <pch.h>
#include "TppVarInit_InitializeForNewMission.h"

extern "C" {
    #include "lua.h"
}

#include <Windows.h>
#include <atomic>
#include <cstdint>

#include "log.h"
#include "MissionCodeGuard.h"
#include "CustomHeadRegistry.h"
#include "OutfitRegistry.h"
#include "../../lua/LuaApi.h"

namespace
{
    constexpr const char* kOriginalInitializeField = "_V_InitializeForNewMission";
    constexpr ULONGLONG   kWrapCheckIntervalMs     = 1000;

    std::atomic<bool>      g_Installed{ false };
    std::atomic<ULONGLONG> g_NextWrapCheck{ 0 };
    std::atomic<bool>      g_WrapBusy{ false };

    bool AbilitiesAllowedIn(std::int32_t missionCode)
    {
        if (missionCode <= 0)
            return false;
        const std::int32_t series = missionCode / 10000;
        const bool hardMission = missionCode / 1000 - series * 10 == 1; // TppMission.IsHardMission: Extreme, Subsistence, Total Stealth
        return series != 4 && series != 5 && !hardMission              // helicopter space, FOB
            && missionCode != 10010 && missionCode != 10240 && missionCode != 10280;
    }

    std::int32_t ReadGlobalNumber(lua_State* L, const char* table, const char* field)
    {
        const int top = g_lua_gettop(L);
        std::int32_t result = -1;
        g_lua_getfield(L, LUA_GLOBALSINDEX_51, const_cast<char*>(table));
        if (g_lua_type(L, -1) == LUA_TTABLE || g_lua_type(L, -1) == LUA_TUSERDATA)
        {
            g_lua_getfield(L, -1, const_cast<char*>(field));
            if (g_lua_type(L, -1) == LUA_TNUMBER)
                result = static_cast<std::int32_t>(g_lua_tonumber(L, -1));
        }
        g_lua_settop(L, top);
        return result;
    }

    bool CallLuaFunction(lua_State* L, const char* library, const char* function, const bool* argument)
    {
        const int top = g_lua_gettop(L);
        bool ok = false;
        g_lua_getfield(L, LUA_GLOBALSINDEX_51, const_cast<char*>(library));
        if (g_lua_type(L, -1) == LUA_TTABLE)
        {
            g_lua_getfield(L, -1, const_cast<char*>(function));
            if (g_lua_type(L, -1) == LUA_TFUNCTION)
            {
                if (argument)
                    g_lua_pushboolean(L, *argument ? 1 : 0);
                ok = g_lua_pcall(L, argument ? 1 : 0, 0, 0) == 0;
                if (!ok)
                {
                    const char* err = GetLuaString(L, -1);
                    LogError("HeadAbilities: %s.%s: %s", library, function, err ? err : "unknown error");
                }
            }
        }
        g_lua_settop(L, top);
        return ok;
    }

    int __cdecl l_SetChickCapEnabled(lua_State* L)
    {
        g_lua_getfield(L, LUA_GLOBALSINDEX_51, const_cast<char*>("svars"));
        g_lua_pushstring(L, const_cast<char*>("chickCapEnabled"));
        g_lua_pushboolean(L, 1);
        g_lua_settable(L, -3);
        return 0;
    }

    bool SetChickCapEnabled(lua_State* L)
    {
        const int top = g_lua_gettop(L);
        g_lua_pushcclosure(L, &l_SetChickCapEnabled, 0);
        const bool ok = g_lua_pcall(L, 0, 0, 0) == 0;
        if (!ok)
        {
            const char* err = GetLuaString(L, -1);
            LogError("HeadAbilities: svars.chickCapEnabled: %s", err ? err : "unknown error");
        }
        g_lua_settop(L, top);
        return ok;
    }

    void ApplyWornHeadAbilities(lua_State* L)
    {
        if (MissionCodeGuard::ShouldBypassHooks())
            return;
        const std::uint16_t worn = outfit::ReadLiveWornHeadCategory();
        if (worn == 0xFFFF)
            return;
        const outfit::CustomHeadEntry* head = outfit::TryGetCustomHeadBySlot(static_cast<std::uint8_t>(worn & 0xFF));
        if (!head)
            return;
        const bool stealthAssist = outfit::GetCustomHeadStealthAssist(head->name);
        const bool chickCap = outfit::GetCustomHeadChickCap(head->name);
        if (!stealthAssist && !chickCap)
            return;

        const std::int32_t missionCode = ReadGlobalNumber(L, "vars", "missionCode");
        const std::int32_t playerType  = ReadGlobalNumber(L, "vars", "playerType");
        if (!AbilitiesAllowedIn(missionCode) || playerType < 0 || playerType > outfit::kPlayerType_Avatar
            || !outfit::IsHeadOptionOfferedByLiveSuit(head->equipId))
            return;

        const bool enable = true;
        if (stealthAssist)
        {
            const bool on = CallLuaFunction(L, "GameConfig", "SetStealthAssistEnabled", &enable)
                && CallLuaFunction(L, "TppPlayer", "SetRetryFlag", nullptr);
            if (!on)
                LogWarn("HeadAbilities: '%s' stealthAssist not turned on for mission %d", head->name, missionCode);
        }
        if (chickCap)
        {
            const bool on = SetChickCapEnabled(L)
                && CallLuaFunction(L, "TppPlayer", "SetRetryFlagWithChickCap", nullptr);
            if (!on)
                LogWarn("HeadAbilities: '%s' chickCap not turned on for mission %d", head->name, missionCode);
        }
    }

    bool CallOriginal(lua_State* L, int argumentCount, const char* name)
    {
        g_lua_pushvalue(L, LUA_UPVALUEINDEX_51_1);
        for (int i = 1; i <= argumentCount; ++i)
            g_lua_pushvalue(L, i);
        if (g_lua_pcall(L, argumentCount, LUA_MULTRET, 0) == 0)
            return true;
        const char* err = GetLuaString(L, -1);
        LogError("%s: %s", name, err ? err : "unknown error");
        g_lua_settop(L, argumentCount);
        return false;
    }

    int __cdecl l_InitializeForNewMission(lua_State* L)
    {
        const int top = g_lua_gettop(L);
        if (!CallOriginal(L, top, "TppVarInit.InitializeForNewMission"))
            return 0;
        const int results = g_lua_gettop(L);
        if (g_Installed.load(std::memory_order_acquire))
            ApplyWornHeadAbilities(L);
        g_lua_settop(L, results);
        return results - top;
    }

    void WrapLuaFunction(lua_State* L, const char* library, const char* function,
                         const char* originalField, lua_CFunction wrapper)
    {
        const int top = g_lua_gettop(L);
        g_lua_getfield(L, LUA_GLOBALSINDEX_51, const_cast<char*>(library));
        if (g_lua_type(L, -1) != LUA_TTABLE)
        {
            g_lua_settop(L, top);
            return;
        }
        const int libraryTable = g_lua_gettop(L);
        g_lua_getfield(L, libraryTable, const_cast<char*>(originalField));
        const bool wrapped = g_lua_type(L, -1) != LUA_TNIL;
        g_lua_settop(L, libraryTable);
        if (wrapped)
        {
            g_lua_settop(L, top);
            return;
        }

        g_lua_getfield(L, libraryTable, const_cast<char*>(function));
        if (g_lua_type(L, -1) != LUA_TFUNCTION)
        {
            g_lua_settop(L, top);
            return;
        }
        const int original = g_lua_gettop(L);

        g_lua_pushstring(L, const_cast<char*>(originalField));
        g_lua_pushvalue(L, original);
        g_lua_rawset(L, libraryTable);

        g_lua_pushstring(L, const_cast<char*>(function));
        g_lua_pushvalue(L, original);
        g_lua_pushcclosure(L, wrapper, 1);
        g_lua_rawset(L, libraryTable);

        g_lua_settop(L, top);
    }
}

namespace outfit::headabilities
{
    void EnsureLuaWrapsInstalled(lua_State* L)
    {
        if (!L || !g_Installed.load(std::memory_order_acquire))
            return;
        const ULONGLONG now = GetTickCount64();
        if (now < g_NextWrapCheck.load(std::memory_order_relaxed))
            return;
        if (g_WrapBusy.exchange(true))
            return;
        g_NextWrapCheck.store(now + kWrapCheckIntervalMs, std::memory_order_relaxed);
        if (ResolveLuaApi())
            WrapLuaFunction(L, "TppVarInit", "InitializeForNewMission", kOriginalInitializeField, &l_InitializeForNewMission);
        g_WrapBusy.store(false);
    }
}

bool Install_TppVarInit_InitializeForNewMission_Hook()
{
    g_Installed.store(true, std::memory_order_release);
    return true;
}

bool Uninstall_TppVarInit_InitializeForNewMission_Hook()
{
    g_Installed.store(false, std::memory_order_release);
    return true;
}
