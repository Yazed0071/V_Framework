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

        // PLAYERTYPE GATE REMOVED 2026-04-28 — previously this hook
        // returned 0 for custom outfits whose playerType didn't match
        // the live character (e.g. a Female-only outfit when playing
        // as Snake). Intent was to keep PT-mismatched outfits out of
        // the UNIFORMS panel, but the implementation lied about the
        // actual develop bit. Side-effect: the R&D screen also calls
        // IsEquipDeveloped while building its tile counts; the
        // playerType lie made researched-but-PT-mismatched outfits
        // appear as "still to develop" → R&D refresh asymmetry where
        // a Female outfit looked undeveloped while playing as Male
        // until UNIFORMS opened (which has its own injection logic
        // that didn't go through this hook).
        //
        // The develop bit IS a fact: the player either researched the
        // outfit or didn't. UNIFORMS panel visibility filtering by live
        // playerType is a separate concern, correctly handled at the
        // injection layer in `ShouldInjectOutfit` (which still gates on
        // `e->playerType != livePT`). So removing the gate here:
        //   - R&D reads truthful developed status (fixes the asymmetry)
        //   - UNIFORMS panel still hides PT-mismatched outfits because
        //     `ShouldInjectOutfit` returns false for them, so they're
        //     never added to the panel's flowIndex array in the first
        //     place — the per-row IsEquipDeveloped check inside the
        //     panel walker isn't reached for them.
        //   - Supply-drop catalog also reads truthful state, which is
        //     fine: the player can only drop outfits they've already
        //     researched (vanilla R&D semantics).
        //
        // We still bound-check flowIndex >= 0x400 above to keep the
        // orig's unchecked bit-array read in-bounds.
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

    bool IsFlowIndexDevelopedByOrig(unsigned short flowIndex)
    {
        if (!g_CachedEDC || !g_OrigIsEquipDeveloped) return false;
        // EDC bit-array is sized for flowIndex < 0x400. Anything
        // above that would OOB the orig's own bit lookup.
        if (flowIndex >= 0x400) return false;
        return g_OrigIsEquipDeveloped(g_CachedEDC, flowIndex) != 0;
    }
}
