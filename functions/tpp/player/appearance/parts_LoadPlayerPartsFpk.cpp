#include "pch.h"
#include "parts_LoadPlayerPartsFpk.h"

#include <Windows.h>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <mutex>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"
#include "FoxHashes.h"

namespace
{
    using LoadPlayerPartsFpk_t = std::uint64_t* (__fastcall*)(
        std::uint64_t* fileSlotIndex,
        std::uint32_t playerType,
        std::uint32_t playerPartsType);

    using FoxPathCtor_t = void* (__fastcall*)(void* pathObj, std::uint64_t pathCode64);

    struct LuaDeps
    {
        bool (*ResolveLuaApi)() = nullptr;
        int (*GetLuaTop)(lua_State* L) = nullptr;
        int (*LuaType)(lua_State* L, int idx) = nullptr;
        int (*GetLuaInt)(lua_State* L, int idx) = nullptr;
        const char* (*GetLuaString)(lua_State* L, int idx) = nullptr;
        void (*PushLuaNumber)(lua_State* L, float value) = nullptr;
    };

    struct SuitKey
    {
        std::uint32_t playerType;
        std::uint32_t playerPartsType;

        bool operator==(const SuitKey& other) const noexcept
        {
            return playerType == other.playerType &&
                playerPartsType == other.playerPartsType;
        }
    };

    struct SuitKeyHasher
    {
        std::size_t operator()(const SuitKey& key) const noexcept
        {
            const std::uint64_t packed =
                (static_cast<std::uint64_t>(key.playerType) << 32) |
                static_cast<std::uint64_t>(key.playerPartsType);
            return std::hash<std::uint64_t>{}(packed);
        }
    };

    struct CustomSuitEntry
    {
        std::string path;
        std::uint64_t pathCode64 = 0;
    };

    static LoadPlayerPartsFpk_t g_OrigLoadPlayerPartsFpk = nullptr;
    static bool g_HookInstalled = false;
    static LuaDeps g_LuaDeps{};

    static std::unordered_map<SuitKey, CustomSuitEntry, SuitKeyHasher> g_CustomSuits;
    static std::unordered_map<std::uint32_t, std::uint32_t> g_NextDynamicPartsTypeByPlayerType;
    static std::mutex g_CustomSuitsMutex;

    static constexpr std::uint32_t kDynamicPartsTypeStart = 100;

    static constexpr int LUA_TNUMBER_CONST = 3;
    static constexpr int LUA_TSTRING_CONST = 4;

    static bool ValidateLuaDeps()
    {
        return
            g_LuaDeps.ResolveLuaApi &&
            g_LuaDeps.GetLuaTop &&
            g_LuaDeps.LuaType &&
            g_LuaDeps.GetLuaInt &&
            g_LuaDeps.GetLuaString &&
            g_LuaDeps.PushLuaNumber;
    }

    static bool EnsureLuaReady()
    {
        return ValidateLuaDeps() && g_LuaDeps.ResolveLuaApi();
    }

    static bool LuaIsNumber(lua_State* L, int idx)
    {
        return g_LuaDeps.LuaType(L, idx) == LUA_TNUMBER_CONST;
    }

    static bool LuaIsString(lua_State* L, int idx)
    {
        return g_LuaDeps.LuaType(L, idx) == LUA_TSTRING_CONST;
    }

    static FoxPathCtor_t ResolveFoxPathCtor()
    {
        return reinterpret_cast<FoxPathCtor_t>(ResolveGameAddress(gAddr.FoxPath_Path));
    }

    static std::uint32_t AllocateDynamicPartsType_NoLock(std::uint32_t playerType)
    {
        std::uint32_t& nextValue = g_NextDynamicPartsTypeByPlayerType[playerType];
        if (nextValue < kDynamicPartsTypeStart)
            nextValue = kDynamicPartsTypeStart;

        while (true)
        {
            const SuitKey key{ playerType, nextValue };
            if (g_CustomSuits.find(key) == g_CustomSuits.end())
                return nextValue++;

            ++nextValue;
        }
    }

