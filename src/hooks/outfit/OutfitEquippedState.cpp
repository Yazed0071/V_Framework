#include "pch.h"

#include "OutfitEquippedState.h"
#include "OutfitRegistry.h"

#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    using IsEquipDeveloped_t =
        std::uint8_t (__fastcall*)(void* self, std::uint32_t flowIndex);

    using GetEquipIdFromLoadoutInfo_t =
        std::uint64_t (__fastcall*)(void* self, std::uint32_t slot);

    static IsEquipDeveloped_t          g_OrigIsEquipDeveloped         = nullptr;
    static GetEquipIdFromLoadoutInfo_t g_OrigGetEquipIdFromLoadoutInfo = nullptr;

    static bool g_InstalledIsDeveloped = false;
    static bool g_InstalledGetEquipId  = false;

    static void* g_CachedEDC = nullptr;

    // Slot 1 = UNIFORMS in the loadout-info dispatcher. Confirmed by
    // tracing GetEquipIdFromLoadoutInfo callers; SetupEquipPanelParam
    // calls this with slot from the panel-builder iteration counter.
    constexpr std::uint32_t kSlotUniforms = 1;

    static std::uint8_t __fastcall hkIsEquipDeveloped(void* self, std::uint32_t flowIndex)
    {
        if (!g_CachedEDC && self)
        {
            g_CachedEDC = self;
            Log("[OutfitEquippedState] cached EDC=%p\n", self);
        }

        // EDC bit-array bounds: capacity is 0x400 (1024) per the row
        // table; the vanilla IsEquipDeveloped does an unchecked
        // bit_array[flowIndex] read, so any flowIndex >= 0x400 is an
        // OOB read in the game's own code. We ALWAYS bound-check
        // before passing through, regardless of whether the
        // flowIndex is custom or vanilla.
        constexpr std::uint32_t kEdcRowCapacity = 0x400;
        if (flowIndex >= kEdcRowCapacity)
            return 0;

        const outfit::OutfitEntry* entry = nullptr;
        if (outfit::TryGetOutfitByFlowIndex(static_cast<std::uint16_t>(flowIndex), &entry)
            && entry)
        {
            // Hide outfits whose playerType doesn't match the live
            // character so e.g. a DD-Female-only outfit doesn't show
            // up as developed when Snake is active.
            const std::uint8_t live = outfit::ReadLivePlayerType();
            if (live != 0xFF && entry->playerType != live) return 0;

            // For matching playerType — ALWAYS report developed.
            // Originally we fell through to the vanilla bit-array
            // (respecting R&D state controlled by
            // `develop.flow.initialAvailable`), but this broke supply-
            // drop equip: post-pickup the game checks IsEquipDeveloped
            // and rolls back to the previous vanilla suit if false.
            // Reporting true unconditionally for registered outfits
            // matches the modder intent — if the outfit is registered,
            // it should be wearable. R&D gating for custom outfits
            // can be reintroduced later via an explicit Lua flag if
            // anyone needs it.
            return 1;
        }

        return g_OrigIsEquipDeveloped(self, flowIndex);
    }

    static std::uint64_t __fastcall hkGetEquipIdFromLoadoutInfo(
        void* self, std::uint32_t slot)
    {
        const std::uint64_t orig = g_OrigGetEquipIdFromLoadoutInfo(self, slot);

        if (slot != kSlotUniforms) return orig;

        const std::uint8_t pt = outfit::ReadLivePartsType();
        if (pt < outfit::kCustomPartsTypeStart || pt > outfit::kCustomPartsTypeEnd)
            return orig;

        const outfit::OutfitEntry* entry = nullptr;
        if (!outfit::TryGetOutfitByPartsType(pt, &entry) || !entry)
            return orig;

        // Confirm this entry actually matches the live player —
        // protects against transient state mismatch during character
        // switch.
        const std::uint8_t livePlayer = outfit::ReadLivePlayerType();
        if (livePlayer != 0xFF && entry->playerType != livePlayer)
            return orig;

        return static_cast<std::uint64_t>(entry->flowIndex);
    }
}

namespace outfit
{
    bool Install_OutfitEquippedState_Hooks()
    {
        void* tDeveloped = ResolveGameAddress(gAddr.IsEquipDeveloped);
        void* tGetEquip  = ResolveGameAddress(gAddr.GetEquipIdFromLoadoutInfo);

        if (tDeveloped)
        {
            g_InstalledIsDeveloped = CreateAndEnableHook(
                tDeveloped,
                reinterpret_cast<void*>(&hkIsEquipDeveloped),
                reinterpret_cast<void**>(&g_OrigIsEquipDeveloped));
        }

        if (tGetEquip)
        {
            g_InstalledGetEquipId = CreateAndEnableHook(
                tGetEquip,
                reinterpret_cast<void*>(&hkGetEquipIdFromLoadoutInfo),
                reinterpret_cast<void**>(&g_OrigGetEquipIdFromLoadoutInfo));
        }

        Log("[OutfitEquippedState] installed: isDeveloped=%s getEquipId=%s\n",
            g_InstalledIsDeveloped ? "OK" : "skip",
            g_InstalledGetEquipId  ? "OK" : "skip");

        return g_InstalledIsDeveloped && g_InstalledGetEquipId;
    }

    void Uninstall_OutfitEquippedState_Hooks()
    {
        if (g_InstalledIsDeveloped)
            DisableAndRemoveHook(ResolveGameAddress(gAddr.IsEquipDeveloped));
        if (g_InstalledGetEquipId)
            DisableAndRemoveHook(ResolveGameAddress(gAddr.GetEquipIdFromLoadoutInfo));

        g_OrigIsEquipDeveloped         = nullptr;
        g_OrigGetEquipIdFromLoadoutInfo = nullptr;
        g_CachedEDC                    = nullptr;
        g_InstalledIsDeveloped = g_InstalledGetEquipId = false;

        Log("[OutfitEquippedState] removed\n");
    }

    void* GetCachedEquipDevelopController()
    {
        return g_CachedEDC;
    }
}
