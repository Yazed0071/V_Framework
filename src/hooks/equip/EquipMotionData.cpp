#include "pch.h"
#include "EquipMotionData.h"

#include <Windows.h>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    constexpr int LUA_TTABLE_51 = 5;
    constexpr int LUA_TNUMBER_51 = 3;
    constexpr int LUA_TSTRING_51 = 4;

    EquipMotionData::Deps g_Deps{};

    using ReloadEquipMotionData_t = int(__fastcall*)(lua_State* L);
    ReloadEquipMotionData_t g_OrigReloadEquipMotionData = nullptr;
    bool g_HookInstalled = false;

    struct MotionPair
    {
        std::uint32_t equipId = 0;
        std::string   mtarPath;
    };

    std::mutex g_QueueMutex;
    std::vector<MotionPair> g_QueuedPairs;

    static bool ValidateDeps()
    {
        return
            g_Deps.ResolveLuaApi &&
            g_Deps.GetLuaTop &&
            g_Deps.LuaType &&
            g_Deps.LuaObjLen &&
            g_Deps.LuaPop &&
            g_Deps.GetLuaString &&
            g_Deps.GetLuaInt &&
            g_Deps.LuaGetField &&
            g_Deps.LuaGetTable &&
            g_Deps.LuaPushString &&
            g_Deps.LuaCreateTable &&
            g_Deps.LuaSetTable &&
            g_Deps.PushLuaNumber &&
            g_Deps.LuaPushValue &&
            g_Deps.LuaPushNil &&
            g_Deps.LuaNext;
    }

    static bool LuaApiReady()
    {
        return ValidateDeps() && g_Deps.ResolveLuaApi && g_Deps.ResolveLuaApi();
    }

    static void QueueOrUpdatePair(std::uint32_t equipId, const char* mtarPath)
    {
        if (!mtarPath || !mtarPath[0])
            return;

        std::lock_guard<std::mutex> lock(g_QueueMutex);

        for (auto& p : g_QueuedPairs)
        {
            if (p.equipId == equipId)
            {
                p.mtarPath = mtarPath;
                return;
            }
        }

        g_QueuedPairs.push_back({ equipId, mtarPath });
    }

    static int ReadPairsFromLuaArg(lua_State* L, int argIdx)
    {
        int added = 0;

        g_Deps.LuaPushNil(L);
        while (g_Deps.LuaNext(L, argIdx) != 0)
        {
            // stack: ..., key, value (the pair)
            if (g_Deps.LuaType(L, -1) == LUA_TTABLE_51)
            {
                std::uint32_t equipId = 0;
                const char* path = nullptr;

                g_Deps.PushLuaNumber(L, 1.0f);
                g_Deps.LuaGetTable(L, -2);
                if (g_Deps.LuaType(L, -1) == LUA_TNUMBER_51)
                    equipId = static_cast<std::uint32_t>(g_Deps.GetLuaInt(L, -1));
                g_Deps.LuaPop(L, 1);

                g_Deps.PushLuaNumber(L, 2.0f);
                g_Deps.LuaGetTable(L, -2);
                if (g_Deps.LuaType(L, -1) == LUA_TSTRING_51)
                    path = g_Deps.GetLuaString(L, -1);

                if (path && path[0])
                {
                    QueueOrUpdatePair(equipId, path);
                    ++added;
                }

                g_Deps.LuaPop(L, 1);
            }

            g_Deps.LuaPop(L, 1);
        }

        return added;
    }

    static void InjectQueuedPairsIntoArg(lua_State* L)
    {
        std::vector<MotionPair> snapshot;
        {
            std::lock_guard<std::mutex> lock(g_QueueMutex);
            snapshot = g_QueuedPairs;
        }

        if (snapshot.empty())
            return;

        if (!L || !LuaApiReady())
            return;

        // The orig reads the arg at stack idx -1 and looks for the
        // "MotionDataTable" field. We append our pairs to that array.
        if (g_Deps.LuaType(L, -1) != LUA_TTABLE_51)
            return;

        g_Deps.LuaGetField(L, -1, "MotionDataTable");
        if (g_Deps.LuaType(L, -1) != LUA_TTABLE_51)
        {
            g_Deps.LuaPop(L, 1);
            return;
        }

        const int innerIdx = g_Deps.GetLuaTop(L);
        const std::size_t startLen = g_Deps.LuaObjLen(L, innerIdx);

        for (std::size_t i = 0; i < snapshot.size(); ++i)
        {
            const int newIndex = static_cast<int>(startLen) + static_cast<int>(i) + 1;

            g_Deps.PushLuaNumber(L, static_cast<float>(newIndex));

            g_Deps.LuaCreateTable(L, 2, 0);
            g_Deps.PushLuaNumber(L, 1.0f);
            g_Deps.PushLuaNumber(L, static_cast<float>(snapshot[i].equipId));
            g_Deps.LuaSetTable(L, -3);

            g_Deps.PushLuaNumber(L, 2.0f);
            g_Deps.LuaPushString(L, snapshot[i].mtarPath.c_str());
            g_Deps.LuaSetTable(L, -3);

            g_Deps.LuaSetTable(L, innerIdx);
        }

        g_Deps.LuaPop(L, 1);

        Log("[ReloadEquipMotionData] Injected %zu queued pair(s) before orig\n", snapshot.size());
    }

    static int __fastcall hkReloadEquipMotionData(lua_State* L)
    {
        InjectQueuedPairsIntoArg(L);

        if (g_OrigReloadEquipMotionData)
            return g_OrigReloadEquipMotionData(L);

        return 0;
    }
}

