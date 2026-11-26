#include <pch.h>

#include "BuddyServiceImpl_CanSortieBuddyType.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <intrin.h>

#include "AddressSet.h"
#include "BattleGearBuddy.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "log.h"
#include "../../lua/LuaApi.h"

namespace
{
    using CanSortieBuddyType_t    = bool(__fastcall*)(void* service, std::uint8_t buddyType);
    using CanSortieBuddyTypeLua_t = int(__cdecl*)(lua_State* L);

    constexpr int kLoggedGameQueries = 12;
    constexpr int kLoggedLuaQueries = 6;

    const std::uint8_t kCanSortieEntry[] = {
        0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x0F, 0xB6, 0xDA, 0x84, 0xD2, 0x75, 0x08, 0xB0, 0x01
    };
    const std::uint8_t kCanSortieLuaEntry[] = {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0xBA, 0x01, 0x00, 0x00, 0x00, 0x48,
        0x8B, 0xF9, 0x32, 0xDB
    };

    CanSortieBuddyType_t    g_Orig = nullptr;
    CanSortieBuddyTypeLua_t g_OrigLua = nullptr;
    void*                   g_Target = nullptr;
    void*                   g_LuaTarget = nullptr;
    std::atomic<int>        g_LoggedGame{ 0 };
    std::atomic<int>        g_LoggedLua{ 0 };

    bool __fastcall hkCanSortieBuddyType(void* service, std::uint8_t buddyType)
    {
        const bool saveBit = g_Orig(service, buddyType);
        if (buddyType != battleGear::kBuddyType)
            return saveBit;
        const bool answer = saveBit || battleGear::SortieUnlocked();
        if (g_LoggedGame.fetch_add(1, std::memory_order_relaxed) < kLoggedGameQueries)
        {
            const std::uintptr_t caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress()) - GetExeBase() + EXE_PREFERRED_BASE; // as Ghidra shows it
            LogDebug("BattleGear: game code at 0x%llX asked if Battle Gear can sortie in mission %u: save bit %d, answer %d",
                static_cast<unsigned long long>(caller), static_cast<unsigned>(MissionCodeGuard::GetCurrentMissionCode()),
                saveBit ? 1 : 0, answer ? 1 : 0);
        }
        return answer;
    }

    int __cdecl hkCanSortieBuddyTypeLua(lua_State* L)
    {
        const int results = g_OrigLua(L);
        if (results != 1 || !ResolveLuaApi() || !LuaIsNumber(L, 1) || GetLuaInt(L, 1) != battleGear::kBuddyType)
            return results;
        const bool saveBit = GetLuaBool(L, -1);
        const bool answer = saveBit || battleGear::SortieUnlocked();
        if (g_LoggedLua.fetch_add(1, std::memory_order_relaxed) < kLoggedLuaQueries)
            LogDebug("BattleGear: Lua asked if Battle Gear can sortie in mission %u: save bit %d, answer %d",
                static_cast<unsigned>(MissionCodeGuard::GetCurrentMissionCode()), saveBit ? 1 : 0, answer ? 1 : 0);
        if (answer != saveBit)
        {
            LuaPop(L, 1);
            PushLuaBool(L, answer);
        }
        return results;
    }

    bool HookOne(std::uintptr_t address, const std::uint8_t* entry, std::size_t entrySize, const char* name,
                 void* detour, void** original, void*& installed)
    {
        void* target = ResolveGameAddress(address);
        if (!target)
        {
            LogWarn("BattleGear: %s address missing on this build, Battle Gear sortie relies on the save bit", name);
            return false;
        }
        const HookEntry state = CheckHookEntry(target, entry, entrySize);
        if (state == HookEntry::Unknown)
        {
            char desc[160] = {};
            DescribeFunctionEntry(target, desc, sizeof(desc));
            LogWarn("BattleGear: %s code differs on this build (%s), Battle Gear sortie relies on the save bit", name, desc);
            return false;
        }
        if (state == HookEntry::ForeignHook)
        {
            char label[96] = {};
            std::snprintf(label, sizeof(label), "BattleGear: %s", name);
            LogHookedOnTop(label, target);
        }
        if (!CreateAndEnableHook(target, detour, original))
        {
            LogWarn("BattleGear: %s hook failed, Battle Gear sortie relies on the save bit", name);
            return false;
        }
        installed = target;
        return true;
    }
}

bool Install_BuddyServiceImpl_CanSortieBuddyType_Hooks()
{
    bool ok = true;
    if (!g_Target)
        ok &= HookOne(gAddr.BuddyServiceImpl_CanSortieBuddyType, kCanSortieEntry, sizeof(kCanSortieEntry),
                      "CanSortieBuddyType", reinterpret_cast<void*>(&hkCanSortieBuddyType),
                      reinterpret_cast<void**>(&g_Orig), g_Target);
    if (!g_LuaTarget)
        ok &= HookOne(gAddr.TppBuddyService_CanSortieBuddyTypeLua, kCanSortieLuaEntry, sizeof(kCanSortieLuaEntry),
                      "TppBuddyService.CanSortieBuddyType", reinterpret_cast<void*>(&hkCanSortieBuddyTypeLua),
                      reinterpret_cast<void**>(&g_OrigLua), g_LuaTarget);
    return ok;
}

void Uninstall_BuddyServiceImpl_CanSortieBuddyType_Hooks()
{
    if (g_LuaTarget)
    {
        DisableAndRemoveHook(g_LuaTarget);
        g_LuaTarget = nullptr;
        g_OrigLua = nullptr;
    }
    if (g_Target)
    {
        DisableAndRemoveHook(g_Target);
        g_Target = nullptr;
        g_Orig = nullptr;
    }
}
