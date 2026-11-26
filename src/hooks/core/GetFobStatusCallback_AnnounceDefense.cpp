#include <pch.h>
#include "GetFobStatusCallback_AnnounceDefense.h"

#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "HookUtils.h"
#include "LuaBroadcaster.h"
#include "log.h"
#include "MissionCodeGuard.h"
#include "../../lua/LuaApi.h"

namespace
{
    using AnnounceDefense_t = void (__fastcall*)(void* callback);

    constexpr std::size_t kAnnounceEntrySize = 62;
    constexpr std::uint8_t kAnnounceWonEntry[kAnnounceEntrySize] = {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x48, 0x89, 0x7C, 0x24, 0x18,
        0x4C, 0x89, 0x74, 0x24, 0x20, 0x41, 0x57, 0x48, 0x81, 0xEC, 0x40, 0x01, 0x00, 0x00,
        0x48, 0x8B, 0x05, 0x00, 0x00, 0x00, 0x00, 0x48, 0x33, 0xC4, 0x48, 0x89, 0x84, 0x24, 0x30, 0x01, 0x00, 0x00,
        0xE8, 0x00, 0x00, 0x00, 0x00,
        0x48, 0xBB, 0xA7, 0x36, 0x13, 0x65, 0x93, 0x03, 0x00, 0x00,   // mov rbx, lang id of the won-defense announce (real name unknown)
    };
    constexpr std::uint8_t kAnnounceLostEntry[kAnnounceEntrySize] = {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x48, 0x89, 0x7C, 0x24, 0x18,
        0x4C, 0x89, 0x74, 0x24, 0x20, 0x41, 0x57, 0x48, 0x81, 0xEC, 0x40, 0x01, 0x00, 0x00,
        0x48, 0x8B, 0x05, 0x00, 0x00, 0x00, 0x00, 0x48, 0x33, 0xC4, 0x48, 0x89, 0x84, 0x24, 0x30, 0x01, 0x00, 0x00,
        0xE8, 0x00, 0x00, 0x00, 0x00,
        0x48, 0xBB, 0x1A, 0x5E, 0x42, 0x80, 0x12, 0x14, 0x00, 0x00,   // mov rbx, lang id of the lost-defense announce (real name unknown)
    };
    constexpr std::uint8_t kAnnounceEntryMask[kAnnounceEntrySize] = {
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1, 0, 0, 0, 0,
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    };

    constexpr const char* kIntrudersRadio      = "f2000_rtrg1030";
    constexpr const char* kBackupRequestRadio  = "f2000_rtrg1040";
    constexpr const char* kDefenseWonRadio     = "f2000_rtrg1060";
    constexpr const char* kDefenseLostRadio    = "f2000_rtrg1070";
    constexpr const char* kIntrudersNotice     = "fobNoticeIntruder";
    constexpr const char* kBackupRequestNotice = "fobReqHelp";
    constexpr const char* kOriginalShowNotice  = "_V_ShowNoticeFobSneaked";
    constexpr ULONGLONG   kNoticeWrapCheckMs   = 1000;
    constexpr ULONGLONG   kResultCheckMs       = 500;
    constexpr ULONGLONG   kSameNoticeGapMs     = 120000;
    constexpr int         kPumpLuaSlots        = 8;
    constexpr int         kDefenseWon          = 1;
    constexpr int         kDefenseLost         = 2;

    std::atomic<bool> g_Installed{ false };
    AnnounceDefense_t g_OrigAnnounceWon    = nullptr;
    AnnounceDefense_t g_OrigAnnounceLost   = nullptr;
    void*             g_AnnounceWonTarget  = nullptr;
    void*             g_AnnounceLostTarget = nullptr;
    std::atomic<int>  g_PendingResults{ 0 };
    std::atomic<bool> g_IntrudersCalledIn{ false };
    std::atomic<bool> g_WentToFob{ false };
    ULONGLONG         g_NextNoticeWrapCheck = 0;
    ULONGLONG         g_NextResultCheck     = 0;
    ULONGLONG         g_IntrudersRadioMs    = 0;
    ULONGLONG         g_BackupRequestRadioMs = 0;

    void __fastcall hkAnnounceDefenseWon(void* callback)
    {
        g_OrigAnnounceWon(callback);
        if (!MissionCodeGuard::ShouldBypassHooks())
            g_PendingResults.fetch_or(kDefenseWon, std::memory_order_relaxed);
    }

    void __fastcall hkAnnounceDefenseLost(void* callback)
    {
        g_OrigAnnounceLost(callback);
        if (!MissionCodeGuard::ShouldBypassHooks())
            g_PendingResults.fetch_or(kDefenseLost, std::memory_order_relaxed);
    }

