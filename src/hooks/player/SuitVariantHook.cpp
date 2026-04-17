#include "pch.h"

#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"
#include "CustomSuitRegistry.h"
#include "SuitVariantHook.h"

namespace
{
    using GetQuarkSystemTable_t = void* (__fastcall*)();
    using AddListSuit_t = void(__fastcall*)(
        void* self, std::uint32_t* rowCounter, std::uint16_t suitId, void* param4);
    using IsEnableCurrentSuit_t = bool(__fastcall*)(void* self);
    using SetupEquipPanelParam_t = void(__fastcall*)(
        void* self, void* panelData, std::uint32_t slotIndex);

    // ---- Parked typedefs (HEAD OPTION cycling) ----
    using GetSuitVariation_t = std::uint8_t(__fastcall*)(void* self, std::uint16_t suitId);
    using HeadOptionTableLookup_t = std::uint64_t(__fastcall*)(
        void* self, std::int16_t* outIndex, std::uint64_t equipKey);
    using SetupCharacterSlotSelect_t = void(__fastcall*)(void* self);
    using GetSelectionNum_t = char(__fastcall*)(void* self);

    // ---- SetItemDetail typedef ----
    // Decompiled signature (from mgsvtpp.exe.c line 2967711):
    //   void MissionPreparationCallbackImpl::SetItemDetail(this, uint16 flowIndex)
    // Internally makes 3 SendTrigger calls via vtable+0x1e0 on this+0x38:
    //   1. (this+0x38, this+0x130, 0x8fda3dfc95ed, 0)     — mode=0
    //   2. (this+0x38, this+0x130, 0x30a0d543e155, flowIndex) — data
    //   3. (this+0x38, this+0x128, SET_KIND_HASH, 7)       — trigger
    using SetItemDetail_t = void(__fastcall*)(void* self, std::uint16_t flowIndex);

    // ---- Active state ----
    static GetQuarkSystemTable_t g_GetQuarkSystemTable = nullptr;
    static AddListSuit_t g_OrigAddListSuit = nullptr;
    static IsEnableCurrentSuit_t g_OrigIsEnableCurrentSuit = nullptr;
    static SetupEquipPanelParam_t g_OrigSetupEquipPanelParam = nullptr;
    static SetItemDetail_t g_OrigSetItemDetail = nullptr;
    static void* g_HookedAddListSuitAddr = nullptr;
    static void* g_HookedIsEnableCurrentSuitAddr = nullptr;
    static void* g_HookedSetupEquipPanelParamAddr = nullptr;
    static void* g_HookedSetItemDetailAddr = nullptr;
    static bool g_Installed = false;

    // Last vanilla flowIndex per playerType (0=Snake, 1=DD?, 2=Female, 3=?).
    static std::uint16_t s_vanillaFlowIndexByPt[4] = {};

    // Cached self and panel pointers.
    static void* s_cachedSelf = nullptr;
    static void* s_suitPanels[3] = {};

    // Set true when SetupEquipPanelParam fires (sortie context only).
    // Used by hkAddListSuit to prevent adding custom suits in the supply
    // drop selector, where FUN_1416a7610's table[flowIndex*0x68] access
    // goes out of bounds for custom flowIndices → infinite loading.
    static bool s_sortieContextActive = false;

    // ---- isDeveloped check hook (vtable+0x188 on loadout controller) ----
    using IsDeveloped_t = bool(__fastcall*)(void* self, std::uint8_t slotIndex);
    static IsDeveloped_t g_OrigIsDeveloped = nullptr;
    static void* g_HookedIsDevelopedAddr = nullptr;

    // ---- HEAD OPTION cycling state ----
    static GetSuitVariation_t g_OrigGetSuitVariation = nullptr;
    static HeadOptionTableLookup_t g_OrigHeadOptionTableLookup = nullptr;
    static SetupCharacterSlotSelect_t g_OrigSetupCharSlotSelect = nullptr;
    static GetSelectionNum_t g_OrigGetSelectionNum = nullptr;

