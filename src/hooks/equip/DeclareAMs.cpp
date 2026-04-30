#define NOMINMAX
#include "pch.h"
#include "DeclareAMs.h"

#include <Windows.h>
#include <algorithm>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    using DeclareAMs_t = void(__fastcall*)(lua_State* L);

    struct AmmoEnumEntry
    {
        std::string name;
        std::int32_t ammoId = 0;
    };

    DeclareAMs::Deps g_Deps{};
    DeclareAMs_t g_OrigDeclareAMs = nullptr;
    bool g_DeclareAMsHookInstalled = false;

    std::vector<AmmoEnumEntry> g_CustomAmmoEntries;
    std::mutex g_CustomAmmoMutex;

    constexpr int LUA_TSTRING_CONST = 4;
    constexpr int LUA_TTABLE_CONST = 5;


    constexpr int LUA_GLOBALSINDEX_51 = -10002;


    constexpr std::int32_t FIRST_CUSTOM_AMMO_ID = 0xC0;
}

namespace
{
    static bool ValidateDeps()
    {
        return
            g_Deps.ResolveLuaApi &&
            g_Deps.GetLuaTop &&
            g_Deps.LuaType &&
            g_Deps.GetLuaString &&
            g_Deps.GetLuaInt &&
            g_Deps.LuaSetTop &&
            g_Deps.PushLuaNumber &&
            g_Deps.LuaPushString &&
            g_Deps.LuaCreateTable &&
            g_Deps.LuaGetField &&
            g_Deps.LuaSetTable &&
            g_Deps.LuaPushNil &&
            g_Deps.LuaNext &&
            g_Deps.LuaRawSet;
    }

    static bool EnsureLuaReady()
    {
        return ValidateDeps() && g_Deps.ResolveLuaApi && g_Deps.ResolveLuaApi();
    }

    static bool LuaIsString(lua_State* L, int idx)
    {
        return g_Deps.LuaType(L, idx) == LUA_TSTRING_CONST;
    }

    static bool LuaIsTable(lua_State* L, int idx)
    {
        return g_Deps.LuaType(L, idx) == LUA_TTABLE_CONST;
    }

    static void PopOne(lua_State* L)
    {
        g_Deps.LuaSetTop(L, -2);
    }

    static bool IsValidAmmoName(const std::string& name)
    {
        return !name.empty() && name.rfind("AM_", 0) == 0;
    }

    static std::int32_t GetOrCreateAmmoIdLocked(const std::string& name, bool& wasCreated)
    {
        auto it = std::find_if(
            g_CustomAmmoEntries.begin(),
            g_CustomAmmoEntries.end(),
            [&](const AmmoEnumEntry& e)
            {
                return e.name == name;
            });

        if (it != g_CustomAmmoEntries.end())
        {
            wasCreated = false;
            return it->ammoId;
        }

        std::int32_t nextId = FIRST_CUSTOM_AMMO_ID;
        for (const auto& e : g_CustomAmmoEntries)
        {
            if (e.ammoId >= nextId)
                nextId = e.ammoId + 1;
        }

        g_CustomAmmoEntries.push_back({ name, nextId });
        wasCreated = true;
        return nextId;
    }

    static std::int32_t QueueCustomAmmo(const std::string& name)
    {
        if (!IsValidAmmoName(name))
            return -1;

        std::lock_guard<std::mutex> lock(g_CustomAmmoMutex);

        bool wasCreated = false;
        const std::int32_t id = GetOrCreateAmmoIdLocked(name, wasCreated);

        Log("[DeclareAMs] %s custom AM '%s' => 0x%X (%d)\n",
            wasCreated ? "Added" : "Existing",
            name.c_str(),
            id,
            id);

        return id;
    }

    static void EnsureTppEquipTable(lua_State* L)
    {
        g_Deps.LuaGetField(L, LUA_GLOBALSINDEX_51, "TppEquip");
        if (LuaIsTable(L, -1))
            return;

        PopOne(L);

        g_Deps.LuaPushString(L, "TppEquip");
        g_Deps.LuaCreateTable(L, 0, 0);
        g_Deps.LuaRawSet(L, LUA_GLOBALSINDEX_51);

        g_Deps.LuaGetField(L, LUA_GLOBALSINDEX_51, "TppEquip");
    }

