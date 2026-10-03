#include "pch.h"

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
    const std::uint8_t kHead153[] = { 0x53, 0x48, 0x83, 0xEC, 0x60, 0x48, 0x8B, 0x05 };
    const std::uint8_t kTail153[] = {
        0x48, 0x31, 0xE0, 0x48, 0x89, 0x44, 0x24, 0x58, 0x45,
        0x31, 0xC0, 0x83, 0xCA, 0xFF, 0x48, 0x89, 0xCB, 0xE8
    };

    LuaErrorThrowSite LuaErrorThrowSiteForBuild()
    {
        switch (gGameBuild)
        {
        case ::AddressSetRuntime::GameBuild::En_1_0_15_4a:
        case ::AddressSetRuntime::GameBuild::En_1_0_15_4:
            return { 0x14006ACD0ull, kHead154, sizeof(kHead154), kTail154, sizeof(kTail154) };
        case ::AddressSetRuntime::GameBuild::Jp_1_0_15_4a:
        case ::AddressSetRuntime::GameBuild::Jp_1_0_15_4:
            return { 0x14006AD30ull, kHead154, sizeof(kHead154), kTail154, sizeof(kTail154) };
        case ::AddressSetRuntime::GameBuild::En_1_0_15_3:
            return { 0x14313D6E0ull, kHead153, sizeof(kHead153), kTail153, sizeof(kTail153) };
        case ::AddressSetRuntime::GameBuild::Jp_1_0_15_3:
            return { 0x1431C8F30ull, kHead153, sizeof(kHead153), kTail153, sizeof(kTail153) };
        default:
            return {};
        }
    }

    bool SiteBytesMatchSEH(const void* fn, const LuaErrorThrowSite& site)
    {
        __try
        {
            const auto* p = static_cast<const std::uint8_t*>(fn);
            return std::memcmp(p, site.head, site.headLen) == 0
                && std::memcmp(p + site.headLen + kCanaryDispLen, site.tail, site.tailLen) == 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
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

    if (!SiteBytesMatchSEH(target, site))
    {
        LogError("LuaError: panic handler 0x%llX bytes differ on %s, Lua errors will not be named",
            static_cast<unsigned long long>(site.addr), GetGameBuildName(gGameBuild));
        return true;
    }

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
