#include "pch.h"

extern "C" {
    #include "lua.h"
}

#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "FoxHashes.h"
#include "HookUtils.h"
#include "log.h"
#include "LuaBroadcaster.h"

namespace
{
    using lua_pcall_t = int(__fastcall*)(lua_State*, int, int, int);
    using lua_pushnumber_t = void(__fastcall*)(lua_State*, lua_Number);
    using lua_pushstring_t = void(__fastcall*)(lua_State*, char*);
    using lua_pushboolean_t = void(__fastcall*)(lua_State*, int);
    using lua_pushnil_t = void(__fastcall*)(lua_State*);
    using lua_settop_t = void(__fastcall*)(lua_State*, int);
    using lua_gettop_t = int(__fastcall*)(lua_State*);
    using lua_getfield_t = void(__fastcall*)(lua_State*, int, char*);
    using lua_type_t = int(__fastcall*)(lua_State*, int);
    using lua_tolstring_t = const char* (__fastcall*)(lua_State*, int, size_t*);
    using lua_pushvalue_t = void (__fastcall*)(lua_State*, int);
    using lua_rawset_t    = void (__fastcall*)(lua_State*, int);

    static constexpr int LUA_GLOBALSINDEX_51 = -10002;

    static constexpr uintptr_t BOOTSTRAP_EN_lua_pcall = 0x141A11AB0ull;
    static constexpr uintptr_t BOOTSTRAP_EN_lua_pushnumber = 0x141A11BC0ull;
    static constexpr uintptr_t BOOTSTRAP_EN_lua_pushstring = 0x14C1E7EE0ull;
    static constexpr uintptr_t BOOTSTRAP_EN_lua_pushboolean = 0x14C1DB230ull;
    static constexpr uintptr_t BOOTSTRAP_EN_lua_pushnil = 0x14C1E7CC0ull;
    static constexpr uintptr_t BOOTSTRAP_EN_lua_gettop = 0x14C1D7D40ull;
    static constexpr uintptr_t BOOTSTRAP_EN_lua_settop = 0x14C1EBBE0ull;
    static constexpr uintptr_t BOOTSTRAP_EN_lua_getfield = 0x14C1D7320ull;
    static constexpr uintptr_t BOOTSTRAP_EN_lua_type = 0x14C1ED760ull;
    static constexpr uintptr_t BOOTSTRAP_EN_lua_tolstring = 0x141A123C0ull;
    static constexpr uintptr_t BOOTSTRAP_EN_lua_pushvalue = 0x14C1E87E0ull;
    static constexpr uintptr_t BOOTSTRAP_EN_lua_rawset    = 0x14C1E9CF0ull;

    static uintptr_t GetLuaBridgeAddress(uintptr_t resolvedAddr,
        uintptr_t bootstrapAddr)
    {
        return resolvedAddr ? resolvedAddr : bootstrapAddr;
    }

    template <typename Fn>
    static Fn ResolveLua(uintptr_t resolvedAddr, uintptr_t bootstrapAddr)
    {
        const uintptr_t addr = GetLuaBridgeAddress(resolvedAddr, bootstrapAddr);
        if (!addr)
            return nullptr;

        return reinterpret_cast<Fn>(ResolveGameAddress(addr));
    }

    struct LuaApi
    {
        lua_pcall_t       pcall = nullptr;
        lua_pushnumber_t  pushnumber = nullptr;
        lua_pushstring_t  pushstring = nullptr;
        lua_pushboolean_t pushboolean = nullptr;
        lua_pushnil_t     pushnil = nullptr;
        lua_settop_t      settop = nullptr;
        lua_gettop_t      gettop = nullptr;
        lua_getfield_t    getfield = nullptr;
        lua_type_t        type = nullptr;
        lua_tolstring_t   tolstring = nullptr;
        lua_pushvalue_t   pushvalue = nullptr;
        lua_rawset_t      rawset = nullptr;
    };

