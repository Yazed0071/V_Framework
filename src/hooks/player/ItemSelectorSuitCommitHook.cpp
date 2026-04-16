#include "pch.h"

#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"
#include "CustomSuitRegistry.h"
#include "ItemSelectorSuitCommitHook.h"

namespace
{
    struct ErrorCode
    {
        int code = 0;
    };

    struct LivePlayerAppearance
    {
        std::uint8_t  partsType = 0xFF;    // +0xF8
        std::uint8_t  selector = 0xFF;     // +0xF9
        std::uint8_t  playerType = 0xFF;   // +0xFB
        std::uint16_t faceId = 0xFFFF;     // +0xFC
        std::uint16_t headOption = 0xFFFF; // +0xFE
    };

    using DecideActMissionPreparationSetEquipMode_t =
        ErrorCode * (__fastcall*)(void* self, ErrorCode* retStorage);

    using DecideActMotherBaseDeviceSupportDropMode_t =
        void(__fastcall*)(void* self);

    using GetQuarkSystemTable_t = void* (__fastcall*)();

    static DecideActMissionPreparationSetEquipMode_t
        g_OrigDecideActMissionPreparationSetEquipMode = nullptr;

    static DecideActMotherBaseDeviceSupportDropMode_t
        g_OrigDecideActMotherBaseDeviceSupportDropMode = nullptr;

    using SupplyDropSuitSetup_t = void(__fastcall*)(void* self, std::uint64_t param2);

    static GetQuarkSystemTable_t g_GetQuarkSystemTable = nullptr;
    static SupplyDropSuitSetup_t g_OrigSupplyDropSuitSetup = nullptr;

    static bool g_InstalledMissionPrep = false;
    static bool g_InstalledSupportDrop = false;
    static bool g_InstalledSupplyDropSetup = false;

    static constexpr std::uint32_t kEquipKind_MissionPrepSuit = 0x80;

    static bool ResolveApis()
    {
        if (!g_GetQuarkSystemTable)
        {
            g_GetQuarkSystemTable =
                reinterpret_cast<GetQuarkSystemTable_t>(
                    ResolveGameAddress(gAddr.GetQuarkSystemTable)
                    );
        }

        return g_GetQuarkSystemTable != nullptr;
    }

    static bool TryReadLivePlayerAppearance(LivePlayerAppearance& out)
    {
        if (!ResolveApis())
            return false;

        auto* quarkSystemTable =
            reinterpret_cast<std::uint8_t*>(g_GetQuarkSystemTable());
        if (!quarkSystemTable)
            return false;

        auto* q98 = *reinterpret_cast<std::uint8_t**>(quarkSystemTable + 0x98);
        if (!q98)
            return false;

        auto* state = *reinterpret_cast<std::uint8_t**>(q98 + 0x10);
        if (!state)
            return false;

        out.partsType = state[0xF8];
        out.selector = state[0xF9];
        out.playerType = state[0xFB];
        out.faceId = *reinterpret_cast<std::uint16_t*>(state + 0xFC);
        out.headOption = *reinterpret_cast<std::uint16_t*>(state + 0xFE);
        return true;
    }

    static bool TryGetSelectedId(
        void* self,
        std::uint32_t& outEquipKind,
        std::uint32_t& outRow,
        std::uint8_t& outVariant,
        std::uint16_t& outSelectedId)
    {
        if (!self)
            return false;

        auto* p = reinterpret_cast<std::uint8_t*>(self);

        outEquipKind = *reinterpret_cast<std::uint32_t*>(p + 0x4434);

        const std::uint32_t row =
            *reinterpret_cast<std::uint32_t*>(p + 0x110) +
            *reinterpret_cast<std::uint32_t*>(p + 0x10C);

        outRow = row;
        outVariant = *(p + 0xC040 + row);

        if (outVariant >= 0x0F)
            return false;

        const std::size_t slot =
            static_cast<std::size_t>(row) * 0x0Fu + outVariant;

        outSelectedId =
            *reinterpret_cast<std::uint16_t*>(p + 0x4440 + slot * sizeof(std::uint16_t));

        return true;
    }

