#include "pch.h"
#include "SetSupportWeaponTypeId.h"

#include <Windows.h>
#include <cstdint>
#include <mutex>
#include <unordered_map>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    using GetSupportWeaponTypeId_t = std::uint64_t(__fastcall*)(void* self, int equipId);

    SupportWeaponType::Deps g_Deps{};

    GetSupportWeaponTypeId_t g_OrigGetSupportWeaponTypeId = nullptr;
    bool g_GetSupportWeaponTypeIdHookInstalled = false;

    std::mutex g_SupportWeaponTypeMutex;
    std::unordered_map<std::int32_t, std::int32_t> g_CustomSupportWeaponTypes;
}

namespace
{
    static bool ValidateDeps()
    {
        return
            g_Deps.ResolveLuaApi &&
            g_Deps.LuaType &&
            g_Deps.GetLuaInt;
    }

    static bool EnsureLuaReady()
    {
        return ValidateDeps() && g_Deps.ResolveLuaApi && g_Deps.ResolveLuaApi();
    }

    static bool IsLuaNumber(lua_State* L, int idx)
    {
        return g_Deps.LuaType(L, idx) == 3;
    }

    static void SetSupportWeaponTypeInternal(std::int32_t equipId, std::int32_t swpType)
    {
        std::lock_guard<std::mutex> lock(g_SupportWeaponTypeMutex);
        g_CustomSupportWeaponTypes[equipId] = swpType;

        Log(
            "[SupportWeaponType] Set custom mapping equipId=0x%X (%d) -> swpType=0x%X (%d)\n",
            equipId,
            equipId,
            swpType,
            swpType
        );
    }

    static bool RemoveSupportWeaponTypeInternal(std::int32_t equipId)
    {
        std::lock_guard<std::mutex> lock(g_SupportWeaponTypeMutex);

        const auto it = g_CustomSupportWeaponTypes.find(equipId);
        if (it == g_CustomSupportWeaponTypes.end())
            return false;

        g_CustomSupportWeaponTypes.erase(it);

        Log(
            "[SupportWeaponType] Removed custom mapping equipId=0x%X (%d)\n",
            equipId,
            equipId
        );

        return true;
    }

    static void ClearSupportWeaponTypesInternal()
    {
        std::lock_guard<std::mutex> lock(g_SupportWeaponTypeMutex);
        g_CustomSupportWeaponTypes.clear();
        Log("[SupportWeaponType] Cleared all custom mappings\n");
    }

    static bool TryGetCustomSupportWeaponType(std::int32_t equipId, std::int32_t& outSwpType)
    {
        std::lock_guard<std::mutex> lock(g_SupportWeaponTypeMutex);

        const auto it = g_CustomSupportWeaponTypes.find(equipId);
        if (it == g_CustomSupportWeaponTypes.end())
            return false;

        outSwpType = it->second;
        return true;
    }

    static std::uint64_t __fastcall hkGetSupportWeaponTypeId(void* self, int equipId)
    {
        std::int32_t customSwpType = 0;
        if (TryGetCustomSupportWeaponType(static_cast<std::int32_t>(equipId), customSwpType))
        {
            return static_cast<std::uint64_t>(static_cast<std::uint32_t>(customSwpType));
        }

        if (g_OrigGetSupportWeaponTypeId)
            return g_OrigGetSupportWeaponTypeId(self, equipId);

        return 0x17;
    }
}

namespace SupportWeaponType
{
    void Bind(const Deps& deps)
    {
        g_Deps = deps;
    }

    int __cdecl Lua_SetSupportWeaponType(lua_State* L)
    {
        if (!L || !EnsureLuaReady())
            return 0;

        if (!IsLuaNumber(L, 1) || !IsLuaNumber(L, 2))
        {
            Log("[SupportWeaponType] Lua_SetSupportWeaponType: expected (number equipId, number swpType)\n");
            return 0;
        }

        const std::int32_t equipId = static_cast<std::int32_t>(g_Deps.GetLuaInt(L, 1));
        const std::int32_t swpType = static_cast<std::int32_t>(g_Deps.GetLuaInt(L, 2));

        SetSupportWeaponTypeInternal(equipId, swpType);
        return 0;
    }

    int __cdecl Lua_RemoveSupportWeaponType(lua_State* L)
    {
        if (!L || !EnsureLuaReady())
            return 0;

        if (!IsLuaNumber(L, 1))
        {
            Log("[SupportWeaponType] Lua_RemoveSupportWeaponType: expected (number equipId)\n");
            return 0;
        }

        const std::int32_t equipId = static_cast<std::int32_t>(g_Deps.GetLuaInt(L, 1));
        RemoveSupportWeaponTypeInternal(equipId);
        return 0;
    }

    int __cdecl Lua_ClearSupportWeaponTypes(lua_State* L)
    {
        UNREFERENCED_PARAMETER(L);
        ClearSupportWeaponTypesInternal();
        return 0;
    }

    bool Install_EquipIdTableImpl_GetSupportWeaponTypeId_Hook()
    {
        if (g_GetSupportWeaponTypeIdHookInstalled)
        {
            Log("[SupportWeaponType] GetSupportWeaponTypeId hook already installed\n");
            return true;
        }

        void* target = ResolveGameAddress(gAddr.EquipIdTableImpl_GetSupportWeaponTypeId);
        if (!target)
        {
            Log("[SupportWeaponType] Failed to resolve GetSupportWeaponTypeId target\n");
            return false;
        }

        const bool ok = CreateAndEnableHook(
            target,
            reinterpret_cast<void*>(&hkGetSupportWeaponTypeId),
            reinterpret_cast<void**>(&g_OrigGetSupportWeaponTypeId)
        );

        if (!ok)
        {
            Log("[SupportWeaponType] Failed to install GetSupportWeaponTypeId hook\n");
            return false;
        }

        g_GetSupportWeaponTypeIdHookInstalled = true;
        Log("[SupportWeaponType] GetSupportWeaponTypeId hook installed\n");
        return true;
    }

    bool Uninstall_EquipIdTableImpl_GetSupportWeaponTypeId_Hook()
    {
        if (!g_GetSupportWeaponTypeIdHookInstalled)
            return true;

        if (void* target = ResolveGameAddress(gAddr.EquipIdTableImpl_GetSupportWeaponTypeId))
        {
            DisableAndRemoveHook(target);
        }

        g_OrigGetSupportWeaponTypeId = nullptr;
        g_GetSupportWeaponTypeIdHookInstalled = false;
        return true;
    }
}