    static bool RegisterCustomSuit(
        std::uint32_t playerType,
        const char* path,
        std::uint32_t* outAssignedPartsType,
        bool useExplicitPartsType,
        std::uint32_t explicitPartsType)
    {
        if (!path || !path[0] || !outAssignedPartsType)
            return false;

        const std::string normalizedPath = FoxHashes::NormalizeAssetPath(path);
        const std::uint64_t pathCode64 = FoxHashes::PathCode64Ext(normalizedPath);
        if (pathCode64 == 0)
        {
            Log("[PlayerPartsFpkHook] Path hash failed for '%s'\n", path);
            return false;
        }

        std::lock_guard<std::mutex> lock(g_CustomSuitsMutex);

        std::uint32_t assignedPartsType = explicitPartsType;
        if (!useExplicitPartsType)
            assignedPartsType = AllocateDynamicPartsType_NoLock(playerType);

        const SuitKey key{ playerType, assignedPartsType };
        g_CustomSuits[key] = CustomSuitEntry{ normalizedPath, pathCode64 };
        *outAssignedPartsType = assignedPartsType;

        Log("[PlayerPartsFpkHook] Registered custom suit: playerType=%u partsType=%u path=%s\n",
            playerType, assignedPartsType, normalizedPath.c_str());

        return true;
    }

    static bool RemoveCustomSuit(std::uint32_t playerType, std::uint32_t playerPartsType)
    {
        std::lock_guard<std::mutex> lock(g_CustomSuitsMutex);

        const SuitKey key{ playerType, playerPartsType };
        const auto erased = g_CustomSuits.erase(key);

        if (erased != 0)
        {
            Log("[PlayerPartsFpkHook] Removed custom suit: playerType=%u partsType=%u\n",
                playerType, playerPartsType);
            return true;
        }

        return false;
    }

    static void ClearCustomSuits()
    {
        std::lock_guard<std::mutex> lock(g_CustomSuitsMutex);
        g_CustomSuits.clear();
        g_NextDynamicPartsTypeByPlayerType.clear();
        Log("[PlayerPartsFpkHook] Cleared all custom suits\n");
    }

    static bool TryGetCustomSuit(
        std::uint32_t playerType,
        std::uint32_t playerPartsType,
        CustomSuitEntry& outEntry)
    {
        std::lock_guard<std::mutex> lock(g_CustomSuitsMutex);

        const SuitKey key{ playerType, playerPartsType };
        const auto it = g_CustomSuits.find(key);
        if (it == g_CustomSuits.end())
            return false;

        outEntry = it->second;
        return true;
    }

    static std::uint64_t* __fastcall hkLoadPlayerPartsFpk(
        std::uint64_t* fileSlotIndex,
        std::uint32_t playerType,
        std::uint32_t playerPartsType)
    {
        CustomSuitEntry entry{};
        if (TryGetCustomSuit(playerType, playerPartsType, entry))
        {
            FoxPathCtor_t foxPathCtor = ResolveFoxPathCtor();
            if (!foxPathCtor)
            {
                Log("[PlayerPartsFpkHook] FoxPath::Path unresolved, fallback\n");
                return g_OrigLoadPlayerPartsFpk
                    ? g_OrigLoadPlayerPartsFpk(fileSlotIndex, playerType, playerPartsType)
                    : fileSlotIndex;
            }

            foxPathCtor(fileSlotIndex, entry.pathCode64);

            Log("[PlayerPartsFpkHook] Custom suit loaded: playerType=%u partsType=%u path=%s\n",
                playerType, playerPartsType, entry.path.c_str());

            return fileSlotIndex;
        }

        return g_OrigLoadPlayerPartsFpk
            ? g_OrigLoadPlayerPartsFpk(fileSlotIndex, playerType, playerPartsType)
            : fileSlotIndex;
    }
}