    static bool ResolveApis()
    {
        if (!g_GetQuarkSystemTable)
        {
            g_GetQuarkSystemTable = reinterpret_cast<GetQuarkSystemTable_t>(
                ResolveGameAddress(gAddr.GetQuarkSystemTable));
        }
        return g_GetQuarkSystemTable != nullptr;
    }

    // Hook for GetCurrentSuitFlowIndex (FUN_140955c70, vtable+0x1F8 on sysObj+0x48).
    // This function returns the equipped suit's flowIndex. For custom suits it
    // returns 0x400 (blank sentinel), causing blank UNIFORMS panel and no EQP badge.
    // The fix: after the original returns, if the result is 0x400 and a custom suit
    // is active in Quark state, return the custom suit's linkedFlowIndex instead.
    using GetCurrentSuitFlowIndex_t = std::uint16_t(__fastcall*)(void* self);
    static GetCurrentSuitFlowIndex_t g_OrigGetCurrentSuitFlowIndex = nullptr;
    static void* g_HookedGetCurrentSuitFlowIndexAddr = nullptr;

    // Helper: read current playerType from Quark state[0xFB].
    static std::uint8_t ReadCurrentPlayerType()
    {
        if (!ResolveApis()) return 0xFF;
        auto* qt = reinterpret_cast<std::uint8_t*>(g_GetQuarkSystemTable());
        if (!qt) return 0xFF;
        auto* q98 = *reinterpret_cast<std::uint8_t**>(qt + 0x98);
        if (!q98) return 0xFF;
        auto* state = *reinterpret_cast<std::uint8_t**>(q98 + 0x10);
        if (!state) return 0xFF;
        return state[0xFB];
    }

    // Hook for vtable+0x188 (isDeveloped check on loadout controller).
    // Returns true for suit slots (0-2) when a custom suit is active,
    // so SetupEquipPanelParam runs ALL its display setup (icon, text, visibility).
    // Guard flag: only override isDeveloped when called from SetupEquipPanelParam.
    // vtable+0x188 is called from many game contexts — forcing true everywhere crashes.
    static bool s_inSetupEquipPanel = false;

    static bool __fastcall hkIsDeveloped(void* self, std::uint8_t slotIndex)
    {
        const bool result = g_OrigIsDeveloped(self, slotIndex);
        if (!result && slotIndex <= 2 && s_inSetupEquipPanel)
        {
            Log("[IsDeveloped] slot=%u forced true\n",
                static_cast<unsigned>(slotIndex));
            return true;
        }
        return result;
    }

    static std::uint16_t __fastcall hkGetCurrentSuitFlowIndex(void* self)
    {
        const std::uint16_t result = g_OrigGetCurrentSuitFlowIndex(self);

        // Save vanilla flowIndex per playerType for use by hkSetItemDetail.
        if (result != 0x400 && result != 0)
        {
            const std::uint8_t pt = ReadCurrentPlayerType();
            if (pt < 4)
            {
                if (s_vanillaFlowIndexByPt[pt] != result)
                    Log("[GetCurrentSuitFlowIndex] saved vanilla %u for pt=%u\n",
                        static_cast<unsigned>(result), static_cast<unsigned>(pt));
                s_vanillaFlowIndexByPt[pt] = result;
            }
        }

        if (result == 0x400 && ResolveApis())
        {
            auto* qt = reinterpret_cast<std::uint8_t*>(g_GetQuarkSystemTable());
            if (qt)
            {
                auto* q98 = *reinterpret_cast<std::uint8_t**>(qt + 0x98);
                if (q98)
                {
                    auto* state = *reinterpret_cast<std::uint8_t**>(q98 + 0x10);
                    if (state)
                    {
                        const std::uint8_t livePartsType = state[0xF8];
                        const CustomSuitEntry* entry = nullptr;
                        if (TryGetCustomSuitByPartsType(livePartsType, &entry) &&
                            entry && entry->linkedFlowIndex != 0xFFFF)
                        {
                            return entry->linkedFlowIndex;
                        }
                    }
                }
            }
        }

        return result;
    }