    void PlayRadio(lua_State* L, const char* group, const char* delay)
    {
        const int top = g_lua_gettop(L);
        g_lua_getfield(L, LUA_GLOBALSINDEX_51, const_cast<char*>("TppRadio"));
        if (g_lua_type(L, -1) == LUA_TTABLE)
        {
            g_lua_getfield(L, -1, const_cast<char*>("Play"));
            if (g_lua_type(L, -1) == LUA_TFUNCTION)
            {
                g_lua_pushstring(L, const_cast<char*>(group));
                g_lua_createtable(L, 0, 2);
                g_lua_pushstring(L, const_cast<char*>("delayTime"));
                g_lua_pushstring(L, const_cast<char*>(delay));
                g_lua_rawset(L, -3);
                g_lua_pushstring(L, const_cast<char*>("isEnqueue"));
                g_lua_pushboolean(L, 1);
                g_lua_rawset(L, -3);
                V_FrameWork::EnterLuaPcall();
                const int err = g_lua_pcall(L, 2, 0, 0);
                V_FrameWork::AbandonLuaPcall();
                if (err != 0)
                {
                    const char* text = GetLuaString(L, -1);
                    LogError("FobRadio: TppRadio.Play %s: %s", group, text ? text : "unknown error");
                }
            }
        }
        g_lua_settop(L, top);
    }

    bool CallBooleanFunction(lua_State* L, const char* table, const char* function, bool* result)
    {
        const int top = g_lua_gettop(L);
        bool called = false;
        g_lua_getfield(L, LUA_GLOBALSINDEX_51, const_cast<char*>(table));
        if (g_lua_type(L, -1) == LUA_TTABLE)
        {
            g_lua_getfield(L, -1, const_cast<char*>(function));
            if (g_lua_type(L, -1) == LUA_TFUNCTION)
            {
                V_FrameWork::EnterLuaPcall();
                called = g_lua_pcall(L, 0, 1, 0) == 0;
                V_FrameWork::AbandonLuaPcall();
                if (called)
                    *result = g_lua_toboolean(L, -1) != 0;
            }
        }
        g_lua_settop(L, top);
        return called;
    }

    bool OcelotCanCall(lua_State* L)
    {
        bool missionReady = false;
        if (!CallBooleanFunction(L, "TppSequence", "IsMissionPrepareFinished", &missionReady) || !missionReady)
            return false;
        bool noticesOff = true;
        if (!CallBooleanFunction(L, "TppTerminal", "IsDisableNoticeFobSneaked", &noticesOff))
            return false;
        return !noticesOff;
    }

    int __cdecl l_ShowNoticeFobSneaked(lua_State* L)
    {
        const int argumentCount = g_lua_gettop(L);
        g_lua_pushvalue(L, LUA_UPVALUEINDEX_51_1);
        for (int i = 1; i <= argumentCount; ++i)
            g_lua_pushvalue(L, i);
        if (g_lua_pcall(L, argumentCount, 0, 0) != 0)
        {
            const char* err = GetLuaString(L, -1);
            LogError("TppTerminal.ShowNoticeFobSneaked: %s", err ? err : "unknown error");
            g_lua_settop(L, argumentCount);
        }
        if (!g_Installed.load(std::memory_order_relaxed) || MissionCodeGuard::ShouldBypassHooks())
            return 0;
        if (argumentCount < 1 || g_lua_type(L, 1) != LUA_TSTRING)
            return 0;
        const char* notice = GetLuaString(L, 1);
        if (!notice)
            return 0;
        const ULONGLONG now = GetTickCount64();
        if (std::strcmp(notice, kIntrudersNotice) == 0)
        {
            g_IntrudersCalledIn.store(true, std::memory_order_relaxed);
            g_WentToFob.store(false, std::memory_order_relaxed);
            if (now - g_IntrudersRadioMs >= kSameNoticeGapMs)
            {
                g_IntrudersRadioMs = now;
                PlayRadio(L, kIntrudersRadio, "mid");
            }
        }
        else if (std::strcmp(notice, kBackupRequestNotice) == 0)
        {
            if (now - g_BackupRequestRadioMs >= kSameNoticeGapMs)
            {
                g_BackupRequestRadioMs = now;
                PlayRadio(L, kBackupRequestRadio, "mid");
            }
        }
        return 0;
    }

    void EnsureNoticeWrapInstalled(lua_State* L)
    {
        const int top = g_lua_gettop(L);
        g_lua_getfield(L, LUA_GLOBALSINDEX_51, const_cast<char*>("TppTerminal"));
        if (g_lua_type(L, -1) != LUA_TTABLE)
        {
            g_lua_settop(L, top);
            return;
        }
        const int terminal = g_lua_gettop(L);
        g_lua_getfield(L, terminal, const_cast<char*>(kOriginalShowNotice));
        const bool wrapped = g_lua_type(L, -1) != LUA_TNIL;
        g_lua_settop(L, terminal);
        if (!wrapped)
        {
            g_lua_getfield(L, terminal, const_cast<char*>("ShowNoticeFobSneaked"));
            if (g_lua_type(L, -1) == LUA_TFUNCTION)
            {
                const int original = g_lua_gettop(L);
                g_lua_pushstring(L, const_cast<char*>(kOriginalShowNotice));
                g_lua_pushvalue(L, original);
                g_lua_rawset(L, terminal);
                g_lua_pushstring(L, const_cast<char*>("ShowNoticeFobSneaked"));
                g_lua_pushvalue(L, original);
                g_lua_pushcclosure(L, &l_ShowNoticeFobSneaked, 1);
                g_lua_rawset(L, terminal);
            }
        }
        g_lua_settop(L, top);
    }