    static bool ResolveLuaApi(LuaApi& lua)
    {
        lua.pcall = ResolveLua<lua_pcall_t>(
            gAddr.lua_pcall,
            BOOTSTRAP_EN_lua_pcall);

        lua.pushnumber = ResolveLua<lua_pushnumber_t>(
            gAddr.lua_pushnumber,
            BOOTSTRAP_EN_lua_pushnumber);

        lua.pushstring = ResolveLua<lua_pushstring_t>(
            gAddr.lua_pushstring,
            BOOTSTRAP_EN_lua_pushstring);

        lua.pushboolean = ResolveLua<lua_pushboolean_t>(
            gAddr.lua_pushboolean,
            BOOTSTRAP_EN_lua_pushboolean);

        lua.pushnil = ResolveLua<lua_pushnil_t>(
            gAddr.lua_pushnil,
            BOOTSTRAP_EN_lua_pushnil);

        lua.settop = ResolveLua<lua_settop_t>(
            gAddr.lua_settop,
            BOOTSTRAP_EN_lua_settop);

        lua.gettop = ResolveLua<lua_gettop_t>(
            gAddr.lua_gettop,
            BOOTSTRAP_EN_lua_gettop);

        lua.getfield = ResolveLua<lua_getfield_t>(
            gAddr.lua_getfield,
            BOOTSTRAP_EN_lua_getfield);

        lua.type = ResolveLua<lua_type_t>(
            gAddr.lua_type,
            BOOTSTRAP_EN_lua_type);

        lua.tolstring = ResolveLua<lua_tolstring_t>(
            gAddr.lua_tolstring,
            BOOTSTRAP_EN_lua_tolstring);

        lua.pushvalue = ResolveLua<lua_pushvalue_t>(
            gAddr.lua_pushvalue,
            BOOTSTRAP_EN_lua_pushvalue);

        lua.rawset = ResolveLua<lua_rawset_t>(
            gAddr.lua_rawset,
            BOOTSTRAP_EN_lua_rawset);

        return lua.pcall &&
            lua.pushnumber &&
            lua.pushstring &&
            lua.pushboolean &&
            lua.pushnil &&
            lua.settop &&
            lua.gettop &&
            lua.getfield &&
            lua.type &&
            lua.pushvalue &&
            lua.rawset;
    }

    static int ClampBroadcastArgCount(int argCount)
    {
        if (argCount <= 0)
            return 0;

        if (argCount > V_FrameWork::kMaxBroadcastArgs)
            return V_FrameWork::kMaxBroadcastArgs;

        return argCount;
    }

    static bool PushTppMainOnMessage(lua_State* L, const LuaApi& lua, int top0)
    {
        lua.getfield(L, LUA_GLOBALSINDEX_51, const_cast<char*>("TppMain"));
        if (lua.type(L, -1) != LUA_TTABLE)
        {
            lua.settop(L, top0);
            return false;
        }

        lua.getfield(L, -1, const_cast<char*>("OnMessage"));
        if (lua.type(L, -1) != LUA_TFUNCTION)
        {
            lua.settop(L, top0);
            return false;
        }

        return true;
    }

    static volatile std::uint32_t* GetMessageResendCounter()
    {
        if (!gAddr.MessageResendCounter)
            return nullptr;

        return reinterpret_cast<volatile std::uint32_t*>(
            ResolveGameAddress(gAddr.MessageResendCounter));
    }

    static void PushRequiredBroadcastArgs(lua_State* L,
        const LuaApi& lua,
        const char* category,
        const char* msg)
    {
        lua.pushstring(L, const_cast<char*>(category));
        lua.pushstring(L, const_cast<char*>(msg));
    }

    static bool PushOneOptionalArg(lua_State* L,
        const LuaApi& lua,
        const V_FrameWork::LuaBroadcastArg& arg)
    {
        using Type = V_FrameWork::LuaBroadcastArg::Type;

        switch (arg.type)
        {
        case Type::Nil:
        {
            lua.pushnil(L);
            return true;
        }

        case Type::Boolean:
        {
            lua.pushboolean(L, arg.boolean ? 1 : 0);
            return true;
        }

        case Type::Number:
        {
            lua.pushnumber(L, static_cast<lua_Number>(arg.number));
            return true;
        }

        case Type::String:
        {
            lua.pushstring(L, const_cast<char*>(arg.stringValue.c_str()));
            return true;
        }
        }

        return false;
    }

