#include "pch.h"

extern "C" {
    #include "lua.h"
}

#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "HookUtils.h"
#include "FoxHashes.h"
#include "log.h"
#include "LuaBroadcaster.h"
#include "LuaStackGuard.h"
#include "GetGameObjectIdWithIndex.h"

namespace
{
    static constexpr std::uint32_t kInvalidGameObjectId = 0xFFFFu;
    static constexpr std::uint64_t kStringIdMask = 0x0000FFFFFFFFFFFFull;

    struct NativeGameObjectId
    {
        std::uint16_t value = 0xFFFF;
    };

    using GetGameObjectIdWithIndex_t =
        void(__fastcall*)(NativeGameObjectId* out,
            std::uint64_t typeNameId,
            std::uint32_t index);

    using GetGameObjectIdWithName_t =
        void(__fastcall*)(NativeGameObjectId* out,
            std::uint64_t typeNameId,
            std::uint64_t instanceNameId);

    static GetGameObjectIdWithIndex_t g_GetGameObjectIdWithIndex = nullptr;

    static bool IsTypeName(const char* lhs, const char* rhs)
    {
        if (!lhs || !rhs)
            return false;

        return std::strcmp(lhs, rhs) == 0;
    }

    static std::uint16_t LookupTypeIndex(const char* typeName)
    {
        if (IsTypeName(typeName, "TppSoldier2"))
            return TppGameObjectType::kSoldier2;

        return TppGameObjectType::kUnknown;
    }

    static std::uint32_t PackGameObjectId(std::uint16_t typeIndex,
        std::uint32_t index)
    {
        return (static_cast<std::uint32_t>(typeIndex) << 9) |
            (index & 0x1FFu);
    }

    using lua_getfield_t   = void(__fastcall*)(lua_State*, int, char*);
    using lua_type_t       = int(__fastcall*)(lua_State*, int);
    using lua_pushstring_t = void(__fastcall*)(lua_State*, char*);
    using lua_pcall_t      = int(__fastcall*)(lua_State*, int, int, int);
    using lua_tointeger_t  = std::intptr_t(__fastcall*)(lua_State*, int);
    using lua_gettop_t     = int(__fastcall*)(lua_State*);
    using lua_settop_t     = void(__fastcall*)(lua_State*, int);

    static constexpr int kLuaGlobalsIndex51 = -10002;

    template <typename Fn>
    static Fn ResolveLuaFn(std::uintptr_t resolvedAddr)
    {
        if (!resolvedAddr)
            return nullptr;
        return reinterpret_cast<Fn>(ResolveGameAddress(resolvedAddr));
    }

    static thread_local bool g_InByNameResolve = false;

    struct ByNameReentryGuard
    {
        bool entered;
        ByNameReentryGuard() : entered(!g_InByNameResolve)
        {
            if (entered)
                g_InByNameResolve = true;
        }
        ~ByNameReentryGuard()
        {
            if (entered)
                g_InByNameResolve = false;
        }
    };

    static bool ResolveByNameViaLua_Unguarded(const char* typeName,
        const char* instanceName,
        std::uint32_t& gameObjectIdOut)
    {
        if (GetCurrentThreadId() != V_FrameWork_LuaOwnerThreadId())
            return false;
        lua_State* L = V_FrameWork_AnyLuaState();
        if (!L)
            return false;

        auto getfield   = ResolveLuaFn<lua_getfield_t>(gAddr.lua_getfield);
        auto luatype    = ResolveLuaFn<lua_type_t>(gAddr.lua_type);
        auto pushstring = ResolveLuaFn<lua_pushstring_t>(gAddr.lua_pushstring);
        auto pcall      = ResolveLuaFn<lua_pcall_t>(gAddr.lua_pcall);
        auto tointeger  = ResolveLuaFn<lua_tointeger_t>(gAddr.lua_tointeger);
        auto gettop     = ResolveLuaFn<lua_gettop_t>(gAddr.lua_gettop);
        auto settop     = ResolveLuaFn<lua_settop_t>(gAddr.lua_settop);

        if (!getfield || !luatype || !pushstring || !pcall || !tointeger ||
            !gettop || !settop)
            return false;

        bool ok = false;

        __try
        {
            const int savedTop = gettop(L);

            if (!luaguard::HasStackRoom(L, 6))
                return false;

            getfield(L, kLuaGlobalsIndex51, const_cast<char*>("GameObject"));
            if (luatype(L, -1) != LUA_TTABLE)
            {
                settop(L, savedTop);
                return false;
            }

            getfield(L, -1, const_cast<char*>("GetGameObjectId"));
            if (luatype(L, -1) != LUA_TFUNCTION)
            {
                settop(L, savedTop);
                return false;
            }

            pushstring(L, const_cast<char*>(typeName));
            pushstring(L, const_cast<char*>(instanceName));

            const int err = pcall(L, 2, 1, 0);
            if (err == 0 && luatype(L, -1) == LUA_TNUMBER)
            {
                const std::uint32_t id =
                    static_cast<std::uint32_t>(tointeger(L, -1)) & 0x1FFFFu;
                if (id != 0xFFFFu)
                {
                    gameObjectIdOut = id;
                    ok = true;
                }
            }

            settop(L, savedTop);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            static bool s_logged = false;
            if (!s_logged)
            {
                s_logged = true;
                LogWarn("GetGameObjectIdWithIndex: fault in by-name resolve for %s '%s', not resolved",
                    typeName, instanceName);
            }
            return false;
        }

        return ok;
    }