    static void TrackSelectedCustomSuit(
        void* self,
        const char* tag)
    {
        std::uint32_t equipKind = 0;
        std::uint32_t row = 0;
        std::uint8_t variant = 0;
        std::uint16_t selectedId = 0;

        if (!TryGetSelectedId(self, equipKind, row, variant, selectedId))
        {
            Log("[%s] NoSelection self=%p\n", tag, self);
            return;
        }

        Log(
            "[%s] Seen equipKind=0x%X row=%u variant=%u selectedId=%u\n",
            tag,
            static_cast<unsigned>(equipKind),
            static_cast<unsigned>(row),
            static_cast<unsigned>(variant),
            static_cast<unsigned>(selectedId)
        );

        LivePlayerAppearance live{};
        const bool haveLive = TryReadLivePlayerAppearance(live);
        const std::uint8_t currentPlayerType =
            haveLive ? live.playerType : 0xFF;

        const CustomSuitEntry* entry = nullptr;

        if (TryGetCustomSuitByDevelopIdForPlayerType(selectedId, currentPlayerType, &entry) && entry)
        {
            SetPendingCustomSuitDevelopId(entry->linkedDevelopId);

            Log(
                "[%s] Track by developId=%u playerType=%u -> developId=%u partsType=0x%02X selector=0x%02X\n",
                tag,
                static_cast<unsigned>(selectedId),
                static_cast<unsigned>(currentPlayerType),
                static_cast<unsigned>(entry->linkedDevelopId),
                static_cast<unsigned>(entry->customPartsType),
                static_cast<unsigned>(entry->customSelectorCode)
            );
        }
        else if (TryGetCustomSuitByFlowIndexForPlayerType(selectedId, currentPlayerType, &entry) && entry)
        {
            SetPendingCustomSuitDevelopId(entry->linkedDevelopId);

            Log(
                "[%s] Track by flowIndex=%u playerType=%u -> developId=%u partsType=0x%02X selector=0x%02X\n",
                tag,
                static_cast<unsigned>(selectedId),
                static_cast<unsigned>(currentPlayerType),
                static_cast<unsigned>(entry->linkedDevelopId),
                static_cast<unsigned>(entry->customPartsType),
                static_cast<unsigned>(entry->customSelectorCode)
            );
        }
        else
        {
            ClearPendingCustomSuitDevelopId();

            Log(
                "[%s] Miss selectedId=%u playerType=%u equipKind=0x%X\n",
                tag,
                static_cast<unsigned>(selectedId),
                static_cast<unsigned>(currentPlayerType),
                static_cast<unsigned>(equipKind)
            );
        }
    }
}

// Hook for FUN_1416a7610 — sets up the supply drop request for suit equips.
// The original does table[flowIndex*0x68+0x36] which goes out of bounds for
// custom flowIndices.  For custom suits, we skip the table lookup entirely
// and populate the request fields directly from the selector's internal arrays.
static void __fastcall hkSupplyDropSuitSetup(void* self_raw, std::uint64_t param2)
{
    auto* p = reinterpret_cast<std::uint8_t*>(self_raw);
    const std::uint32_t row = static_cast<std::uint32_t>(param2 & 0xFFFFFFFF);

    // Read selected flowIndex — same logic as decompiled lines 2954022-2954024
    auto* pIVar16 = p + row + 0xc040;
    const std::uint64_t lVar15 = static_cast<std::uint64_t>(row) * 0x0F;
    const std::uint8_t variantByte = *pIVar16;
    const std::uint16_t selFlowIndex = *reinterpret_cast<std::uint16_t*>(
        p + (static_cast<std::uint64_t>(variantByte) + lVar15) * 2 + 0x4440);

    if (selFlowIndex == 0x400)
        return; // blank sentinel — nothing to do

    // Check if the selected suit is a custom suit
    const CustomSuitEntry* entry = nullptr;
    if (TryGetCustomSuitByFlowIndex(selFlowIndex, &entry) && entry)
    {
        // Populate supply drop request directly from CustomSuitEntry.
        // The selector's internal arrays (0xcc40+) aren't reliably populated
        // in supply drop context, so we read from our registry instead.
        *(std::uint32_t*)(p + 0x46240) = 3;   // type = suit equip
        *(std::uint32_t*)(p + 0x4630c) = 1;   // flags = suit

        p[0x46251] = entry->customSelectorCode;  // selectorCode
        p[0x46250] = 0;                           // variant index (base)
        p[0x46252] = 0;                           // headOption

        Log("[SupplyDropSuitSetup] custom flowIndex=%u sel=0x%02X partsType=0x%02X devId=%u\n",
            static_cast<unsigned>(selFlowIndex),
            static_cast<unsigned>(entry->customSelectorCode),
            static_cast<unsigned>(entry->customPartsType),
            static_cast<unsigned>(entry->linkedDevelopId));
        return;
    }

    // Vanilla suit — call original (table lookup is valid)
    g_OrigSupplyDropSuitSetup(self_raw, param2);
}

