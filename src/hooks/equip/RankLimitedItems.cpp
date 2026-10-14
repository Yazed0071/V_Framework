#include "pch.h"
#include "RankLimitedItems.h"

#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "EquipAliasLease.h"
#include "EquipIdCompression.h"
#include "GoneEquipCleanup.h"
#include "LuaApi.h"
#include "MissionCodeGuard.h"
#include "log.h"

namespace
{
    constexpr std::size_t   kWeaponCells    = 0x548;
    constexpr std::size_t   kSupportCells   = 0x584;
    constexpr std::size_t   kItemCells      = 0x5A4;
    constexpr std::size_t   kHandEquipCell  = 0x5D4;
    constexpr int           kWeaponCount    = 6;
    constexpr int           kSlotCount      = 8;
    constexpr int           kCarriedCount   = kWeaponCount + kSlotCount + kSlotCount + 1;
    constexpr std::uint64_t kPumpIntervalMs = 1000;
    constexpr std::uint16_t kEquipIdMask    = 0x7FF;

    std::mutex                                     g_Mutex;
    std::unordered_map<std::int32_t, std::string>  g_Items;
    std::vector<std::int32_t>                      g_Used;
    std::atomic<bool>                              g_HasItems{ false };
    std::atomic<std::uint64_t>                     g_NextPump{ 0 };
    std::atomic<std::uint64_t>                     g_NextInstallCheck{ 0 };

    constexpr const char* kOriginalField = "_V_RegistUsedLimitedItemLangId";

    std::int32_t RealIdOfCell(std::uint16_t cell)
    {
        if (EquipIdCompression::IsWideEquipId(cell))
            return cell;
        return equip::FromEngine(cell & kEquipIdMask);
    }

    std::vector<std::string> UsedLangIds(bool& anyUsed)
    {
        std::vector<std::string> langIds;
        anyUsed = false;
        std::lock_guard<std::mutex> lock(g_Mutex);
        for (const std::int32_t equipId : g_Used)
        {
            const auto it = g_Items.find(equipId);
            if (it == g_Items.end())
                continue;
            anyUsed = true;
            if (!it->second.empty())
                langIds.push_back(it->second);
        }
        return langIds;
    }

    void ApplyRankLimit(lua_State* L)
    {
        bool anyUsed = false;
        const std::vector<std::string> langIds = UsedLangIds(anyUsed);
        if (!anyUsed)
            return;
        const int top = g_lua_gettop(L);

        g_lua_getfield(L, LUA_GLOBALSINDEX_51, const_cast<char*>("mvars"));
        if (g_lua_type(L, -1) == LUA_TTABLE)
        {
            g_lua_pushstring(L, const_cast<char*>("res_isUsedRankLimitedItem"));
            g_lua_pushboolean(L, 1);
            g_lua_rawset(L, -3);
        }
        g_lua_settop(L, top);
        if (langIds.empty())
            return;

        g_lua_getfield(L, LUA_GLOBALSINDEX_51, const_cast<char*>("TppUiCommand"));
        if (g_lua_type(L, -1) == LUA_TTABLE)
        {
            g_lua_getfield(L, -1, const_cast<char*>("SetResultScore"));
            if (g_lua_type(L, -1) == LUA_TFUNCTION)
            {
                g_lua_pushstring(L, const_cast<char*>(langIds.front().c_str()));
                g_lua_pushstring(L, const_cast<char*>("ranklimited"));
                if (g_lua_pcall(L, 2, 0, 0) != 0)
                {
                    const char* err = GetLuaString(L, -1);
                    LogWarn("TppResult.RegistUsedLimitedItemLangId: SetResultScore('%s', \"ranklimited\") failed, %s",
                        langIds.front().c_str(), err ? err : "unknown error");
                }
            }
        }
        g_lua_settop(L, top);
    }

    int __cdecl l_RegistUsedLimitedItemLangId(lua_State* L)
    {
        const int top = g_lua_gettop(L);
        g_lua_pushvalue(L, LUA_UPVALUEINDEX_51_1);
        for (int i = 1; i <= top; ++i)
            g_lua_pushvalue(L, i);
        if (g_lua_pcall(L, top, 0, 0) != 0)
        {
            const char* err = GetLuaString(L, -1);
            LogError("TppResult.RegistUsedLimitedItemLangId: %s", err ? err : "unknown error");
        }
        g_lua_settop(L, top);

        if (!MissionCodeGuard::ShouldBypassHooks())
            ApplyRankLimit(L);
        g_lua_settop(L, top);
        return 0;
    }

