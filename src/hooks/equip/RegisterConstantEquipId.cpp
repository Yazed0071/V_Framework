#include "pch.h"

#include "AddressSet.h"
#include "FoxHashes.h"
#include "HookUtils.h"
#include "log.h"
extern "C" {
    #include "lua.h"
}

#include <cstdint>
#include <string>
#include "RegisterConstantEquipId.h"
#include "CustomEquipIdState.h"

namespace
{
    constexpr int LUA_GLOBALSINDEX_51 = -10002;

    // Native RegisterConstantEquipId uses IDs up through 0x608.
    // Keep all custom IDs above that range.
    constexpr std::int32_t kFirstCustomEquipId = 0x609;

    RegisterConstantEquipId::Deps g_Deps{};
    bool g_IsBound = false;

    bool HasDeps()
    {
        return g_IsBound &&
            g_Deps.ResolveLuaApi &&
            g_Deps.GetLuaTop &&
            g_Deps.LuaGetField &&
            g_Deps.LuaType &&
            g_Deps.LuaIsString &&
            g_Deps.LuaIsNumber &&
            g_Deps.LuaPop &&
            g_Deps.GetLuaString &&
            g_Deps.GetLuaInt &&
            g_Deps.PushLuaNumber &&
            g_Deps.LuaPushString &&
            g_Deps.LuaCreateTable &&
            g_Deps.LuaRawSet &&
            g_Deps.LuaSetTable &&
            g_Deps.LuaPushNil &&
            g_Deps.LuaNext;
    }

    std::uint32_t* ResolveRegisterConstantEquipIdHashTable()
    {
        if (!gAddr.RegisterConstantEquipIdHashTable)
            return nullptr;

        return reinterpret_cast<std::uint32_t*>(
            ResolveGameAddress(gAddr.RegisterConstantEquipIdHashTable));
    }

    int ComputeRegisterConstantEquipIdHashIndex(const std::uint32_t equipId)
    {
        if (equipId < 0x400)
            return static_cast<int>(equipId);

        if (equipId < 0x500)
            return static_cast<int>(equipId - 0x99);

        if (equipId < 0x600)
            return static_cast<int>(equipId - 0x149);

        return static_cast<int>(equipId - 0x223);
    }

    int FindHighestDeclaredEqpValue(lua_State* L, const int tableIndex)
    {
        int highest = -1;

        g_Deps.LuaPushNil(L);
        while (g_Deps.LuaNext(L, tableIndex) != 0)
        {
            // key at -2, value at -1
            if (g_Deps.LuaIsString(L, -2) && g_Deps.LuaIsNumber(L, -1))
            {
                const char* key = g_Deps.GetLuaString(L, -2);
                const int value = g_Deps.GetLuaInt(L, -1);

                if (key &&
                    key[0] == 'E' &&
                    key[1] == 'Q' &&
                    key[2] == 'P' &&
                    key[3] == '_' &&
                    value > highest)
                {
                    highest = value;
                }
            }

            // pop value, keep key for next iteration
            g_Deps.LuaPop(L, 1);
        }

        return highest;
    }

    std::int32_t ComputeMinimumCustomEquipId(lua_State* L, const int tppEquipIndex)
    {
        std::int32_t minimumEquipId = kFirstCustomEquipId;

        const int highestEqpValue = FindHighestDeclaredEqpValue(L, tppEquipIndex);
        if (highestEqpValue >= 0)
        {
            const std::int32_t nextAfterHighest = static_cast<std::int32_t>(highestEqpValue + 1);
            if (nextAfterHighest > minimumEquipId)
                minimumEquipId = nextAfterHighest;
        }

        return minimumEquipId;
    }

    bool IsCustomEquipIdInSafeRange(const std::int32_t equipId)
    {
        return equipId >= kFirstCustomEquipId;
    }
}

namespace RegisterConstantEquipId
{
    void Bind(const Deps& deps)
    {
        g_Deps = deps;
        g_IsBound = true;
    }