    static bool TryResolveByNameViaLua(const char* typeName,
        const char* instanceName,
        std::uint32_t& gameObjectIdOut)
    {
        ByNameReentryGuard guard;
        if (!guard.entered)
            return false;

        return ResolveByNameViaLua_Unguarded(typeName, instanceName,
                                             gameObjectIdOut);
    }

    static bool TryNativeByName(const char* typeName,
        const char* instanceName,
        std::uint32_t& gameObjectIdOut,
        bool& nativeAnswered)
    {
        nativeAnswered = false;

        if (!gAddr.GameObject_GetGameObjectIdWithName)
            return false;

        auto fn = reinterpret_cast<GetGameObjectIdWithName_t>(
            ResolveGameAddress(gAddr.GameObject_GetGameObjectIdWithName));
        if (!fn)
            return false;

        const std::uint64_t typeNameId =
            FoxHashes::StrCode64(typeName) & kStringIdMask;
        const std::uint64_t instanceNameId =
            FoxHashes::StrCode64(instanceName) & kStringIdMask;
        NativeGameObjectId result{};

        __try
        {
            fn(&result, typeNameId, instanceNameId);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            static bool s_logged = false;
            if (!s_logged)
            {
                s_logged = true;
                LogWarn("GetGameObjectIdWithIndex: fault in native by-name resolve for %s '%s', using Lua",
                    typeName, instanceName);
            }
            return false;
        }

        nativeAnswered = true;

        if (result.value == kInvalidGameObjectId)
            return false;

        gameObjectIdOut = static_cast<std::uint32_t>(result.value);
        return true;
    }

    static bool TryNativeWithTypeNameId(const char* typeName,
        std::uint32_t index,
        std::uint32_t& gameObjectIdOut)
    {
        if (!g_GetGameObjectIdWithIndex)
            return false;

        const std::uint64_t typeNameId = FoxHashes::StrCode64(typeName) & kStringIdMask;
        NativeGameObjectId result{};

        __try
        {
            g_GetGameObjectIdWithIndex(&result, typeNameId, index);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            static bool s_logged = false;
            if (!s_logged)
            {
                s_logged = true;
                LogWarn("GetGameObjectIdWithIndex: fault resolving %s index %u", typeName, index);
            }
            return false;
        }

        if (result.value == 0xFFFFu)
            return false;

        gameObjectIdOut = static_cast<std::uint32_t>(result.value);
        return true;
    }
}

bool Install_GetGameObjectIdWithIndex()
{
    if (g_GetGameObjectIdWithIndex)
        return true;

    static bool s_failLogged = false;

    if (!gAddr.GetGameObjectIdWithIndex)
    {
        if (!s_failLogged)
        {
            s_failLogged = true;
            LogError("GetGameObjectIdWithIndex: address missing for this build, index lookups off");
        }
        return false;
    }

    void* target = ResolveGameAddress(gAddr.GetGameObjectIdWithIndex);
    if (!target)
    {
        if (!s_failLogged)
        {
            s_failLogged = true;
            LogError("GetGameObjectIdWithIndex: address resolve failed, index lookups off");
        }
        return false;
    }

    g_GetGameObjectIdWithIndex =
        reinterpret_cast<GetGameObjectIdWithIndex_t>(target);

    return true;
}

bool Uninstall_GetGameObjectIdWithIndex()
{
    g_GetGameObjectIdWithIndex = nullptr;
    return true;
}

bool Is_GetGameObjectIdWithIndex_Installed()
{
    return g_GetGameObjectIdWithIndex != nullptr;
}

bool GetGameObjectIdWithIndex(const char* typeName,
    std::uint32_t index,
    std::uint32_t& gameObjectIdOut)
{
    gameObjectIdOut = kInvalidGameObjectId;

    if (!typeName || !typeName[0])
        return false;

    const std::uint16_t typeIndex = LookupTypeIndex(typeName);
    if (typeIndex != TppGameObjectType::kUnknown)
    {
        gameObjectIdOut = PackGameObjectId(typeIndex, index);
        return true;
    }

    if (!g_GetGameObjectIdWithIndex)
        Install_GetGameObjectIdWithIndex();

    if (TryNativeWithTypeNameId(typeName, index, gameObjectIdOut))
        return true;

    return false;
}

bool GetSoldierGameObjectIdWithIndex(std::uint32_t soldierIndex,
    std::uint32_t& gameObjectIdOut)
{
    return GetGameObjectIdWithIndex("TppSoldier2",
        soldierIndex,
        gameObjectIdOut);
}

std::uint32_t GetGameObjectIdByIndex(const char* typeName,
    std::uint32_t index)
{
    std::uint32_t out = 0;
    return GetGameObjectIdWithIndex(typeName, index, out) ? out : 0;
}

bool GetGameObjectIdByName(const char* typeName,
    const char* instanceName,
    std::uint32_t& gameObjectIdOut)
{
    gameObjectIdOut = kInvalidGameObjectId;

    if (!typeName || !typeName[0] || !instanceName || !instanceName[0])
        return false;

    bool nativeAnswered = false;
    if (TryNativeByName(typeName, instanceName, gameObjectIdOut, nativeAnswered))
        return true;

    if (nativeAnswered)
        return false;

    return TryResolveByNameViaLua(typeName, instanceName, gameObjectIdOut);
}