    // Hook for IsEnableCurrentSuit — returns true when a custom suit is
    // equipped so the EQP badge and suit panel info display correctly.
    // The original checks vtable+0x478(flowIndex) which fails for custom suits.
    static bool __fastcall hkIsEnableCurrentSuit(void* self)
    {
        // Check if live Quark state has a custom suit for the current playerType
        if (ResolveApis())
        {
            auto* qt = reinterpret_cast<std::uint8_t*>(g_GetQuarkSystemTable());
            if (qt)
            {
                auto* q98 = *reinterpret_cast<std::uint8_t**>(qt + 0x98);
                if (q98)
                {
                    auto* state = *reinterpret_cast<std::uint8_t**>(q98 + 0x10);
                    if (state)
                    {
                        const std::uint8_t livePartsType = state[0xF8];
                        const CustomSuitEntry* entry = nullptr;
                        if (TryGetCustomSuitByPartsType(livePartsType, &entry) && entry)
                            return true;
                    }
                }
            }
        }

        return g_OrigIsEnableCurrentSuit(self);
    }

    // Hook for SetupEquipPanelParam — fixes blank UNIFORMS panel for custom suits.
    // The original checks vtable+0x188(slotIndex) on the loadout controller,
    // which returns false for custom suits → skips icon/name display.
    // After the original runs, if a custom suit is live and the panel is blank,
    // force the "isDeveloped" flag so the game populates the display.
    //
    // Panel layout (EquipPanelInfo at panelData):
    //   +0x178 = equipId (uint16) — the suit's flowIndex
    //   +0x17a = displayId (uint16) — resolved icon/name key
    //   +0x17c = isDeveloped (byte) — controls whether details show
    static void __fastcall hkSetupEquipPanelParam(
        void* self, void* panelData, std::uint32_t slotIndex)
    {
        s_sortieContextActive = true;  // we're in sortie prep
        s_inSetupEquipPanel = true;
        g_OrigSetupEquipPanelParam(self, panelData, slotIndex);
        s_inSetupEquipPanel = false;

        if (!panelData)
            return;

        auto* panel = reinterpret_cast<std::uint8_t*>(panelData);
        const std::uint16_t equipId = *reinterpret_cast<std::uint16_t*>(panel + 0x178);
        const std::uint16_t displayId = *reinterpret_cast<std::uint16_t*>(panel + 0x17a);
        const std::uint8_t isDeveloped = panel[0x17c];

        Log("[SetupEquipPanel] slot=%u equipId=%u displayId=0x%04X isDev=%u\n",
            slotIndex, static_cast<unsigned>(equipId),
            static_cast<unsigned>(displayId), static_cast<unsigned>(isDeveloped));

        // Only fix suit slots (0, 1, 2)
        if (slotIndex > 2)
            return;

        // Cache self and panel for re-triggering from hkSetItemDetail
        s_cachedSelf = self;
        s_suitPanels[slotIndex] = panelData;

        // One-time: capture and hook vtable+0x188 (isDeveloped check) on the
        // loadout controller at self+0xa0.  hkIsDeveloped returns true for suit
        // slots when a custom suit is active, so the ORIGINAL SetupEquipPanelParam
        // runs all its display setup (icon, text, visibility) correctly.
        if (!g_HookedIsDevelopedAddr)
        {
            auto* selfBytes = reinterpret_cast<std::uint8_t*>(self);
            auto* loadout = *reinterpret_cast<void**>(selfBytes + 0xa0);
            if (loadout)
            {
                auto** vtable = *reinterpret_cast<void***>(loadout);
                void* fnAddr = vtable[0x188 / 8];
                if (fnAddr)
                {
                    const bool ok = CreateAndEnableHook(
                        fnAddr,
                        reinterpret_cast<void*>(&hkIsDeveloped),
                        reinterpret_cast<void**>(&g_OrigIsDeveloped));
                    if (ok)
                    {
                        g_HookedIsDevelopedAddr = fnAddr;
                        Log("[Hook] SuitVariant: Hooked IsDeveloped at %p "
                            "(vtable+0x188 on loadout %p)\n", fnAddr, loadout);
                    }
                }
            }
        }

        // Fix grade/category check: the original function's final check calls
        // vtable+0x500(this+0x68) which reads the CURRENT equipment grade.
        // For custom suits it returns 0 (no grade entry), which falls through
        // to the default → vtable+0x2a8(uixUtil, panel+0x100, 0) → HIDE.
        // This hides the element AFTER the icon/text were already set up.
        // Re-show it here by calling vtable+0x2a8 with 1.
        if (isDeveloped != 0)
        {
            bool customActive = false;
            if (ResolveApis())
            {
                auto* qt = reinterpret_cast<std::uint8_t*>(g_GetQuarkSystemTable());
                if (qt)
                {
                    auto* q98 = *reinterpret_cast<std::uint8_t**>(qt + 0x98);
                    if (q98)
                    {
                        auto* state = *reinterpret_cast<std::uint8_t**>(q98 + 0x10);
                        if (state)
                        {
                            const CustomSuitEntry* e = nullptr;
                            if (TryGetCustomSuitByPartsType(state[0xF8], &e) && e)
                                customActive = true;
                        }
                    }
                }
            }

            if (customActive)
            {
                auto* selfBytes = reinterpret_cast<std::uint8_t*>(self);
                auto* uixUtil = *reinterpret_cast<void**>(selfBytes + 0x38);
                auto* gradeElem = *reinterpret_cast<void**>(panel + 0x100);

                if (uixUtil && gradeElem)
                {
                    auto** uixVt = *reinterpret_cast<void***>(uixUtil);
                    using ShowHide_t = void(__fastcall*)(void*, void*, int);
                    auto showHide = reinterpret_cast<ShowHide_t>(uixVt[0x2a8 / 8]);
                    showHide(uixUtil, gradeElem, 1);
                    Log("[SetupEquipPanel] forced grade show for slot=%u\n",
                        slotIndex);
                }
                else
                {
                    Log("[SetupEquipPanel] grade fix: uix=%p elem=%p for slot=%u\n",
                        uixUtil, gradeElem, slotIndex);
                }
            }
        }
    }

