#include "pch.h"
#include "parts_LoadPlayerCamoFpk.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

#include "AddressSet.h"
#include "FoxHashes.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    using LoadPlayerCamoFpk_t = std::uint64_t* (__fastcall*)(
        std::uint64_t* fileSlotIndex,
        std::uint32_t playerType,
        std::uint32_t playerPartsType,
        std::uint32_t playerCamoType);

    using FoxPathCtor_t = void* (__fastcall*)(void* pathObj, std::uint64_t pathCode64);

    static LoadPlayerCamoFpk_t g_OrigLoadPlayerCamoFpk = nullptr;
    static bool g_HookInstalled = false;
    static PlayerCamoFpkHook::LuaBindings g_Lua{};

    static constexpr int LUA_TNONE_CONST = -1;
    static constexpr int LUA_TNIL_CONST = 0;
    static constexpr int LUA_TNUMBER_CONST = 3;
    static constexpr int LUA_TSTRING_CONST = 4;

    struct CamoKey
    {
        std::uint32_t playerType;
        std::uint32_t playerPartsType;
        std::int32_t playerCamoType; // allow -1 wildcard

        bool operator==(const CamoKey& other) const noexcept
        {
            return playerType == other.playerType &&
                playerPartsType == other.playerPartsType &&
                playerCamoType == other.playerCamoType;
        }
    };

    struct CamoKeyHasher
    {
        std::size_t operator()(const CamoKey& key) const noexcept
        {
            const std::uint64_t a = static_cast<std::uint64_t>(key.playerType);
            const std::uint64_t b = static_cast<std::uint64_t>(key.playerPartsType);
            const std::uint64_t c = static_cast<std::uint32_t>(key.playerCamoType);
            return std::hash<std::uint64_t>{}((a << 48) ^ (b << 16) ^ c);
        }
    };

    struct CustomCamoEntry
    {
        std::string path;
        std::uint64_t pathCode64 = 0;
    };

    static std::unordered_map<CamoKey, CustomCamoEntry, CamoKeyHasher> g_CustomCamos;
    static std::mutex g_CustomCamosMutex;

    static bool LuaReady()
    {
        return g_Lua.ResolveLuaApi && g_Lua.ResolveLuaApi();
    }

    static bool LuaIsNumber(lua_State* L, int idx)
    {
        return g_Lua.LuaType && g_Lua.LuaType(L, idx) == LUA_TNUMBER_CONST;
    }

    static bool LuaIsString(lua_State* L, int idx)
    {
        return g_Lua.LuaType && g_Lua.LuaType(L, idx) == LUA_TSTRING_CONST;
    }

    static FoxPathCtor_t ResolveFoxPathCtor()
    {
        return reinterpret_cast<FoxPathCtor_t>(ResolveGameAddress(gAddr.FoxPath_Path));
    }

    static bool RegisterCustomCamo(
        std::uint32_t playerType,
        std::uint32_t playerPartsType,
        std::int32_t playerCamoType,
        const char* path)
    {
        if (!path || !path[0])
            return false;

        const std::uint64_t pathCode64 = FoxHashes::PathCode64Ext(path);
        if (pathCode64 == 0)
        {
            Log("[PlayerCamoFpkHook] Path hash failed for '%s'\n", path);
            return false;
        }

        std::lock_guard<std::mutex> lock(g_CustomCamosMutex);

        const CamoKey key{ playerType, playerPartsType, playerCamoType };
        g_CustomCamos[key] = CustomCamoEntry{ path, pathCode64 };

        Log("[PlayerCamoFpkHook] Registered custom camo: playerType=%u partsType=%u camoType=%d path=%s\n",
            playerType, playerPartsType, playerCamoType, path);

        return true;
    }

    static bool RemoveCustomCamo(
        std::uint32_t playerType,
        std::uint32_t playerPartsType,
        std::int32_t playerCamoType)
    {
        std::lock_guard<std::mutex> lock(g_CustomCamosMutex);

        const CamoKey key{ playerType, playerPartsType, playerCamoType };
        const auto erased = g_CustomCamos.erase(key);

        if (erased != 0)
        {
            Log("[PlayerCamoFpkHook] Removed custom camo: playerType=%u partsType=%u camoType=%d\n",
                playerType, playerPartsType, playerCamoType);
            return true;
        }

        return false;
    }

    static void ClearCustomCamos()
    {
        std::lock_guard<std::mutex> lock(g_CustomCamosMutex);
        g_CustomCamos.clear();
        Log("[PlayerCamoFpkHook] Cleared all custom camos\n");
    }

    static bool TryGetCustomCamo(
        std::uint32_t playerType,
        std::uint32_t playerPartsType,
        std::uint32_t playerCamoType,
        CustomCamoEntry& outEntry)
    {
        std::lock_guard<std::mutex> lock(g_CustomCamosMutex);

        {
            const CamoKey exactKey{ playerType, playerPartsType, static_cast<std::int32_t>(playerCamoType) };
            const auto it = g_CustomCamos.find(exactKey);
            if (it != g_CustomCamos.end())
            {
                outEntry = it->second;
                return true;
            }
        }

        {
            const CamoKey wildcardKey{ playerType, playerPartsType, -1 };
            const auto it = g_CustomCamos.find(wildcardKey);
            if (it != g_CustomCamos.end())
            {
                outEntry = it->second;
                return true;
            }
        }

        return false;
    }

    static std::uint64_t* __fastcall hkLoadPlayerCamoFpk(
        std::uint64_t* fileSlotIndex,
        std::uint32_t playerType,
        std::uint32_t playerPartsType,
        std::uint32_t playerCamoType)
    {
        CustomCamoEntry entry{};
        if (TryGetCustomCamo(playerType, playerPartsType, playerCamoType, entry))
        {
            FoxPathCtor_t foxPathCtor = ResolveFoxPathCtor();
            if (!foxPathCtor)
            {
                Log("[PlayerCamoFpkHook] FoxPath::Path unresolved, fallback\n");
                return g_OrigLoadPlayerCamoFpk
                    ? g_OrigLoadPlayerCamoFpk(fileSlotIndex, playerType, playerPartsType, playerCamoType)
                    : fileSlotIndex;
            }

            foxPathCtor(fileSlotIndex, entry.pathCode64);

            Log("[PlayerCamoFpkHook] Custom camo loaded: playerType=%u partsType=%u camoType=%u path=%s\n",
                playerType, playerPartsType, playerCamoType, entry.path.c_str());

            return fileSlotIndex;
        }

        return g_OrigLoadPlayerCamoFpk
            ? g_OrigLoadPlayerCamoFpk(fileSlotIndex, playerType, playerPartsType, playerCamoType)
            : fileSlotIndex;
    }
}

