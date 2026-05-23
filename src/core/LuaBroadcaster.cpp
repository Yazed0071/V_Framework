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

    static bool PushBroadcastTarget(lua_State* L, const LuaApi& lua)
    {
        lua.getfield(L, LUA_GLOBALSINDEX_51, const_cast<char*>("Mission"));
        if (lua.type(L, -1) != LUA_TTABLE)
            return false;

        lua.getfield(L, -1, const_cast<char*>("SendMessage"));
        if (lua.type(L, -1) != LUA_TFUNCTION)
            return false;

        return true;
    }

    static bool EnsureDispatchHelperInstalled(lua_State* L, const LuaApi& lua)
    {
        const int top0 = lua.gettop(L);

        lua.getfield(L, LUA_GLOBALSINDEX_51, const_cast<char*>("_V_FrameWork_Dispatch"));
        if (lua.type(L, -1) == LUA_TFUNCTION)
        {
            lua.settop(L, top0);
            return true;
        }
        lua.settop(L, top0);

        lua.getfield(L, LUA_GLOBALSINDEX_51, const_cast<char*>("loadstring"));
        if (lua.type(L, -1) != LUA_TFUNCTION)
        {
            Log("[V_FrameWork] install: loadstring missing (type=%d)\n",
                lua.type(L, -1));
            lua.settop(L, top0);
            return false;
        }

        static const char* kChunk =
            "return function(G, sender, msg, a0, a1, a2, a3)\n"
            "    local TppMain = G.TppMain\n"
            "    local debug = G.debug\n"
            "    local Tpp = G.Tpp\n"
            "    local TppMission = G.TppMission\n"
            "    if not TppMain or not debug or not debug.getupvalue then return end\n"
            "    local fn = TppMain.SetMessageFunction\n"
            "    if not fn then return end\n"
            "    local omt, omtSize, met, metSize\n"
            "    local i = 1\n"
            "    while true do\n"
            "        local name, value = debug.getupvalue(fn, i)\n"
            "        if name == nil then break end\n"
            "        if name == 'onMessageTable' then omt = value\n"
            "        elseif name == 'onMessageTableSize' then omtSize = value\n"
            "        elseif name == 'messageExecTable' then met = value\n"
            "        elseif name == 'messageExecTableSize' then metSize = value\n"
            "        end\n"
            "        i = i + 1\n"
            "    end\n"
            "    if omt and omtSize then\n"
            "        for j = 1, omtSize do\n"
            "            local cb = omt[j]\n"
            "            if type(cb) == 'function' then\n"
            "                pcall(cb, sender, msg, a0, a1, a2, a3, '')\n"
            "            end\n"
            "        end\n"
            "    end\n"
            "    if met and metSize and Tpp and Tpp.DoMessage then\n"
            "        local checkOpt = TppMission and TppMission.CheckMessageOption\n"
            "        for j = 1, metSize do\n"
            "            local et = met[j]\n"
            "            if type(et) == 'table' then\n"
            "                pcall(Tpp.DoMessage, et, checkOpt, sender, msg, a0, a1, a2, a3, '')\n"
            "            end\n"
            "        end\n"
            "    end\n"
            "end\n";

        lua.pushstring(L, const_cast<char*>(kChunk));

        int err = lua.pcall(L, 1, 2, 0);
        if (err != 0)
        {
            const char* errMsg = lua.tolstring(L, -1, nullptr);
            Log("[V_FrameWork] install: loadstring pcall err=%d: %s\n",
                err, errMsg ? errMsg : "<unknown>");
            lua.settop(L, top0);
            return false;
        }

        if (lua.type(L, -2) != LUA_TFUNCTION)
        {
            const char* errMsg = lua.tolstring(L, -1, nullptr);
            Log("[V_FrameWork] install: chunk compile failed: %s\n",
                errMsg ? errMsg : "<unknown>");
            lua.settop(L, top0);
            return false;
        }

        lua.settop(L, lua.gettop(L) - 1);

        err = lua.pcall(L, 0, 1, 0);
        if (err != 0)
        {
            const char* errMsg = lua.tolstring(L, -1, nullptr);
            Log("[V_FrameWork] install: chunk exec err=%d: %s\n",
                err, errMsg ? errMsg : "<unknown>");
            lua.settop(L, top0);
            return false;
        }

        if (lua.type(L, -1) != LUA_TFUNCTION)
        {
            Log("[V_FrameWork] install: chunk did not return a function (type=%d)\n",
                lua.type(L, -1));
            lua.settop(L, top0);
            return false;
        }

        const int fnIdx = lua.gettop(L);

        lua.pushstring(L, const_cast<char*>("_V_FrameWork_Dispatch"));
        lua.pushvalue(L, fnIdx);
        lua.rawset(L, LUA_GLOBALSINDEX_51);

        lua.settop(L, top0);

        lua.getfield(L, LUA_GLOBALSINDEX_51, const_cast<char*>("_V_FrameWork_Dispatch"));
        const bool installed = (lua.type(L, -1) == LUA_TFUNCTION);
        lua.settop(L, top0);

        if (!installed)
        {
            Log("[V_FrameWork] install: rawset verification failed; "
                "rawset(LUA_GLOBALSINDEX) did not stick\n");
        }

        return installed;
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

    const std::uint32_t senderHash = FoxHashes::StrCode32(category);
    const std::uint32_t msgHash    = FoxHashes::StrCode32(msg);

    __try
    {
        if (PushBroadcastTarget(L, lua))
        {
            PushRequiredBroadcastArgs(L, lua, category, msg);

            const int pushedOptionalArgs = PushOptionalArgs(L, lua, args, argCount);
            const int luaArgCount = 2 + pushedOptionalArgs;

            const int err = lua.pcall(L, luaArgCount, 0, 0);
            if (err != 0)
            {
                LogBroadcastError(lua, L, err, category, msg);
            }
        }
        lua.settop(L, savedTop);

        if (EnsureDispatchHelperInstalled(L, lua))
        {
            lua.getfield(L, LUA_GLOBALSINDEX_51, const_cast<char*>("_V_FrameWork_Dispatch"));
            if (lua.type(L, -1) == LUA_TFUNCTION)
            {
                lua.pushvalue(L, LUA_GLOBALSINDEX_51);
                lua.pushnumber(L, static_cast<lua_Number>(senderHash));
                lua.pushnumber(L, static_cast<lua_Number>(msgHash));

                for (int i = 0; i < 4; ++i)
                {
                    if (args && i < argCount)
                    {
                        PushOneOptionalArg(L, lua, args[i]);
                    }
                    else
                    {
                        lua.pushnil(L);
                    }
                }

                const int err = lua.pcall(L, 7, 0, 0);
                if (err != 0)
                {
                    const char* errMsg = lua.tolstring(L, -1, nullptr);
                    Log("[V_FrameWork] _V_FrameWork_Dispatch pcall err=%d "
                        "sender=0x%08x msg=0x%08x: %s\n",
                        err, senderHash, msgHash,
                        errMsg ? errMsg : "<no message>");
                }
            }
            else
            {
                lua.settop(L, lua.gettop(L) - 1);
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        Log("[V_FrameWork] BroadcastMessage SEH exception category=%s msg=%s\n",
            category,
            msg);
    }

    lua.settop(L, savedTop);
}
