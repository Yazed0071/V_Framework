#include "pch.h"

#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"
#include "CustomSuitRegistry.h"
#include "SuitVariantHook.h"

namespace
{
    using GetQuarkSystemTable_t = void* (__fastcall*)();
    using GetSuitVariation_t = std::uint8_t(__fastcall*)(void* self, std::uint16_t suitId);
    using HeadOptionTableLookup_t = std::uint64_t(__fastcall*)(
        void* self, std::int16_t* outIndex, std::uint64_t equipKey);
    using SetupCharacterSlotSelect_t = void(__fastcall*)(void* self);
    using AddListSuit_t = void(__fastcall*)(
        void* self, std::uint32_t* rowCounter, std::uint16_t suitId, void* param4);

    static GetQuarkSystemTable_t g_GetQuarkSystemTable = nullptr;
    static GetSuitVariation_t g_OrigGetSuitVariation = nullptr;
    static HeadOptionTableLookup_t g_OrigHeadOptionTableLookup = nullptr;
    static SetupCharacterSlotSelect_t g_OrigSetupCharSlotSelect = nullptr;
    static AddListSuit_t g_OrigAddListSuit = nullptr;
    static void* g_HookedFuncAddr = nullptr;
    static void* g_HookedHeadOptionAddr = nullptr;
    static void* g_HookedSetupCharSlotAddr = nullptr;
    static void* g_HookedAddListSuitAddr = nullptr;
    static bool g_Installed = false;

    static bool ResolveApis()
    {
        if (!g_GetQuarkSystemTable)
        {
            g_GetQuarkSystemTable = reinterpret_cast<GetQuarkSystemTable_t>(
                ResolveGameAddress(gAddr.GetQuarkSystemTable));
        }
        return g_GetQuarkSystemTable != nullptr;
    }

    // Our replacement for GetSuitVariation (vtable+0x460).
    // Returns: bit 0 = has scarf variant, bit 1 = has naked variant.
    // For custom suits with variant groups, we return non-zero.
    //
    // IMPORTANT: The second parameter `suitId` is the suit equipId that
    // GetSelectionNum passes when checking whether the *browsed* suit
    // (not the currently equipped one) has body variants.
    // Previously we ignored it and read the equipped suit via vtable+0x608,
    // which meant HEAD OPTION never appeared while browsing a custom suit
    // with a vanilla suit equipped.
    static std::uint8_t __fastcall hkGetSuitVariation(void* self, std::uint16_t suitId)
    {
        // Try to find by developId, then by partsType, then by selectorCode
        const CustomSuitEntry* entry = nullptr;
        if (!TryGetCustomSuitByDevelopId(suitId, &entry) || !entry)
        {
            TryGetCustomSuitByPartsType(static_cast<std::uint8_t>(suitId & 0xFF), &entry);
        }
        if (!entry)
        {
            TryGetCustomSuitBySelectorCode(static_cast<std::uint8_t>(suitId & 0xFF), &entry);
        }

        if (entry)
        {
            std::uint8_t bits = 0;

            // HEAD OPTION slot: show when enableHead is true (IsFaceEnabled)
            // This lets the player choose NONE/BALACLAVA/HEADGEAR for the suit.
            if (entry->IsFaceEnabled())
                bits |= 1;

            // Body variants: show when the suit has a variant group with multiple entries
            if (entry->HasVariantGroup())
            {
                const std::size_t groupSize = GetVariantGroupSize(entry->variantGroupId);
                if (groupSize >= 2) bits |= 1;
                if (groupSize >= 3) bits |= 2;
            }

            if (bits != 0)
            {
                Log("[SuitVariant] custom suitId=%u -> bits=0x%02X\n",
                    static_cast<unsigned>(suitId), static_cast<unsigned>(bits));
                return bits;
            }
        }

        // Fall through to original for vanilla suits
        if (g_OrigGetSuitVariation)
            return g_OrigGetSuitVariation(self, suitId);

        return 0;
    }