namespace PlayerCamoFpkHook
{
    void BindLua(const LuaBindings& bindings)
    {
        g_Lua = bindings;
    }

    int __cdecl Lua_RegisterCustomPlayerCamoFpk(lua_State* L)
    {
        if (!L || !LuaReady())
            return 0;

        if (!g_Lua.LuaType || !g_Lua.GetLuaInt || !g_Lua.GetLuaString)
            return 0;

        if (!LuaIsNumber(L, 1) ||
            !LuaIsNumber(L, 2) ||
            !LuaIsNumber(L, 3) ||
            !LuaIsString(L, 4))
        {
            return 0;
        }

        const std::uint32_t playerType =
            static_cast<std::uint32_t>(g_Lua.GetLuaInt(L, 1));
        const std::uint32_t playerPartsType =
            static_cast<std::uint32_t>(g_Lua.GetLuaInt(L, 2));
        const std::int32_t playerCamoType =
            static_cast<std::int32_t>(g_Lua.GetLuaInt(L, 3));
        const char* path = g_Lua.GetLuaString(L, 4);

        RegisterCustomCamo(playerType, playerPartsType, playerCamoType, path);
        return 0;
    }

    int __cdecl Lua_RemoveCustomPlayerCamoFpk(lua_State* L)
    {
        if (!L || !LuaReady())
            return 0;

        if (!g_Lua.LuaType || !g_Lua.GetLuaInt)
            return 0;

        if (!LuaIsNumber(L, 1) ||
            !LuaIsNumber(L, 2) ||
            !LuaIsNumber(L, 3))
        {
            return 0;
        }

        const std::uint32_t playerType =
            static_cast<std::uint32_t>(g_Lua.GetLuaInt(L, 1));
        const std::uint32_t playerPartsType =
            static_cast<std::uint32_t>(g_Lua.GetLuaInt(L, 2));
        const std::int32_t playerCamoType =
            static_cast<std::int32_t>(g_Lua.GetLuaInt(L, 3));

        RemoveCustomCamo(playerType, playerPartsType, playerCamoType);
        return 0;
    }

    int __cdecl Lua_ClearCustomPlayerCamoFpk(lua_State* L)
    {
        UNREFERENCED_PARAMETER(L);
        ClearCustomCamos();
        return 0;
    }

    bool Install()
    {
        if (g_HookInstalled)
            return true;

        void* target = ResolveGameAddress(gAddr.LoadPlayerCamoFpk);
        if (!target)
        {
            Log("[PlayerCamoFpkHook] Failed to resolve LoadPlayerCamoFpk\n");
            return false;
        }

        const bool ok = CreateAndEnableHook(
            target,
            reinterpret_cast<void*>(&hkLoadPlayerCamoFpk),
            reinterpret_cast<void**>(&g_OrigLoadPlayerCamoFpk));

        if (!ok)
        {
            Log("[PlayerCamoFpkHook] Hook install failed\n");
            return false;
        }

        g_HookInstalled = true;
        Log("[PlayerCamoFpkHook] Hook installed OK\n");
        return true;
    }

    bool Uninstall()
    {
        if (!g_HookInstalled)
            return true;

        void* target = ResolveGameAddress(gAddr.LoadPlayerCamoFpk);
        if (target)
            DisableAndRemoveHook(target);

        g_OrigLoadPlayerCamoFpk = nullptr;
        g_HookInstalled = false;
        return true;
    }
}