namespace EquipMotionData
{
    void Bind(const Deps& deps)
    {
        g_Deps = deps;
    }

    int __cdecl Lua_AddToEquipMotionDataTable(lua_State* L)
    {
        if (!L)
        {
            Log("[AddToEquipMotionDataTable] FAILED: lua_State is null\n");
            return 0;
        }

        if (!LuaApiReady())
        {
            Log("[AddToEquipMotionDataTable] FAILED: missing deps\n");
            return 0;
        }

        if (g_Deps.LuaType(L, 1) != LUA_TTABLE_51)
        {
            Log("[AddToEquipMotionDataTable] FAILED: arg 1 is not a table\n");
            return 0;
        }

        const int added = ReadPairsFromLuaArg(L, 1);

        std::size_t total = 0;
        {
            std::lock_guard<std::mutex> lock(g_QueueMutex);
            total = g_QueuedPairs.size();
        }

        if (added > 0)
        {
            Log("[AddToEquipMotionDataTable] Queued %d pair(s); total queue size: %zu\n",
                added, total);
        }
        else
        {
            Log("[AddToEquipMotionDataTable] No valid pairs in input\n");
        }

        return 0;
    }

    bool Install_ReloadEquipMotionData_Hook()
    {
        if (g_HookInstalled)
        {
            Log("[ReloadEquipMotionData] hook already installed\n");
            return true;
        }

        if (gAddr.ReloadEquipMotionData == 0)
        {
            Log("[ReloadEquipMotionData] address not configured for this build; skipping hook install\n");
            return false;
        }

        void* target = ResolveGameAddress(gAddr.ReloadEquipMotionData);
        if (!target)
        {
            Log("[ReloadEquipMotionData] address unresolved; skipping hook install\n");
            return false;
        }

        const bool ok = CreateAndEnableHook(
            target,
            reinterpret_cast<void*>(&hkReloadEquipMotionData),
            reinterpret_cast<void**>(&g_OrigReloadEquipMotionData));

        if (!ok)
        {
            Log("[ReloadEquipMotionData] hook install failed\n");
            return false;
        }

        g_HookInstalled = true;
        Log("[ReloadEquipMotionData] hook installed at 0x%p\n", target);
        return true;
    }

    bool Uninstall_ReloadEquipMotionData_Hook()
    {
        if (!g_HookInstalled)
            return true;

        if (void* target = ResolveGameAddress(gAddr.ReloadEquipMotionData))
        {
            DisableAndRemoveHook(target);
        }

        g_OrigReloadEquipMotionData = nullptr;
        g_HookInstalled = false;
        return true;
    }
}