namespace PlayerPartsFpkHook
{
    void BindLua(const LuaBindings& bindings)
    {
        g_LuaDeps.ResolveLuaApi = bindings.ResolveLuaApi;
        g_LuaDeps.GetLuaTop = bindings.GetLuaTop;
        g_LuaDeps.LuaType = bindings.LuaType;
        g_LuaDeps.GetLuaInt = bindings.GetLuaInt;
        g_LuaDeps.GetLuaString = bindings.GetLuaString;
        g_LuaDeps.PushLuaNumber = bindings.PushLuaNumber;
    }

    int __cdecl Lua_RegisterCustomPlayerPartsFpk(lua_State* L)
    {
        // RegisterCustomPlayerPartsFpk(playerType, path)
        // RegisterCustomPlayerPartsFpk(playerType, path, explicitPartsType)

        if (!L || !EnsureLuaReady())
            return 0;

        const int argCount = g_LuaDeps.GetLuaTop(L);
        if (argCount < 2)
            return 0;

        if (!LuaIsNumber(L, 1) || !LuaIsString(L, 2))
            return 0;

        const std::uint32_t playerType =
            static_cast<std::uint32_t>(g_LuaDeps.GetLuaInt(L, 1));

        const char* path = g_LuaDeps.GetLuaString(L, 2);

        bool useExplicitPartsType = false;
        std::uint32_t explicitPartsType = 0;

        if (argCount >= 3 && LuaIsNumber(L, 3))
        {
            useExplicitPartsType = true;
            explicitPartsType =
                static_cast<std::uint32_t>(g_LuaDeps.GetLuaInt(L, 3));
        }

        std::uint32_t assignedPartsType = 0;
        const bool ok = RegisterCustomSuit(
            playerType,
            path,
            &assignedPartsType,
            useExplicitPartsType,
            explicitPartsType);

        if (!ok)
            return 0;

        g_LuaDeps.PushLuaNumber(L, static_cast<float>(assignedPartsType));
        return 1;
    }

    int __cdecl Lua_RemoveCustomPlayerPartsFpk(lua_State* L)
    {
        if (!L || !EnsureLuaReady())
            return 0;

        if (!LuaIsNumber(L, 1) || !LuaIsNumber(L, 2))
            return 0;

        const std::uint32_t playerType =
            static_cast<std::uint32_t>(g_LuaDeps.GetLuaInt(L, 1));
        const std::uint32_t playerPartsType =
            static_cast<std::uint32_t>(g_LuaDeps.GetLuaInt(L, 2));

        RemoveCustomSuit(playerType, playerPartsType);
        return 0;
    }

    int __cdecl Lua_ClearCustomPlayerPartsFpk(lua_State* L)
    {
        UNREFERENCED_PARAMETER(L);
        ClearCustomSuits();
        return 0;
    }

    bool Install()
    {
        if (g_HookInstalled)
            return true;

        void* target = ResolveGameAddress(gAddr.LoadPlayerPartsFpk);
        if (!target)
        {
            Log("[PlayerPartsFpkHook] Failed to resolve LoadPlayerPartsFpk\n");
            return false;
        }

        const bool ok = CreateAndEnableHook(
            target,
            reinterpret_cast<void*>(&hkLoadPlayerPartsFpk),
            reinterpret_cast<void**>(&g_OrigLoadPlayerPartsFpk));

        if (!ok)
        {
            Log("[PlayerPartsFpkHook] Hook install failed\n");
            return false;
        }

        g_HookInstalled = true;
        Log("[PlayerPartsFpkHook] Hook installed OK\n");
        return true;
    }

    bool Uninstall()
    {
        if (!g_HookInstalled)
            return true;

        if (void* target = ResolveGameAddress(gAddr.LoadPlayerPartsFpk))
            DisableAndRemoveHook(target);

        g_OrigLoadPlayerPartsFpk = nullptr;
        g_HookInstalled = false;
        return true;
    }
}