    // Hook for SetItemDetail — diagnostic passthrough.
    // The UNIFORMS display fix is now in hkSetupEquipPanelParam (visibility fix).
    // SetItemDetail still fires for dynamic updates when the user changes suits.
    // For custom suits, remap to the last vanilla flowIndex so mode 0 can resolve.
    static void __fastcall hkSetItemDetail(void* self, std::uint16_t flowIndex)
    {
        const CustomSuitEntry* entry = nullptr;
        if (TryGetCustomSuitByFlowIndex(flowIndex, &entry) && entry)
        {
            const std::uint8_t pt = ReadCurrentPlayerType();
            const std::uint16_t vanillaId = (pt < 4) ? s_vanillaFlowIndexByPt[pt] : 0;

            if (vanillaId != 0)
            {
                Log("[SetItemDetail] remap custom %u -> vanilla %u (pt=%u)\n",
                    static_cast<unsigned>(flowIndex),
                    static_cast<unsigned>(vanillaId),
                    static_cast<unsigned>(pt));
                g_OrigSetItemDetail(self, vanillaId);
                return;
            }
        }

        g_OrigSetItemDetail(self, flowIndex);
    }

    // Hook for GetSelectionNum (0x1416bc2c0).
    // Returns: 1 = single row, 2 = two rows (no head option), 3 = three rows (has head option).
    // The original calls vtable+0x460 (GetSuitVariation) on the system object but
    // that vtable entry points to a different concrete function than what we hooked.
    // Fix: override the return value directly for custom suits with IsFaceEnabled.
    static char __fastcall hkGetSelectionNum(void* self)
    {
        const char result = g_OrigGetSelectionNum(self);

        // Only upgrade 2→3 (add HEAD OPTION row). Don't touch other values.
        if (result == 2)
        {
            // Check if a custom suit with face support is currently equipped
            if (ResolveApis())
            {
                auto* qt = reinterpret_cast<std::uint8_t*>(g_GetQuarkSystemTable());
                if (qt)
                {
                    auto* q98 = *reinterpret_cast<std::uint8_t**>(qt + 0x98);
                    if (q98)
                    {
                        auto* state = *reinterpret_cast<std::uint8_t**>(q98 + 0x10);
                        if (state)
                        {
                            const std::uint8_t livePartsType = state[0xF8];
                            const CustomSuitEntry* entry = nullptr;
                            if (TryGetCustomSuitByPartsType(livePartsType, &entry) &&
                                entry && entry->IsFaceEnabled())
                            {
                                Log("[GetSelectionNum] forced 2->3 for partsType=0x%02X\n",
                                    static_cast<unsigned>(livePartsType));
                                return 3;
                            }
                        }
                    }
                }
            }
        }

        return result;
    }

