#include <pch.h>

#include <Windows.h>
#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "HookUtils.h"
#include "LuaErrorThrow.h"
#include "log.h"
#include "../../lua/LuaApi.h"

namespace
{
    struct LuaErrorThrowSite
    {
        std::uintptr_t      addr;
        const std::uint8_t* head;
        std::size_t         headLen;
        const std::uint8_t* tail;
        std::size_t         tailLen;
    };

    constexpr std::size_t kCanaryDispLen = 4;

    const std::uint8_t kHead154[] = { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x60, 0x48, 0x8B, 0x05 };
    const std::uint8_t kTail154[] = {
        0x48, 0x33, 0xC4, 0x48, 0x89, 0x44, 0x24, 0x58, 0x45,
        0x33, 0xC0, 0x83, 0xCA, 0xFF, 0x48, 0x8B, 0xD9, 0xE8
    };

    LuaErrorThrowSite LuaErrorThrowSiteForBuild()
    {
        return { gAddr.Lua_PanicHandler, kHead154, sizeof(kHead154), kTail154, sizeof(kTail154) };
    }

    HookEntry CheckSiteEntry(const void* fn, const LuaErrorThrowSite& site)
    {
        std::uint8_t expected[64] = {};
        std::uint8_t mask[64] = {};
        std::memcpy(expected, site.head, site.headLen);
        std::memset(mask, 1, site.headLen);
        std::memcpy(expected + site.headLen + kCanaryDispLen, site.tail, site.tailLen);
        std::memset(mask + site.headLen + kCanaryDispLen, 1, site.tailLen);
        return CheckHookEntryMasked(fn, expected, mask, site.headLen + kCanaryDispLen + site.tailLen);
    }

    using LuaErrorThrow_t = void (__fastcall*)(lua_State* L);

    LuaErrorThrow_t g_OrigLuaErrorThrow = nullptr;
    void*           g_HookLuaErrorThrow = nullptr;

    const char* ReadLuaErrorMessageSEH(lua_State* L)
    {
        __try
        {
            return L ? GetLuaString(L, -1) : nullptr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    void __fastcall hkLuaErrorThrow(lua_State* L)
    {
        const char* msg = nullptr;
        if (L && ResolveLuaApi())
            msg = ReadLuaErrorMessageSEH(L);

        LogError("LuaError: %s", (msg && *msg) ? msg : "(message unreadable)");

        g_OrigLuaErrorThrow(L);
    }
}

bool Install_LuaErrorThrow()
{
    const LuaErrorThrowSite site = LuaErrorThrowSiteForBuild();
    void* target = ResolveGameAddress(site.addr);
    if (!target)
        return true;

    const HookEntry entry = CheckSiteEntry(target, site);
    if (entry == HookEntry::Unknown)
    {
        LogError("LuaError: panic handler 0x%llX bytes differ on %s, Lua errors will not be named",
            static_cast<unsigned long long>(site.addr), GetGameBuildName(gGameBuild));
        return true;
    }
    if (entry == HookEntry::ForeignHook)
        LogHookedOnTop("LuaError: panic handler", target);

    if (!CreateAndEnableHook(target,
                             reinterpret_cast<void*>(&hkLuaErrorThrow),
                             reinterpret_cast<void**>(&g_OrigLuaErrorThrow)))
    {
        LogError("LuaError: hook failed, Lua errors will not be named");
        return true;
    }

    g_HookLuaErrorThrow = target;
    return true;
}

void Uninstall_LuaErrorThrow()
{
    if (g_HookLuaErrorThrow)
    {
        DisableAndRemoveHook(g_HookLuaErrorThrow);
        g_HookLuaErrorThrow = nullptr;
    }
    g_OrigLuaErrorThrow = nullptr;
}
