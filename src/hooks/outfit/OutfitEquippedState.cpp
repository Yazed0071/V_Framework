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

            // Respect the orig R&D bit-array (REVERTED 2026-04-27 from
            // "always return 1"). Modder controls visibility via
            // `develop.flow.initialAvailable` and player R&D research:
            //   - initialAvailable=1: bit set at register-time, outfit
            //     always available.
            //   - initialAvailable=0: bit set only after the player
            //     researches the outfit in MotherBase R&D, mirroring
            //     vanilla equip flow.
            // The previous "always 1" override hid this gate — it made
            // every registered outfit appear in the UNIFORMS panel
            // immediately, regardless of whether the player had
            // developed it. User report 2026-04-27: registering a
            // second outfit (FROGS) with initialAvailable=0 still
            // showed it in the panel; selecting it loaded a different
            // outfit because of cell/display mismatch fallout from
            // the orig walker handling an undeveloped flowIndex it
            // shouldn't have seen at all.
            //
            // Falling through to orig: if the user's R&D state has the
            // bit set, returns 1 (panel shows). Otherwise returns 0
            // (panel hides). The orig vtable[0x230]/[0x240] use the
            // same bit-array, so panel construction naturally excludes
            // undeveloped outfits — no synthetic injection needed for
            // them, and none of the variant-fold or row-collision
            // pathologies kick in.
            //
            // Supply-drop concern (post-pickup IsEquipDeveloped roll-
            // back) — historically cited as the reason for the always-1
            // override — only manifests if the player can drop an
            // un-researched outfit. Vanilla R&D semantics require
            // research first; with this revert the player must research
            // the outfit before it appears in the supply-drop catalog,
            // which means the post-pickup check naturally returns 1
            // (researched → bit set). No rollback.
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

    bool IsFlowIndexDevelopedByOrig(unsigned short flowIndex)
    {
        if (!g_CachedEDC || !g_OrigIsEquipDeveloped) return false;
        // EDC bit-array is sized for flowIndex < 0x400. Anything
        // above that would OOB the orig's own bit lookup.
        if (flowIndex >= 0x400) return false;
        return g_OrigIsEquipDeveloped(g_CachedEDC, flowIndex) != 0;
    }
}