static ErrorCode* __fastcall hkDecideActMissionPreparationSetEquipMode(
    void* self,
    ErrorCode* retStorage)
{
    if (!self || !retStorage)
        return g_OrigDecideActMissionPreparationSetEquipMode(self, retStorage);

    std::uint32_t equipKind = 0;
    std::uint32_t row = 0;
    std::uint8_t variant = 0;
    std::uint16_t selectedId = 0;

    if (!TryGetSelectedId(self, equipKind, row, variant, selectedId))
    {
        Log("[SuitCommit] NoSelection self=%p\n", self);
        return g_OrigDecideActMissionPreparationSetEquipMode(self, retStorage);
    }

    if (equipKind == kEquipKind_MissionPrepSuit)
        TrackSelectedCustomSuit(self, "SuitCommit");
    else
        ClearPendingCustomSuitDevelopId();

    return g_OrigDecideActMissionPreparationSetEquipMode(self, retStorage);
}

static void __fastcall hkDecideActMotherBaseDeviceSupportDropMode(
    void* self)
{
    if (!self)
    {
        g_OrigDecideActMotherBaseDeviceSupportDropMode(self);
        return;
    }

    std::uint32_t equipKind = 0;
    std::uint32_t row = 0;
    std::uint8_t variant = 0;
    std::uint16_t selectedId = 0;

    if (TryGetSelectedId(self, equipKind, row, variant, selectedId) &&
        equipKind == kEquipKind_MissionPrepSuit)
    {
        // For custom suits in supply drop, populate the request manually
        // then let the original send the SendTrigger.  FUN_1416a7610 inside
        // the original does table[flowIndex*0x68+0x36] which is out of bounds
        // for custom flowIndices → infinite loading.
        // We set up the request fields so the original's FUN_1416a7610 path
        // writes valid data.
        const CustomSuitEntry* entry = nullptr;
        bool isCustom = false;
        if (TryGetCustomSuitByFlowIndexForPlayerType(
                selectedId,
                g_GetQuarkSystemTable ? static_cast<std::uint8_t>(
                    *reinterpret_cast<std::uint8_t*>(
                        *reinterpret_cast<std::uint8_t**>(
                            *reinterpret_cast<std::uint8_t**>(
                                reinterpret_cast<std::uint8_t*>(
                                    g_GetQuarkSystemTable()) + 0x98) + 0x10) + 0xFB)) : 0xFF,
                &entry) && entry)
        {
            isCustom = true;
        }
        else if (TryGetCustomSuitByDevelopIdForPlayerType(
                selectedId,
                g_GetQuarkSystemTable ? static_cast<std::uint8_t>(
                    *reinterpret_cast<std::uint8_t*>(
                        *reinterpret_cast<std::uint8_t**>(
                            *reinterpret_cast<std::uint8_t**>(
                                reinterpret_cast<std::uint8_t*>(
                                    g_GetQuarkSystemTable()) + 0x98) + 0x10) + 0xFB)) : 0xFF,
                &entry) && entry)
        {
            isCustom = true;
        }

        if (isCustom && entry)
        {
            // Pre-populate the supply drop request area so the original's
            // FUN_1416a7610 code writes to already-initialized data.
            auto* p = reinterpret_cast<std::uint8_t*>(self);
            auto* ptr = p + 0x46240;

            // Set request type=3 (suit equip)
            *reinterpret_cast<std::uint32_t*>(ptr) = 3;
            // Set flags=1 (suit)
            *reinterpret_cast<std::uint32_t*>(p + 0x4630c) = 1;
            // Use CustomSuitEntry data directly (selector arrays unreliable
            // in supply drop context)
            p[0x46251] = entry->customSelectorCode;
            p[0x46250] = 0;   // variant index (base)
            p[0x46252] = 0;   // headOption

            SetPendingCustomSuitDevelopId(entry->linkedDevelopId);

            Log("[SupportDropSuitCommit] custom flowIndex=%u devId=%u "
                "selector=0x%02X parts=0x%02X\n",
                static_cast<unsigned>(selectedId),
                static_cast<unsigned>(entry->linkedDevelopId),
                static_cast<unsigned>(p[0x46251]),
                static_cast<unsigned>(p[0x46250]));

            // Now call original — it will go to LAB_1416a46f7 and send
            // the SendTrigger with our pre-populated request data.
            // FUN_1416a7610 will still run but our data is already set.
            g_OrigDecideActMotherBaseDeviceSupportDropMode(self);
            return;
        }

        TrackSelectedCustomSuit(self, "SupportDropSuitCommit");
    }
    else
    {
        ClearPendingCustomSuitDevelopId();
    }

    g_OrigDecideActMotherBaseDeviceSupportDropMode(self);
}