    // Hook for the HEAD OPTION table lookup (FUN_1460af810).
    // Called during list building for each suit. equipKey identifies the suit.
    // If the original returns 0 (suit not in head option table) and the suit
    // is a custom suit with IsFaceEnabled(), force return 1.
    static std::uint64_t __fastcall hkHeadOptionTableLookup(
        void* self, std::int16_t* outIndex, std::uint64_t equipKey)
    {
        const std::uint64_t result = g_OrigHeadOptionTableLookup(self, outIndex, equipKey);

        // Log every call to understand what equipKey values the game uses
        static std::uint64_t s_lastLoggedKey = ~0ULL;
        if (equipKey != s_lastLoggedKey)
        {
            Log("[HeadOptionTableLookup] equipKey=0x%llX (%llu) result=%llu outIndex=%d\n",
                static_cast<unsigned long long>(equipKey),
                static_cast<unsigned long long>(equipKey),
                static_cast<unsigned long long>(result),
                outIndex ? static_cast<int>(*outIndex) : -1);
            s_lastLoggedKey = equipKey;
        }

        // If original found it, use vanilla result
        if (result & 0xFF)
            return result;

        // Try to match equipKey against custom suit registrations.
        // The game passes equipKey as the suit's equipId (p00 developId).
        // Also try matching as partsType or selectorCode in case it's a
        // smaller value.
        const CustomSuitEntry* entry = nullptr;

        // Try as developId first (equipKey could be 51006, 51007, etc.)
        const std::uint16_t keyAsU16 = static_cast<std::uint16_t>(equipKey & 0xFFFF);
        if (!TryGetCustomSuitByDevelopId(keyAsU16, &entry) || !entry)
        {
            // Try as partsType
            TryGetCustomSuitByPartsType(static_cast<std::uint8_t>(equipKey & 0xFF), &entry);
        }
        if (!entry)
        {
            // Try as selectorCode
            TryGetCustomSuitBySelectorCode(static_cast<std::uint8_t>(equipKey & 0xFF), &entry);
        }

        if (entry && entry->IsFaceEnabled())
        {
            Log("[HeadOptionTableLookup] custom equipKey=0x%llX -> forced=1\n",
                static_cast<unsigned long long>(equipKey));
            if (outIndex)
                *outIndex = 0;
            return 1;
        }

        // Fallback: also check live Quark state in case we're in the
        // confirm/apply path where the suit is already set
        if (ResolveApis())
        {
            auto* quarkTable = reinterpret_cast<std::uint8_t*>(g_GetQuarkSystemTable());
            if (quarkTable)
            {
                auto* q98 = *reinterpret_cast<std::uint8_t**>(quarkTable + 0x98);
                if (q98)
                {
                    auto* state = *reinterpret_cast<std::uint8_t**>(q98 + 0x10);
                    if (state)
                    {
                        const std::uint8_t livePartsType = state[0xF8];
                        const CustomSuitEntry* liveEntry = nullptr;
                        if (TryGetCustomSuitByPartsType(livePartsType, &liveEntry) &&
                            liveEntry && liveEntry->IsFaceEnabled())
                        {
                            if (outIndex)
                                *outIndex = 0;
                            return 1;
                        }
                    }
                }
            }
        }

        return result;
    }

