#include "pch.h"

#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"
#include "tpp/player/appearance/CustomSuitRegistry.h"
#include "tpp/player/appearance/ItemSelectorSuitCommitHook.h"

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

    static GetQuarkSystemTable_t g_GetQuarkSystemTable = nullptr;

    static bool g_InstalledMissionPrep = false;
    static bool g_InstalledSupportDrop = false;

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

    TrackSelectedCustomSuit(self, "SupportDropSuitCommit");

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