bool Install_ItemSelectorSuitCommit_Hook()
{
    bool okMissionPrep = g_InstalledMissionPrep;
    bool okSupportDrop = g_InstalledSupportDrop;

    if (!ResolveApis())
    {
        Log("[Hook] ItemSelectorSuitCommit: failed to resolve GetQuarkSystemTable\n");
        return false;
    }

    if (!g_InstalledMissionPrep)
    {
        void* targetMissionPrep =
            ResolveGameAddress(gAddr.ItemSelectorCallbackImpl_DecideActMissionPreparationSetEquipMode);

        if (!targetMissionPrep)
        {
            Log("[Hook] ItemSelectorSuitCommit: failed to resolve mission-prep target\n");
            return false;
        }

        okMissionPrep = CreateAndEnableHook(
            targetMissionPrep,
            reinterpret_cast<void*>(&hkDecideActMissionPreparationSetEquipMode),
            reinterpret_cast<void**>(&g_OrigDecideActMissionPreparationSetEquipMode)
        );

        Log("[Hook] ItemSelectorSuitCommit: %s\n", okMissionPrep ? "OK" : "FAIL");
        g_InstalledMissionPrep = okMissionPrep;
    }

    if (!g_InstalledSupportDrop)
    {
        void* targetSupportDrop =
            ResolveGameAddress(gAddr.ItemSelectorCallbackImpl_DecideActMotherBaseDeviceSupportDropMode);

        if (!targetSupportDrop)
        {
            Log("[Hook] SupportDropSuitCommit: failed to resolve support-drop target\n");
            return false;
        }

        okSupportDrop = CreateAndEnableHook(
            targetSupportDrop,
            reinterpret_cast<void*>(&hkDecideActMotherBaseDeviceSupportDropMode),
            reinterpret_cast<void**>(&g_OrigDecideActMotherBaseDeviceSupportDropMode)
        );

        Log("[Hook] SupportDropSuitCommit: %s\n", okSupportDrop ? "OK" : "FAIL");
        g_InstalledSupportDrop = okSupportDrop;
    }

    // Hook FUN_1416a7610 — supply drop suit setup (table[flowIndex*0x68] fix)
    if (!g_InstalledSupplyDropSetup && gAddr.SupplyDropSuitSetup != 0)
    {
        void* target = ResolveGameAddress(gAddr.SupplyDropSuitSetup);
        if (target)
        {
            const bool ok = CreateAndEnableHook(
                target,
                reinterpret_cast<void*>(&hkSupplyDropSuitSetup),
                reinterpret_cast<void**>(&g_OrigSupplyDropSuitSetup));
            Log("[Hook] SupplyDropSuitSetup: %s\n", ok ? "OK" : "FAIL");
            g_InstalledSupplyDropSetup = ok;
        }
    }

    return g_InstalledMissionPrep && g_InstalledSupportDrop;
}

bool Uninstall_ItemSelectorSuitCommit_Hook()
{
    if (g_InstalledMissionPrep)
    {
        if (void* target =
            ResolveGameAddress(gAddr.ItemSelectorCallbackImpl_DecideActMissionPreparationSetEquipMode))
        {
            DisableAndRemoveHook(target);
        }

        g_OrigDecideActMissionPreparationSetEquipMode = nullptr;
        g_InstalledMissionPrep = false;
    }

    if (g_InstalledSupportDrop)
    {
        if (void* target =
            ResolveGameAddress(gAddr.ItemSelectorCallbackImpl_DecideActMotherBaseDeviceSupportDropMode))
        {
            DisableAndRemoveHook(target);
        }

        g_OrigDecideActMotherBaseDeviceSupportDropMode = nullptr;
        g_InstalledSupportDrop = false;
    }

    g_GetQuarkSystemTable = nullptr;

    Log("[Hook] ItemSelectorSuitCommit: removed\n");
    return true;
}