    // Our replacement for GetSuitVariation (vtable+0x460).
    // Returns: bit 0 = has scarf variant, bit 1 = has naked variant.
    // Returns non-zero for custom suits with variant groups.
    //
    // IMPORTANT: The second parameter `suitId` is the suit equipId that
    // GetSelectionNum passes when checking whether the *browsed* suit
    // (not the currently equipped one) has body variants.
    // Previously this was ignored, reading the equipped suit via vtable+0x608,
    // which meant HEAD OPTION never appeared while browsing a custom suit
    // with a vanilla suit equipped.
    static std::uint8_t __fastcall hkGetSuitVariation(void* self, std::uint16_t suitId)
    {
        // Try flowIndex first (GetSelectionNum passes the flowIndex from
        // GetCurrentSuitFlowIndex), then developId, partsType, selectorCode.
        const CustomSuitEntry* entry = nullptr;
        if (!TryGetCustomSuitByFlowIndex(suitId, &entry) || !entry)
        {
            if (!TryGetCustomSuitByDevelopId(suitId, &entry) || !entry)
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

        // If original found it, use vanilla result
        if (result & 0xFF)
            return result;

        // Match equipKey against a registered custom suit. Only force return 1
        // when the key itself corresponds to a custom suit with face enabled —
        // we must NOT force it for unrelated keys, otherwise the game treats
        // every vanilla suit as having a head option entry.
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
            if (outIndex)
                *outIndex = 0;
            return 1;
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
        // suitId in AddListSuit is the FLOW INDEX (p50), not developId (p00).
        // Per-playerType filtering: if this is a custom suit and the current
        // playerType doesn't match, skip it entirely (don't add to menu).
        const CustomSuitEntry* entry = nullptr;
        if (TryGetCustomSuitByFlowIndex(suitId, &entry) && entry)
        {
            // Read current playerType from Quark live state
            std::uint8_t currentPlayerType = 0xFF;
            if (ResolveApis())
            {
                auto* qt = reinterpret_cast<std::uint8_t*>(g_GetQuarkSystemTable());
                if (qt)
                {
                    auto* q98 = *reinterpret_cast<std::uint8_t**>(qt + 0x98);
                    if (q98)
                    {
                        auto* state = *reinterpret_cast<std::uint8_t**>(q98 + 0x10);
                        if (state)
                            currentPlayerType = state[0xFB];
                    }
                }
            }

            if (currentPlayerType != 0xFF && entry->playerType != currentPlayerType)
            {
                Log("[AddListSuit] filtered suitId=%u playerType=%u (current=%u)\n",
                    static_cast<unsigned>(suitId),
                    static_cast<unsigned>(entry->playerType),
                    static_cast<unsigned>(currentPlayerType));
                return; // Don't add to menu
            }
        }

        // Call original
        g_OrigAddListSuit(self, rowCounter, suitId, param4);

        // TODO: EQP badge for custom suits. The badge is determined at render
        // time by comparing vtable+0x1f8() (returns 0x400 for custom suits)
        // with each entry's flowIndex. Fixing this requires hooking
        // vtable+0x1f8 to return the custom suit's flowIndex.
    }

    // Hook for SetupCharacterSlotSelectPrefabListElement (0x1416bf490).
    // After the original runs, force bVar14=1 on the head option UI element
    // so BALACLAVA/HEADGEAR options are available for cycling.
    // The original only sets bVar14=1 for vanilla suits in the head option table.
    // Forced for custom suits with enableHead.
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

    // ---- HEAD OPTION cycling hooks ----

    // Hook GetSelectionNum — force 3 (has head option) for custom suits with face
    if (gAddr.MissionPrep_GetSelectionNum != 0)
    {
        void* addr = ResolveGameAddress(gAddr.MissionPrep_GetSelectionNum);
        if (addr)
        {
            const bool ok = CreateAndEnableHook(
                addr,
                reinterpret_cast<void*>(&hkGetSelectionNum),
                reinterpret_cast<void**>(&g_OrigGetSelectionNum));
            if (ok)
                Log("[Hook] SuitVariant: Hooked GetSelectionNum at %p\n", addr);
        }
    }

    // Hook GetSuitVariation (vtable+0x460) — returns bits for head/body variants
    if (gAddr.GetSuitVariation != 0)
    {
        void* addr = ResolveGameAddress(gAddr.GetSuitVariation);
        if (addr)
        {
            const bool ok = CreateAndEnableHook(
                addr,
                reinterpret_cast<void*>(&hkGetSuitVariation),
                reinterpret_cast<void**>(&g_OrigGetSuitVariation));
            if (ok)
                Log("[Hook] SuitVariant: Hooked GetSuitVariation at %p\n", addr);
        }
    }

    // Hook HeadOptionTableLookup (FUN_1460af810) — force match for custom suits
    if (gAddr.HeadOptionTableLookup != 0)
    {
        void* addr = ResolveGameAddress(gAddr.HeadOptionTableLookup);
        if (addr)
        {
            const bool ok = CreateAndEnableHook(
                addr,
                reinterpret_cast<void*>(&hkHeadOptionTableLookup),
                reinterpret_cast<void**>(&g_OrigHeadOptionTableLookup));
            if (ok)
                Log("[Hook] SuitVariant: Hooked HeadOptionTableLookup at %p\n", addr);
        }
    }

    // Hook SetupCharacterSlotSelectPrefabListElement — force bVar14 for cycling
    if (gAddr.SetupCharacterSlotSelectPrefabListElement != 0)
    {
        void* addr = ResolveGameAddress(gAddr.SetupCharacterSlotSelectPrefabListElement);
        if (addr)
        {
            const bool ok = CreateAndEnableHook(
                addr,
                reinterpret_cast<void*>(&hkSetupCharacterSlotSelect),
                reinterpret_cast<void**>(&g_OrigSetupCharSlotSelect));
            if (ok)
                Log("[Hook] SuitVariant: Hooked SetupCharSlotSelect at %p\n", addr);
        }
    }

    // Hook AddListSuit — per-playerType filtering + head option entries
    if (gAddr.AddListSuit != 0)
    {
        void* addListAddr = ResolveGameAddress(gAddr.AddListSuit);
        if (addListAddr)
        {
            const bool ok = CreateAndEnableHook(
                addListAddr,
                reinterpret_cast<void*>(&hkAddListSuit),
                reinterpret_cast<void**>(&g_OrigAddListSuit)
            );

            if (ok)
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

    // Hook GetCurrentSuitFlowIndex — fixes EQP badge + UNIFORMS panel
    if (gAddr.GetCurrentSuitFlowIndex != 0)
    {
        void* addr = ResolveGameAddress(gAddr.GetCurrentSuitFlowIndex);
        if (addr)
        {
            const bool ok = CreateAndEnableHook(
                addr,
                reinterpret_cast<void*>(&hkGetCurrentSuitFlowIndex),
                reinterpret_cast<void**>(&g_OrigGetCurrentSuitFlowIndex)
            );
            if (ok)
            {
                g_HookedGetCurrentSuitFlowIndexAddr = addr;
                Log("[Hook] SuitVariant: Hooked GetCurrentSuitFlowIndex at %p\n", addr);
            }
        }
    }

    // Hook IsEnableCurrentSuit — EQP badge + suit panel for custom suits
    if (gAddr.IsEnableCurrentSuit != 0)
    {
        void* addr = ResolveGameAddress(gAddr.IsEnableCurrentSuit);
        if (addr)
        {
            const bool ok = CreateAndEnableHook(
                addr,
                reinterpret_cast<void*>(&hkIsEnableCurrentSuit),
                reinterpret_cast<void**>(&g_OrigIsEnableCurrentSuit)
            );

            if (ok)
            {
                g_HookedIsEnableCurrentSuitAddr = addr;
                Log("[Hook] SuitVariant: Hooked IsEnableCurrentSuit at %p\n", addr);
            }
        }
    }

    // Hook SetItemDetail — remap custom flowIndex to vanilla for sortie display
    if (gAddr.SetItemDetail != 0)
    {
        void* addr = ResolveGameAddress(gAddr.SetItemDetail);
        if (addr)
        {
            const bool ok = CreateAndEnableHook(
                addr,
                reinterpret_cast<void*>(&hkSetItemDetail),
                reinterpret_cast<void**>(&g_OrigSetItemDetail)
            );
            if (ok)
            {
                g_HookedSetItemDetailAddr = addr;
                Log("[Hook] SuitVariant: Hooked SetItemDetail at %p\n", addr);
            }
            else
            {
                Log("[Hook] SuitVariant: MinHook failed for SetItemDetail at %p\n", addr);
            }
        }
    }

    // Hook SetupEquipPanelParam — fix blank UNIFORMS panel for custom suits
    if (gAddr.SetupEquipPanelParam != 0)
    {
        void* addr = ResolveGameAddress(gAddr.SetupEquipPanelParam);
        if (addr)
        {
            const bool ok = CreateAndEnableHook(
                addr,
                reinterpret_cast<void*>(&hkSetupEquipPanelParam),
                reinterpret_cast<void**>(&g_OrigSetupEquipPanelParam)
            );

            if (ok)
            {
                g_HookedSetupEquipPanelParamAddr = addr;
                Log("[Hook] SuitVariant: Hooked SetupEquipPanelParam at %p\n", addr);
            }
        }
    }

    g_Installed = (g_HookedAddListSuitAddr != nullptr) ||
                  (g_HookedIsEnableCurrentSuitAddr != nullptr) ||
                  (g_HookedSetupEquipPanelParamAddr != nullptr) ||
                  (g_HookedSetItemDetailAddr != nullptr);
    Log("[Hook] SuitVariant: %s\n", g_Installed ? "OK" : "no hooks installed");
    return true;
}

bool Uninstall_SuitVariant_Hooks()
{
    if (!g_Installed)
        return true;

    if (g_HookedAddListSuitAddr)
    {
        DisableAndRemoveHook(g_HookedAddListSuitAddr);
        g_HookedAddListSuitAddr = nullptr;
    }

    if (g_HookedIsEnableCurrentSuitAddr)
    {
        DisableAndRemoveHook(g_HookedIsEnableCurrentSuitAddr);
        g_HookedIsEnableCurrentSuitAddr = nullptr;
    }

    if (g_HookedSetupEquipPanelParamAddr)
    {
        DisableAndRemoveHook(g_HookedSetupEquipPanelParamAddr);
        g_HookedSetupEquipPanelParamAddr = nullptr;
    }

    if (g_HookedSetItemDetailAddr)
    {
        DisableAndRemoveHook(g_HookedSetItemDetailAddr);
        g_HookedSetItemDetailAddr = nullptr;
    }

    if (g_HookedGetCurrentSuitFlowIndexAddr)
    {
        DisableAndRemoveHook(g_HookedGetCurrentSuitFlowIndexAddr);
        g_HookedGetCurrentSuitFlowIndexAddr = nullptr;
    }

    g_OrigAddListSuit = nullptr;
    g_OrigIsEnableCurrentSuit = nullptr;
    g_OrigSetupEquipPanelParam = nullptr;
    g_OrigSetItemDetail = nullptr;
    g_OrigGetCurrentSuitFlowIndex = nullptr;
    if (g_HookedIsDevelopedAddr)
    {
        DisableAndRemoveHook(g_HookedIsDevelopedAddr);
        g_HookedIsDevelopedAddr = nullptr;
    }
    g_OrigIsDeveloped = nullptr;
    s_vanillaFlowIndexByPt[0] = s_vanillaFlowIndexByPt[1] = 0;
    s_vanillaFlowIndexByPt[2] = s_vanillaFlowIndexByPt[3] = 0;
    s_cachedSelf = nullptr;
    s_suitPanels[0] = s_suitPanels[1] = s_suitPanels[2] = nullptr;
    g_Installed = false;

    Log("[Hook] SuitVariant: removed\n");
    return true;
}