    bool ReadCarriedSafely(const std::uint8_t* vars, std::uint16_t (&carried)[kCarriedCount])
    {
        __try
        {
            const auto* weapons  = reinterpret_cast<const std::uint16_t*>(vars + kWeaponCells);
            const auto* supports = reinterpret_cast<const std::uint16_t*>(vars + kSupportCells);
            const auto* items    = reinterpret_cast<const std::uint16_t*>(vars + kItemCells);
            int n = 0;
            for (int i = 0; i < kWeaponCount; ++i)
                carried[n++] = weapons[i];
            for (int i = 0; i < kSlotCount; ++i)
                carried[n++] = supports[i];
            for (int i = 0; i < kSlotCount; ++i)
                carried[n++] = items[i];
            carried[n++] = *reinterpret_cast<const std::uint16_t*>(vars + kHandEquipCell);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }
}

namespace RankLimitedItems
{
    void Register(std::int32_t equipId, const std::string& resultLangId)
    {
        if (equipId <= 0)
            return;
        std::lock_guard<std::mutex> lock(g_Mutex);
        g_Items[equipId] = resultLangId;
        g_HasItems.store(true, std::memory_order_release);
    }

    void Unregister(std::int32_t equipId)
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        g_Items.erase(equipId);
        g_HasItems.store(!g_Items.empty(), std::memory_order_release);
    }

    void Pump()
    {
        if (!g_HasItems.load(std::memory_order_acquire))
            return;
        const std::uint64_t now = GetTickCount64();
        if (now < g_NextPump.load(std::memory_order_relaxed))
            return;
        g_NextPump.store(now + kPumpIntervalMs, std::memory_order_relaxed);
        if (MissionCodeGuard::ShouldBypassHooks())
            return;

        const std::uint8_t* vars = GoneEquipCleanup::ScriptVars();
        std::uint16_t carried[kCarriedCount] = {};
        if (!vars || !ReadCarriedSafely(vars, carried))
            return;

        std::lock_guard<std::mutex> lock(g_Mutex);
        for (const std::uint16_t cell : carried)
        {
            if (cell == 0)
                continue;
            const std::int32_t equipId = RealIdOfCell(cell);
            if (g_Items.count(equipId) != 0
                && std::find(g_Used.begin(), g_Used.end(), equipId) == g_Used.end())
                g_Used.push_back(equipId);
        }
    }

    void ClearForNewMission()
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        g_Used.clear();
    }

    void EnsureResultHookInstalled(lua_State* L)
    {
        if (!L || !g_HasItems.load(std::memory_order_acquire))
            return;
        const std::uint64_t now = GetTickCount64();
        if (now < g_NextInstallCheck.load(std::memory_order_relaxed))
            return;
        g_NextInstallCheck.store(now + kPumpIntervalMs, std::memory_order_relaxed);
        if (!ResolveLuaApi())
            return;

        const int top = g_lua_gettop(L);
        g_lua_getfield(L, LUA_GLOBALSINDEX_51, const_cast<char*>("TppResult"));
        if (g_lua_type(L, -1) != LUA_TTABLE)
        {
            g_lua_settop(L, top);
            return;
        }
        g_lua_getfield(L, -1, const_cast<char*>(kOriginalField));
        const bool installed = g_lua_type(L, -1) != LUA_TNIL;
        g_lua_settop(L, -2);
        if (installed)
        {
            g_lua_settop(L, top);
            return;
        }
        g_lua_getfield(L, -1, const_cast<char*>("RegistUsedLimitedItemLangId"));
        if (g_lua_type(L, -1) != LUA_TFUNCTION)
        {
            g_lua_settop(L, top);
            return;
        }

        g_lua_pushstring(L, const_cast<char*>(kOriginalField));
        g_lua_pushvalue(L, -2);
        g_lua_rawset(L, -4);

        g_lua_pushstring(L, const_cast<char*>("RegistUsedLimitedItemLangId"));
        g_lua_pushvalue(L, -2);
        g_lua_pushcclosure(L, &l_RegistUsedLimitedItemLangId, 1);
        g_lua_rawset(L, -4);

        g_lua_settop(L, top);
    }

    int __cdecl Lua_GetUsedRankLimitedItemLangIds(lua_State* L)
    {
        g_lua_createtable(L, 0, 0);
        if (MissionCodeGuard::ShouldBypassHooks())
            return 1;
        bool anyUsed = false;
        int index = 0;
        for (const std::string& langId : UsedLangIds(anyUsed))
        {
            g_lua_pushnumber(L, static_cast<lua_Number>(++index));
            g_lua_pushstring(L, const_cast<char*>(langId.c_str()));
            g_lua_settable(L, -3);
        }
        return 1;
    }
}