    // Hook for AddListSuit.
    // After the original runs, check if this is a custom suit with enableHead.
    // If so, modify the sub-slot entries to add head option cycling entries.
    //
    // Sub-slot layout:
    //   this + (row*0xF + subIndex) * 0xC + 0xcc40 = selectorCode (4 bytes)
    //   this + (row*0xF + subIndex) * 0xC + 0xcc44 = variantIndex (4 bytes)
    //   this + (row*0xF + subIndex) * 0xC + 0xcc48 = camoType (1 byte)
    //   this + row + 0xc040 = sub-count (1 byte)
    //   this + row + 0xbc40 = total count including sub (1 byte)
    static void __fastcall hkAddListSuit(
        void* self, std::uint32_t* rowCounter, std::uint16_t suitId, void* param4)
    {
        const std::uint32_t rowBefore = *rowCounter;

        // Log every call to see what suitId values the game passes
        static std::uint16_t s_lastSuitId = 0xFFFF;
        if (suitId != s_lastSuitId)
        {
            Log("[AddListSuit] suitId=%u (0x%04X) rowBefore=%u\n",
                static_cast<unsigned>(suitId), static_cast<unsigned>(suitId), rowBefore);
            s_lastSuitId = suitId;
        }

        // Call original
        g_OrigAddListSuit(self, rowCounter, suitId, param4);

        const std::uint32_t rowAfter = *rowCounter;

        // If the original didn't add a row, nothing to do
        if (rowAfter <= rowBefore)
            return;

        // The suit was added at row index = rowAfter - 1
        const std::uint32_t row = rowAfter - 1;

        // Check if this suitId belongs to a custom suit with enableHead
        const CustomSuitEntry* entry = nullptr;
        if (!TryGetCustomSuitByDevelopId(suitId, &entry) || !entry)
            return;

        if (!entry->IsFaceEnabled())
            return;

        // This custom suit has enableHead=true.
        // The original likely put it on the "weapon path" with only 2 sub-entries
        // (variant=7 at sub 0, variant=0 at sub 1, sub-count=1).
        // We need to add head option sub-entries so the user can cycle
        // NONE/BALACLAVA/HEADGEAR.
        //
        // For HEAD OPTION, the sub-slot variantIndex values map to faceEquipId:
        //   variantIndex 7 = NAKED variant (also used as "no headgear" = NONE)
        //   variantIndex 0 = NORMAL (with headgear = BALACLAVA)
        //   variantIndex 1 = SCARFED variant (HEADGEAR)
        //
        // We overwrite the sub-entries to have 3 entries with the proper variant indices.

        auto* base = reinterpret_cast<std::uint8_t*>(self);

        // Read what the original wrote for the selector code
        const std::uint32_t origSelector =
            *reinterpret_cast<std::uint32_t*>(base + (row * 0xFULL) * 0xC + 0xcc40);
        const std::uint8_t origCamoType =
            base[(row * 0xFULL) * 0xC + 0xcc48];

        // Sub 0: variant=7 (NAKED body / no headgear → HEAD OPTION: NONE)
        auto writeSubSlot = [&](std::uint32_t subIdx, std::uint32_t selector,
                                std::uint32_t variantIdx, std::uint8_t camo)
        {
            const std::uint64_t off = (row * 0xFULL + subIdx) * 0xC + 0xcc40;
            *reinterpret_cast<std::uint32_t*>(base + off + 0) = selector;
            *reinterpret_cast<std::uint32_t*>(base + off + 4) = variantIdx;
            base[off + 8] = camo;
        };

        // Write 3 sub-entries:
        writeSubSlot(0, origSelector, 7, origCamoType); // NONE (naked/no headgear)
        writeSubSlot(1, origSelector, 0, origCamoType); // BALACLAVA (normal)
        writeSubSlot(2, origSelector, 1, origCamoType); // HEADGEAR (scarfed)

        // Set sub-count to 2 (meaning max sub-index = 2 → 3 entries: 0,1,2)
        base[row + 0xc040] = 2;
        // bc40 stores total count including base
        base[row + 0xbc40] = 3;

        Log("[AddListSuit] custom suitId=%u row=%u: injected 3 head option sub-entries (selector=0x%X)\n",
            static_cast<unsigned>(suitId), row, origSelector);
    }

    // Hook for SetupCharacterSlotSelectPrefabListElement (0x1416bf490).
    // After the original runs, force bVar14=1 on the head option UI element
    // so BALACLAVA/HEADGEAR options are available for cycling.
    // The original only sets bVar14=1 for vanilla suits in the head option table.
    // For custom suits with enableHead, we force it.
    //
    // Layout: self+0x2b48 = pointer to first UI element (head option slot 0)
    //         element+0xb8 = bVar14 flag (controls head option cycling availability)
    static void __fastcall hkSetupCharacterSlotSelect(void* self)
    {
        g_OrigSetupCharSlotSelect(self);

        // Check if any custom suit with enableHead exists.
        // If so, force bVar14=1. This is safe because for suits WITHOUT head
        // options, GetSelectionNum returns 2 and the HEAD OPTION slot is hidden,
        // so the bVar14 flag has no effect.
        if (!HasAnyCustomSuitWithFaceEnabled())
            return;

        auto* selfBytes = reinterpret_cast<std::uint8_t*>(self);
        auto* element = *reinterpret_cast<void**>(selfBytes + 0x2b48);
        if (!element)
            return;

        auto* elemBytes = reinterpret_cast<std::uint8_t*>(element);
        const std::uint8_t oldVal = elemBytes[0xb8];
        elemBytes[0xb8] = 1;

        if (oldVal != 1)
        {
            Log("[SetupCharSlotSelect] forced bVar14: %u -> 1\n",
                static_cast<unsigned>(oldVal));
        }
    }

}