    static void InjectSingleAmmoIntoLua(lua_State* L, const std::string& name, std::int32_t ammoId)
    {
        if (!L || !EnsureLuaReady() || name.empty() || ammoId < 0)
            return;

        EnsureTppEquipTable(L);
        if (!LuaIsTable(L, -1))
        {
            PopOne(L);
            return;
        }

        g_Deps.LuaPushString(L, name.c_str());
        g_Deps.PushLuaNumber(L, static_cast<float>(ammoId));
        g_Deps.LuaSetTable(L, -3);

        Log("[DeclareAMs] Injected immediate custom AM '%s' => 0x%X (%d)\n",
            name.c_str(),
            ammoId,
            ammoId);

        PopOne(L);
    }

    static void InjectCustomAMs(lua_State* L)
    {
        std::vector<AmmoEnumEntry> snapshot;
        {
            std::lock_guard<std::mutex> lock(g_CustomAmmoMutex);
            snapshot = g_CustomAmmoEntries;
        }

        if (snapshot.empty())
            return;

        EnsureTppEquipTable(L);
        if (!LuaIsTable(L, -1))
        {
            PopOne(L);
            return;
        }

        for (const auto& entry : snapshot)
        {
            g_Deps.LuaPushString(L, entry.name.c_str());
            g_Deps.PushLuaNumber(L, static_cast<float>(entry.ammoId));
            g_Deps.LuaSetTable(L, -3);

            Log("[DeclareAMs] Injected custom AM '%s' => 0x%X (%d)\n",
                entry.name.c_str(),
                entry.ammoId,
                entry.ammoId);
        }

        PopOne(L);
    }

    static void __fastcall hkDeclareAMs(lua_State* L)
    {
        if (g_OrigDeclareAMs)
            g_OrigDeclareAMs(L);

        if (!L || !EnsureLuaReady())
            return;

        InjectCustomAMs(L);
    }
}

namespace DeclareAMs
{
    void Bind(const Deps& deps)
    {
        g_Deps = deps;
    }

    int __cdecl Lua_DeclareAMs(lua_State* L)
    {
        if (!L || !EnsureLuaReady())
            return 0;


        if (LuaIsString(L, 1))
        {
            const char* rawName = g_Deps.GetLuaString(L, 1);
            if (rawName && rawName[0] != '\0')
            {
                const std::string name = rawName;
                const std::int32_t id = QueueCustomAmmo(name);
                if (id >= 0)
                {
                    InjectSingleAmmoIntoLua(L, name, id);
                }
            }

            return 0;
        }


        if (LuaIsTable(L, 1))
        {
            g_Deps.LuaPushNil(L);

            while (g_Deps.LuaNext(L, 1) != 0)
            {
                if (LuaIsString(L, -1))
                {
                    const char* rawName = g_Deps.GetLuaString(L, -1);
                    if (rawName && rawName[0] != '\0')
                    {
                        const std::string name = rawName;
                        const std::int32_t id = QueueCustomAmmo(name);
                        if (id >= 0)
                        {
                            InjectSingleAmmoIntoLua(L, name, id);
                        }
                    }
                }

                PopOne(L);
            }
        }

        return 0;
    }

    bool Install_DeclareAMs_Hook()
    {
        if (g_DeclareAMsHookInstalled)
        {
            Log("[DeclareAMs] hook already installed\n");
            return true;
        }

        void* target = ResolveGameAddress(gAddr.DeclareAMs);
        if (!target)
        {
            Log("[DeclareAMs] Failed to resolve DeclareAMs target\n");
            return false;
        }

        const bool ok = CreateAndEnableHook(
            target,
            reinterpret_cast<void*>(&hkDeclareAMs),
            reinterpret_cast<void**>(&g_OrigDeclareAMs));

        if (!ok)
        {
            Log("[DeclareAMs] Failed to install hook\n");
            return false;
        }

        g_DeclareAMsHookInstalled = true;
        Log("[DeclareAMs] hook installed\n");
        return true;
    }

    bool Uninstall_DeclareAMs_Hook()
    {
        if (!g_DeclareAMsHookInstalled)
            return true;

        if (void* target = ResolveGameAddress(gAddr.DeclareAMs))
        {
            DisableAndRemoveHook(target);
        }

        g_OrigDeclareAMs = nullptr;
        g_DeclareAMsHookInstalled = false;
        return true;
    }
}