    bool HookAnnounce(std::uintptr_t address, const std::uint8_t* entry, void* detour, void** original, void** target,
                      const char* what)
    {
        void* fn = ResolveGameAddress(address);
        const HookEntry state = CheckHookEntryMasked(fn, entry, kAnnounceEntryMask, kAnnounceEntrySize);
        if (state == HookEntry::Unknown)
        {
            char bytes[160] = {};
            DescribeFunctionEntry(fn, bytes, sizeof(bytes));
            LogError("FobRadio: the %s differs on %s (%s), Ocelot stays quiet about it", what,
                     GetGameBuildName(gGameBuild), bytes);
            return false;
        }
        if (state == HookEntry::ForeignHook)
            LogHookedOnTop(what, fn);
        if (!CreateAndEnableHook(fn, detour, original))
        {
            LogError("FobRadio: the %s hook failed at %p, Ocelot stays quiet about it", what, fn);
            *original = nullptr;
            return false;
        }
        *target = fn;
        return true;
    }
}

void FobRadio_Pump()
{
    if (!g_Installed.load(std::memory_order_relaxed))
        return;
    if (MissionCodeGuard::ShouldBypassHooks())
    {
        if (g_IntrudersCalledIn.load(std::memory_order_relaxed))
            g_WentToFob.store(true, std::memory_order_relaxed);
        return;
    }
    const ULONGLONG now = GetTickCount64();
    const bool checkWrap = now >= g_NextNoticeWrapCheck;
    const bool checkResult = now >= g_NextResultCheck && g_PendingResults.load(std::memory_order_relaxed) != 0;
    if (!checkWrap && !checkResult)
        return;
    if (!ResolveLuaApi())
        return;
    lua_State* L = V_FrameWork_AnyLuaState();
    if (!L || !V_FrameWork::LuaStackHasRoom(L, kPumpLuaSlots))
        return;

    if (checkWrap)
    {
        g_NextNoticeWrapCheck = now + kNoticeWrapCheckMs;
        EnsureNoticeWrapInstalled(L);
    }
    if (!checkResult)
        return;
    g_NextResultCheck = now + kResultCheckMs;
    if (!OcelotCanCall(L))
        return;

    const int results = g_PendingResults.exchange(0, std::memory_order_relaxed);
    const bool playerDefended = g_WentToFob.exchange(false, std::memory_order_relaxed);
    g_IntrudersCalledIn.store(false, std::memory_order_relaxed);
    if (playerDefended)
    {
        return;
    }
    if (results & kDefenseWon)
        PlayRadio(L, kDefenseWonRadio, "long");
    if (results & kDefenseLost)
        PlayRadio(L, kDefenseLostRadio, "long");
}

bool Install_GetFobStatusCallback_AnnounceDefense_Hooks()
{
    if (g_Installed.load(std::memory_order_relaxed))
        return true;
    if (!gAddr.GetFobStatusCallback_AnnounceDefenseWon || !gAddr.GetFobStatusCallback_AnnounceDefenseLost)
    {
        LogWarn("FobRadio: FOB defense announces unknown for %s, Ocelot does not report FOB defenses you skipped",
                GetGameBuildName(gGameBuild));
    }
    else
    {
        HookAnnounce(gAddr.GetFobStatusCallback_AnnounceDefenseWon, kAnnounceWonEntry,
                     reinterpret_cast<void*>(&hkAnnounceDefenseWon), reinterpret_cast<void**>(&g_OrigAnnounceWon),
                     &g_AnnounceWonTarget, "won FOB defense announce");
        HookAnnounce(gAddr.GetFobStatusCallback_AnnounceDefenseLost, kAnnounceLostEntry,
                     reinterpret_cast<void*>(&hkAnnounceDefenseLost), reinterpret_cast<void**>(&g_OrigAnnounceLost),
                     &g_AnnounceLostTarget, "lost FOB defense announce");
    }
    g_Installed.store(true, std::memory_order_relaxed);
    return true;
}

void Uninstall_GetFobStatusCallback_AnnounceDefense_Hooks()
{
    g_Installed.store(false, std::memory_order_relaxed);
    if (g_AnnounceWonTarget)
        DisableAndRemoveHook(g_AnnounceWonTarget);
    if (g_AnnounceLostTarget)
        DisableAndRemoveHook(g_AnnounceLostTarget);
    g_AnnounceWonTarget  = nullptr;
    g_AnnounceLostTarget = nullptr;
    g_OrigAnnounceWon    = nullptr;
    g_OrigAnnounceLost   = nullptr;
    g_PendingResults.store(0, std::memory_order_relaxed);
    g_IntrudersCalledIn.store(false, std::memory_order_relaxed);
    g_WentToFob.store(false, std::memory_order_relaxed);
}