bool Install_SuitVariant_Hooks()
{
    if (g_Installed)
    {
        Log("[Hook] SuitVariant: already installed\n");
        return true;
    }

    // Hook GetSuitVariation by resolved address
    void* funcAddr = (gAddr.GetSuitVariation != 0)
        ? ResolveGameAddress(gAddr.GetSuitVariation)
        : nullptr;

    if (funcAddr)
    {
        const bool ok = CreateAndEnableHook(
            funcAddr,
            reinterpret_cast<void*>(&hkGetSuitVariation),
            reinterpret_cast<void**>(&g_OrigGetSuitVariation)
        );

        if (ok)
        {
            Log("[Hook] SuitVariant: Hooked GetSuitVariation at %p\n", funcAddr);
            g_HookedFuncAddr = funcAddr;
        }
        else
        {
            Log("[Hook] SuitVariant: MinHook failed for GetSuitVariation at %p\n", funcAddr);
        }
    }

    // Hook HeadOptionTableLookup (FUN_1460af810)
    if (gAddr.HeadOptionTableLookup != 0)
    {
        void* headOptAddr = ResolveGameAddress(gAddr.HeadOptionTableLookup);
        if (headOptAddr)
        {
            const bool okHead = CreateAndEnableHook(
                headOptAddr,
                reinterpret_cast<void*>(&hkHeadOptionTableLookup),
                reinterpret_cast<void**>(&g_OrigHeadOptionTableLookup)
            );

            if (okHead)
            {
                g_HookedHeadOptionAddr = headOptAddr;
                Log("[Hook] SuitVariant: Hooked HeadOptionTableLookup at %p\n", headOptAddr);
            }
            else
            {
                Log("[Hook] SuitVariant: MinHook failed for HeadOptionTableLookup at %p\n", headOptAddr);
            }
        }
    }

    // Hook SetupCharacterSlotSelectPrefabListElement to force bVar14=1
    if (gAddr.SetupCharacterSlotSelectPrefabListElement != 0)
    {
        void* setupAddr = ResolveGameAddress(gAddr.SetupCharacterSlotSelectPrefabListElement);
        if (setupAddr)
        {
            const bool okSetup = CreateAndEnableHook(
                setupAddr,
                reinterpret_cast<void*>(&hkSetupCharacterSlotSelect),
                reinterpret_cast<void**>(&g_OrigSetupCharSlotSelect)
            );

            if (okSetup)
            {
                g_HookedSetupCharSlotAddr = setupAddr;
                Log("[Hook] SuitVariant: Hooked SetupCharSlotSelect at %p\n", setupAddr);
            }
            else
            {
                Log("[Hook] SuitVariant: MinHook failed for SetupCharSlotSelect at %p\n", setupAddr);
            }
        }
    }

    // Hook AddListSuit to inject head option sub-entries for custom suits
    if (gAddr.AddListSuit != 0)
    {
        void* addListAddr = ResolveGameAddress(gAddr.AddListSuit);
        if (addListAddr)
        {
            const bool okAdd = CreateAndEnableHook(
                addListAddr,
                reinterpret_cast<void*>(&hkAddListSuit),
                reinterpret_cast<void**>(&g_OrigAddListSuit)
            );

            if (okAdd)
            {
                g_HookedAddListSuitAddr = addListAddr;
                Log("[Hook] SuitVariant: Hooked AddListSuit at %p\n", addListAddr);
            }
            else
            {
                Log("[Hook] SuitVariant: MinHook failed for AddListSuit at %p\n", addListAddr);
            }
        }
    }

    g_Installed = (g_HookedFuncAddr != nullptr) || (g_HookedHeadOptionAddr != nullptr) ||
                  (g_HookedSetupCharSlotAddr != nullptr) || (g_HookedAddListSuitAddr != nullptr);
    Log("[Hook] SuitVariant: %s\n", g_Installed ? "OK" : "no hooks installed");
    return true;
}

bool Uninstall_SuitVariant_Hooks()
{
    if (!g_Installed)
        return true;

    if (g_HookedFuncAddr)
    {
        DisableAndRemoveHook(g_HookedFuncAddr);
        g_HookedFuncAddr = nullptr;
    }

    if (g_HookedHeadOptionAddr)
    {
        DisableAndRemoveHook(g_HookedHeadOptionAddr);
        g_HookedHeadOptionAddr = nullptr;
    }

    if (g_HookedSetupCharSlotAddr)
    {
        DisableAndRemoveHook(g_HookedSetupCharSlotAddr);
        g_HookedSetupCharSlotAddr = nullptr;
    }

    if (g_HookedAddListSuitAddr)
    {
        DisableAndRemoveHook(g_HookedAddListSuitAddr);
        g_HookedAddListSuitAddr = nullptr;
    }

    g_OrigGetSuitVariation = nullptr;
    g_OrigSetupCharSlotSelect = nullptr;
    g_OrigAddListSuit = nullptr;
    g_Installed = false;

    Log("[Hook] SuitVariant: removed\n");
    return true;
}
