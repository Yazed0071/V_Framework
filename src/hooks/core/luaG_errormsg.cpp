#include <pch.h>
#include "luaG_errormsg.h"

#include <Windows.h>
#include <intrin.h>
#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"

struct lua_State;

namespace
{
    using ErrorMsg_t = void(__fastcall*)(lua_State* L);

    constexpr std::size_t kLuaErrorCallReturn = 9;   // lua_error is sub rsp,28h then call luaG_errormsg

    const std::uint8_t kPrologue[] = { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9 };
    const std::uint8_t kAfterLuaErrorCall[] = { 0x33, 0xC0, 0x48, 0x83, 0xC4, 0x28, 0xC3 };

    ErrorMsg_t     g_Orig = nullptr;
    void*          g_Target = nullptr;
    std::uintptr_t g_GameBase = 0;
    std::uintptr_t g_GameSize = 0;
    std::uintptr_t g_LuaErrorReturn = 0;

    void __fastcall hkErrorMsg(lua_State* L)
    {
        auto* returnSlot = static_cast<std::uintptr_t*>(_AddressOfReturnAddress());
        if (*returnSlot - g_GameBase >= g_GameSize)
            *returnSlot = g_LuaErrorReturn;   // a hook on lua_error moved this call into a trampoline with no unwind data; longjmp's stack walk reads junk there and can close the game
        g_Orig(L);
    }

    bool ReadGameRange()
    {
        const auto* base = reinterpret_cast<const std::uint8_t*>(GetModuleHandleW(nullptr));
        if (!base)
            return false;
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        g_GameBase = reinterpret_cast<std::uintptr_t>(base);
        g_GameSize = nt->OptionalHeader.SizeOfImage;
        return true;
    }
}

bool Install_luaG_errormsg_Hook()
{
    if (!gAddr.luaG_errormsg || !gAddr.lua_error)
    {
        LogError("LuaError: address missing for this build, a Lua error can still close the game");
        return false;
    }

    void* fn = ResolveGameAddress(gAddr.luaG_errormsg);
    const HookEntry entry = CheckHookEntry(fn, kPrologue, sizeof(kPrologue));
    if (entry == HookEntry::Unknown)
    {
        char found[160] = {};
        DescribeFunctionEntry(fn, found, sizeof(found));
        LogError("LuaError: luaG_errormsg at 0x%llX is not the expected code (%s), a Lua error can still close the game",
                 static_cast<unsigned long long>(gAddr.luaG_errormsg), found);
        return false;
    }

    auto* luaErrorReturn = static_cast<std::uint8_t*>(ResolveGameAddress(gAddr.lua_error)) + kLuaErrorCallReturn;
    if (CheckHookEntry(luaErrorReturn, kAfterLuaErrorCall, sizeof(kAfterLuaErrorCall)) != HookEntry::Expected)
    {
        LogError("LuaError: lua_error at 0x%llX is not the expected code, a Lua error can still close the game",
                 static_cast<unsigned long long>(gAddr.lua_error));
        return false;
    }

    if (!ReadGameRange())
    {
        LogError("LuaError: could not read the game's image range, a Lua error can still close the game");
        return false;
    }
    g_LuaErrorReturn = reinterpret_cast<std::uintptr_t>(luaErrorReturn);

    if (!CreateAndEnableHook(fn, reinterpret_cast<void*>(&hkErrorMsg), reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("LuaError: luaG_errormsg hook failed, a Lua error can still close the game");
        return false;
    }
    if (entry == HookEntry::ForeignHook)
        LogHookedOnTop("LuaError: luaG_errormsg", fn);
    g_Target = fn;
    return true;
}

void Uninstall_luaG_errormsg_Hook()
{
    if (g_Target)
    {
        DisableAndRemoveHook(g_Target);
        g_Target = nullptr;
    }
    g_Orig = nullptr;
}
