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
#include "EquipIdCompression.h"
#include "CustomEquipIdState.h"

namespace
{
    constexpr int LUA_GLOBALSINDEX_51 = -10002;

    // Native AddToEquipIdTable's compression formula maps:
    //   equipId in [0x000, 0x400)  -> compressed = equipId
    //   equipId in [0x400, 0x600)  -> compressed = equipId - 0x1D0
    //   equipId in [0x600, ...)    -> compressed = equipId - 0x380
    // The four parallel native arrays it indexes are sized 0x289, so
    // any equipId whose compressed index >= 0x289 OOB-writes and
    // corrupts adjacent vanilla data (causing vanilla weapon-slot
    // icons to disappear and custom-suit names to render blank).
    //
    // 0x609 was the framework's old default — sits exactly 1 past
    // the bound (0x609 - 0x380 = 0x289). The new allocator scans
    // for compressed slots that vanilla's boot reload didn't claim
    // (observed via EquipIdCompression::MarkCompressedSlotUsed), so
    // we no longer need a hardcoded "first custom" constant. We
    // keep this minimum=0 since the allocator will skip every slot
    // vanilla actually populated.
    constexpr std::int32_t kMinimumCustomEquipId = 0;

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

    std::int32_t ComputeMinimumCustomEquipId(lua_State* /*L*/, const int /*tppEquipIndex*/)
    {
        // Old behavior tried to find the highest declared TppEquip.EQP_*
        // value and start above it. That made sense when the custom
        // range lived above vanilla equipIds — but the actual constraint
        // is the COMPRESSED slot bound (0x289), not "above all vanilla
        // numbers." Vanilla's highest equipId (0x608) compresses to slot
        // 0x288, the LAST in-bounds slot, so "above 0x608" is by
        // definition out of bounds.
        //
        // Allocator strategy now: scan compressed slots [0, 0x289) for
        // ones vanilla didn't claim (observed via the AddToEquipIdTable
        // observer hook) and the session hasn't claimed. The first such
        // slot's equipId form (== slot index, since compressed == equipId
        // for equipId < 0x400) is what we return. Returning 0 here means
        // "no minimum constraint" — the scan starts at 0 and walks up.
        return kMinimumCustomEquipId;
    }

    bool IsCustomEquipIdInSafeRange(const std::int32_t equipId)
    {
        // "Safe" now means "won't OOB-write the native EquipIdTable
        // compressed-slot arrays" (and won't be negative). This matches
        // exactly what the native AddToEquipIdTable can store — vanilla
        // equipIds in the same compressed range collide if used, but
        // the allocator gives us slots vanilla is observed NOT to use.
        return equipId >= 0
            && EquipIdCompression::IsEquipIdSafeForNativeTable(equipId);
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

            Log("[RegisterConstantEquipId] Failed: resolved id 0x%X for '%s' "
                "is out of bounds for the native AddToEquipIdTable "
                "(compressed=0x%X, max=0x%X). The framework's allocator "
                "should have picked an in-bounds free slot — investigate "
                "V_FrameWorkState's allocator and the AddToEquipIdTable "
                "observer hook installation order.\n",
                resolvedEquipId,
                eqpName.c_str(),
                EquipIdCompression::ComputeCompressed(resolvedEquipId),
                EquipIdCompression::kCompressedSlotBound);

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