    static int PushOptionalArgs(lua_State* L,
        const LuaApi& lua,
        const V_FrameWork::LuaBroadcastArg* args,
        int argCount)
    {
        if (!args || argCount <= 0)
            return 0;

        const int safeArgCount = ClampBroadcastArgCount(argCount);

        for (int i = 0; i < safeArgCount; ++i)
        {
            if (!PushOneOptionalArg(L, lua, args[i]))
                return i;
        }

        return safeArgCount;
    }

    static void LogBroadcastError(const LuaApi& lua,
        lua_State* L,
        int err,
        const char* category,
        const char* msg)
    {
        const char* errMsg = lua.tolstring ? lua.tolstring(L, -1, nullptr) : nullptr;

        Log("[V_FrameWork] Mission.SendMessage pcall err=%d category=%s msg=%s: %s\n",
            err,
            category,
            msg,
            errMsg ? errMsg : "<no message>");
    }

}

void V_FrameWork::EmitMessageValues(const char* category,
    const char* msg,
    const LuaBroadcastArg* args,
    int argCount)
{
    if (!category || !msg)
        return;

    lua_State* L = V_FrameWork_AnyLuaState();
    if (!L)
        return;

    LuaApi lua{};
    if (!ResolveLuaApi(lua))
        return;

    const int savedTop = lua.gettop(L);

    volatile std::uint32_t* counter = GetMessageResendCounter();
    const std::uint32_t     savedCounter = counter ? *counter : 0;
    if (counter) *counter = 0xFFFFu;

    DWORD  sehCode     = 0;
    PVOID  sehAddr     = nullptr;
    ULONG_PTR sehFault = 0;
    int    sehKind     = 0;

    __try
    {
        if (PushTppMainOnMessage(L, lua, savedTop))
        {
            const std::uint32_t senderHash = FoxHashes::StrCode32(category);
            const std::uint32_t msgHash    = FoxHashes::StrCode32(msg);

            lua.pushnil(L);
            lua.pushnumber(L, static_cast<lua_Number>(senderHash));
            lua.pushnumber(L, static_cast<lua_Number>(msgHash));

            for (int i = 0; i < 4; ++i)
            {
                if (args && i < argCount)
                    PushOneOptionalArg(L, lua, args[i]);
                else
                    lua.pushnil(L);
            }

            const int err = lua.pcall(L, 7, 0, 0);
            if (err != 0)
            {
                const char* errMsg = lua.tolstring(L, -1, nullptr);
                Log("[V_FrameWork] TppMain.OnMessage pcall err=%d category=%s msg=%s: %s\n",
                    err, category, msg, errMsg ? errMsg : "<no message>");
            }
        }
    }
    __except ((sehCode = GetExceptionCode(),
               sehAddr = GetExceptionInformation()->ExceptionRecord->ExceptionAddress,
               sehKind = (GetExceptionInformation()->ExceptionRecord->NumberParameters >= 2 ? 1 : 0),
               sehFault = (sehKind ? GetExceptionInformation()->ExceptionRecord->ExceptionInformation[1] : 0),
               EXCEPTION_EXECUTE_HANDLER))
    {
        if (sehKind)
        {
            Log("[V_FrameWork] BroadcastMessage SEH category=%s msg=%s code=0x%08X at=%p faulting=0x%llX\n",
                category, msg, sehCode, sehAddr,
                static_cast<unsigned long long>(sehFault));
        }
        else
        {
            Log("[V_FrameWork] BroadcastMessage SEH category=%s msg=%s code=0x%08X at=%p\n",
                category, msg, sehCode, sehAddr);
        }
    }

    if (counter) *counter = savedCounter;
    lua.settop(L, savedTop);
}