    int __cdecl Lua_RegisterConstantEquipId(lua_State* L)
    {
        if (!L || !HasDeps() || !g_Deps.ResolveLuaApi() || !g_Deps.LuaIsString(L, 1))
        {
            if (HasDeps() && g_Deps.PushLuaNumber)
                g_Deps.PushLuaNumber(L, -1.0f);
            return 1;
        }

        const char* rawName = g_Deps.GetLuaString(L, 1);
        if (!rawName || !rawName[0])
        {
            g_Deps.PushLuaNumber(L, -1.0f);
            return 1;
        }

        std::string eqpName = rawName;
        if (eqpName.rfind("EQP_", 0) != 0)
            eqpName = "EQP_" + eqpName;

        std::uint32_t* hashTable = ResolveRegisterConstantEquipIdHashTable();
        if (!hashTable)
        {
            Log("[RegisterConstantEquipId] Failed: native hash table not resolved\n");
            g_Deps.PushLuaNumber(L, -1.0f);
            return 1;
        }

        // Get or create TppEquip.
        g_Deps.LuaGetField(L, LUA_GLOBALSINDEX_51, "TppEquip");
        if (g_Deps.LuaType(L, -1) != LUA_TTABLE)
        {
            g_Deps.LuaPop(L, 1);

            g_Deps.LuaPushString(L, "TppEquip");
            g_Deps.LuaCreateTable(L, 0, 0);
            g_Deps.LuaRawSet(L, LUA_GLOBALSINDEX_51);

            g_Deps.LuaGetField(L, LUA_GLOBALSINDEX_51, "TppEquip");
            if (g_Deps.LuaType(L, -1) != LUA_TTABLE)
            {
                Log("[RegisterConstantEquipId] Failed: could not create TppEquip\n");
                g_Deps.PushLuaNumber(L, -1.0f);
                return 1;
            }
        }

        const int tppEquipIndex = g_Deps.GetLuaTop(L);

        // Never replace existing declarations.
        g_Deps.LuaGetField(L, tppEquipIndex, eqpName.c_str());
        const int existingType = g_Deps.LuaType(L, -1);

        if (existingType == LUA_TNUMBER)
        {
            const int existingId = g_Deps.GetLuaInt(L, -1);
            g_Deps.LuaPop(L, 2);

            Log("[RegisterConstantEquipId] Existing equip '%s' => 0x%X (%d)\n",
                eqpName.c_str(),
                existingId,
                existingId);

            g_Deps.PushLuaNumber(L, static_cast<float>(existingId));
            return 1;
        }

        if (existingType != LUA_TNIL && existingType != LUA_TNONE)
        {
            g_Deps.LuaPop(L, 2);

            Log("[RegisterConstantEquipId] Reject: '%s' already exists and is not numeric\n",
                eqpName.c_str());

            g_Deps.PushLuaNumber(L, -1.0f);
            return 1;
        }

        g_Deps.LuaPop(L, 1);

        const std::int32_t minimumEquipId = ComputeMinimumCustomEquipId(L, tppEquipIndex);

        std::int32_t resolvedEquipId = 0;
        if (!CustomEquipIdState::ResolveOrCreatePersistentEquipId(
            eqpName.c_str(),
            minimumEquipId,
            resolvedEquipId))
        {
            g_Deps.LuaPop(L, 1);

            Log("[RegisterConstantEquipId] Failed: persistent resolve failed for '%s'\n",
                eqpName.c_str());

            g_Deps.PushLuaNumber(L, -1.0f);
            return 1;
        }

        if (!IsCustomEquipIdInSafeRange(resolvedEquipId))
        {
            g_Deps.LuaPop(L, 1);

            Log("[RegisterConstantEquipId] Failed: resolved id 0x%X for '%s' is below custom range start 0x%X\n",
                resolvedEquipId,
                eqpName.c_str(),
                kFirstCustomEquipId);

            g_Deps.PushLuaNumber(L, -1.0f);
            return 1;
        }

        const std::uint32_t newEquipId = static_cast<std::uint32_t>(resolvedEquipId);
        const int hashIndex = ComputeRegisterConstantEquipIdHashIndex(newEquipId);

        g_Deps.LuaPushString(L, eqpName.c_str());
        g_Deps.PushLuaNumber(L, static_cast<float>(newEquipId));
        g_Deps.LuaSetTable(L, tppEquipIndex);

        hashTable[hashIndex] = FoxHashes::StrCode32(eqpName);

        g_Deps.LuaPop(L, 1);

        Log("[RegisterConstantEquipId] Added custom equip '%s' => 0x%X (%u) [hashIndex=0x%X hash=0x%08X]\n",
            eqpName.c_str(),
            newEquipId,
            newEquipId,
            hashIndex,
            hashTable[hashIndex]);

        g_Deps.PushLuaNumber(L, static_cast<float>(newEquipId));
        return 1;
    }
}