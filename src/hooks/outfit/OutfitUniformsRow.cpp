#include "pch.h"

#include "OutfitUniformsRow.h"
#include "OutfitRegistry.h"

#include <Windows.h>
#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    // Verified signatures from named-build:
    //   line 2897768: void ChangeDetailsWindowBuddySelect(longlong this, ulonglong index)
    //   line 2897989: void OpenBuddySelect(longlong this)
    // Both are CharacterSelectorCallbackImpl methods. Same UNIFORMS-row
    // write path; OpenBuddySelect computes the slot index internally
    // from the panel's scroll/cursor offsets, ChangeDetailsWindowBuddy-
    // Select takes it as RDX. We hook both — Open covers initial panel
    // build (entering the buddy-select panel) while Change covers cursor
    // moves between rows on the panel.
    using ChangeDetailsWindowBuddySelect_t =
        void (__fastcall*)(void* self, std::uint64_t index);

    using OpenBuddySelect_t =
        void (__fastcall*)(void* self);

    static ChangeDetailsWindowBuddySelect_t g_OrigChange = nullptr;
    static OpenBuddySelect_t                g_OrigOpen   = nullptr;

    static bool g_InstalledChange = false;
    static bool g_InstalledOpen   = false;

    // CharacterSelectorCallbackImpl field offsets (verified at line 2897777-2897790
    // for ChangeDetailsWindowBuddySelect and 2898010-2898020 for OpenBuddySelect):
    //   *(longlong*)(self + 0x38)               — child object (3-deref chain
    //                                              for plVar2 = ui setter,
    //                                              plVar3 = name translator)
    //   *(longlong*)(self + 0x9C70 + index*4)   — current partsType byte for
    //                                              that buddy slot (8-bit
    //                                              field, but the array is
    //                                              4-byte stride)
    //   self + 0x9FE0                           — Quark window context handle 1
    //                                              (uint64; passed as 2nd arg
    //                                              to setter calls 1 and 2)
    //   self + 0x9FD8                           — Quark window context handle 2
    //                                              (passed to setter call 3)
    //   self + 0xA0D0                           — output hash buffer the
    //                                              translator writes to and
    //                                              the setter then reads;
    //                                              what the user sees in
    //                                              the UNIFORMS row text
    //
    // OpenBuddySelect's slot-index computation (2898007-2898009):
    //   if (*(uint*)(self + 0x94) == 0)
    //       slot = 0;
    //   else
    //       slot = (*(int*)(self + 0xA0) + *(int*)(self + 0x9C))
    //              % *(uint*)(self + 0x94);
    //
    // Quark UI property hashes:
    //   0x30A0D543E155 = "uniformsText" (setter call 2 — the row body text)
    //   0x8FDA3DFC95ED = unused property (setter call 1 — likely visibility flag)
    //   DAT_142B16960  = unused property (setter call 3 — likely refresh flag)
    constexpr std::size_t kOff_ChildObject       = 0x38;
    constexpr std::size_t kOff_PartsTypeArray    = 0x9C70;  // [index*4] byte
    constexpr std::size_t kOff_QuarkWindowHandle = 0x9FE0;
    constexpr std::size_t kOff_HashOutBuffer     = 0xA0D0;

    // OpenBuddySelect slot-index source fields.
    constexpr std::size_t kOff_PanelRowCount    = 0x94;  // u32
    constexpr std::size_t kOff_PanelCursor      = 0x9C;  // i32
    constexpr std::size_t kOff_PanelScrollPage  = 0xA0;  // i32

    // Inside the +0x38 child object, plVar2 (UI setter) lives at +0x20.
    constexpr std::size_t kOff_ChildToUiSetter   = 0x20;

    // Quark UI vtable index for the property setter (+0x1E0 byte offset).
    constexpr std::size_t kVtbl_UiPropertySet    = 0x1E0;

    // The "uniformsText" property hash (the one orig passes for setter call 2).
    constexpr std::uint64_t kPropHash_UniformsText = 0x30A0D543E155ULL;

    // Quark UI setter signature (4-arg).
    using QuarkUiSetter_t =
        void (__fastcall*)(void* uiSetterSelf,
                           std::uint64_t windowHandle,
                           std::uint64_t propertyHash,
                           std::uint64_t valueOrPointer);

    // Shared override body: reads partsType for the given slot, checks
    // if it's a registered custom outfit, and if so overwrites the hash
    // buffer + re-invokes Quark's setter so the row text refreshes.
    // Wrapped in SEH because we follow several derefs into game state.
    // `siteTag` is for log lines so we know which path fired.
    static void TryOverrideUniformsRowForSlot(
        void* self, std::uint32_t slotIdx, const char* siteTag)
    {
        if (!self) return;

        __try
        {
            auto* base = reinterpret_cast<std::uint8_t*>(self);
            const std::uint8_t partsType =
                base[kOff_PartsTypeArray + slotIdx * 4];

            if (partsType < outfit::kCustomPartsTypeStart
             || partsType > outfit::kCustomPartsTypeEnd)
            {
                return;  // not a custom outfit; leave orig's output alone
            }

            const outfit::OutfitEntry* entry = nullptr;
            if (!outfit::TryGetOutfitByPartsType(partsType, &entry) || !entry)
                return;

            if (entry->langEquipNameHash == 0)
            {
                // No override registered for this outfit. Don't touch
                // anything — UNIFORMS row stays blank as orig left it.
                // The user can register a langEquipName in their
                // outfit's develop.const to provide a name.
                return;
            }

            // Overwrite the hash buffer and re-call the setter so the
            // displayed text refreshes from our hash.
            *reinterpret_cast<std::uint64_t*>(base + kOff_HashOutBuffer) =
                entry->langEquipNameHash;

            auto* childObj =
                *reinterpret_cast<std::uint8_t**>(base + kOff_ChildObject);
            if (!childObj) return;

            auto* uiSetterObj =
                *reinterpret_cast<std::uint8_t**>(childObj + kOff_ChildToUiSetter);
            if (!uiSetterObj) return;

            // vtable lookup: uiSetterObj's first qword = vtable pointer.
            // vtable[+0x1E0] (byte offset) is the property setter.
            auto* vtable = *reinterpret_cast<std::uint8_t**>(uiSetterObj);
            if (!vtable) return;

            const auto setter = *reinterpret_cast<QuarkUiSetter_t*>(
                vtable + kVtbl_UiPropertySet);
            if (!setter) return;

            const std::uint64_t windowHandle =
                *reinterpret_cast<std::uint64_t*>(base + kOff_QuarkWindowHandle);

            // Re-invoke the property setter for "uniformsText" with our
            // hash. The setter takes a POINTER to the hash buffer (orig
            // passes self+0xA0D0); we pass the same address since we
            // just wrote our hash there.
            setter(uiSetterObj,
                   windowHandle,
                   kPropHash_UniformsText,
                   reinterpret_cast<std::uint64_t>(base + kOff_HashOutBuffer));

            Log("[OutfitUniformsRow:%s] overrode UNIFORMS hash for "
                "partsType=0x%02X developId=%u slot=%u -> 0x%016llX\n",
                siteTag,
                static_cast<unsigned>(partsType),
                static_cast<unsigned>(entry->developId),
                static_cast<unsigned>(slotIdx),
                static_cast<unsigned long long>(entry->langEquipNameHash));
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            Log("[OutfitUniformsRow:%s] SEH while overriding UNIFORMS row "
                "(self=%p slot=%u)\n",
                siteTag, self, static_cast<unsigned>(slotIdx));
        }
    }

    // Compute the slot index OpenBuddySelect uses internally, mirroring
    // the orig's logic at named-build line 2898003-2898009. Returns 0
    // if the panel has no rows (no work to do).
    static std::uint32_t ComputeOpenBuddySlotIndex(void* self) noexcept
    {
        if (!self) return 0;
        std::uint32_t slot = 0;
        __try
        {
            auto* base = reinterpret_cast<std::uint8_t*>(self);
            const std::uint32_t rowCount =
                *reinterpret_cast<std::uint32_t*>(base + kOff_PanelRowCount);
            if (rowCount == 0) return 0;
            const std::int32_t cursor =
                *reinterpret_cast<std::int32_t*>(base + kOff_PanelCursor);
            const std::int32_t scrollPage =
                *reinterpret_cast<std::int32_t*>(base + kOff_PanelScrollPage);
            const std::uint32_t sum =
                static_cast<std::uint32_t>(cursor + scrollPage);
            slot = sum % rowCount;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
        return slot;
    }

    static void __fastcall hkChangeDetailsWindowBuddySelect(
        void* self, std::uint64_t index)
    {
        // Run orig first. For custom partsTypes the orig translator
        // writes 0 to the hash buffer at self+0xA0D0 and the setter
        // call 2 stamps that 0 into the UI window state — no text shown.
        if (g_OrigChange)
            g_OrigChange(self, index);

        TryOverrideUniformsRowForSlot(
            self,
            static_cast<std::uint32_t>(index & 0xFFFFFFFFu),
            "Change");
    }

    static void __fastcall hkOpenBuddySelect(void* self)
    {
        // Same post-orig override pattern as the Change hook. OpenBuddy-
        // Select doesn't take an index; it computes the active slot from
        // the panel's row-count + scroll + cursor state on the same `self`.
        if (g_OrigOpen)
            g_OrigOpen(self);

        const std::uint32_t slotIdx = ComputeOpenBuddySlotIndex(self);
        TryOverrideUniformsRowForSlot(self, slotIdx, "Open");
    }
}

namespace outfit
{
    bool Install_OutfitUniformsRow_Hook()
    {
        bool anyInstalled = false;

        if (!g_InstalledChange)
        {
            void* target = ResolveGameAddress(
                gAddr.CharacterSelectorCallbackImpl_ChangeDetailsWindowBuddySelect);
            if (target)
            {
                g_InstalledChange = CreateAndEnableHook(
                    target,
                    reinterpret_cast<void*>(&hkChangeDetailsWindowBuddySelect),
                    reinterpret_cast<void**>(&g_OrigChange));
                Log("[OutfitUniformsRow] Change hook: %s (target=%p)\n",
                    g_InstalledChange ? "OK" : "FAIL", target);
                anyInstalled = anyInstalled || g_InstalledChange;
            }
            else
            {
                Log("[OutfitUniformsRow] Change target unresolved\n");
            }
        }

        if (!g_InstalledOpen)
        {
            void* target = ResolveGameAddress(
                gAddr.CharacterSelectorCallbackImpl_OpenBuddySelect);
            if (target)
            {
                g_InstalledOpen = CreateAndEnableHook(
                    target,
                    reinterpret_cast<void*>(&hkOpenBuddySelect),
                    reinterpret_cast<void**>(&g_OrigOpen));
                Log("[OutfitUniformsRow] Open hook: %s (target=%p)\n",
                    g_InstalledOpen ? "OK" : "FAIL", target);
                anyInstalled = anyInstalled || g_InstalledOpen;
            }
            else
            {
                Log("[OutfitUniformsRow] Open target unresolved (JP build?)\n");
            }
        }

        return anyInstalled;
    }

    void Uninstall_OutfitUniformsRow_Hook()
    {
        if (g_InstalledChange)
        {
            if (void* t = ResolveGameAddress(
                    gAddr.CharacterSelectorCallbackImpl_ChangeDetailsWindowBuddySelect))
                DisableAndRemoveHook(t);
            g_OrigChange      = nullptr;
            g_InstalledChange = false;
        }

        if (g_InstalledOpen)
        {
            if (void* t = ResolveGameAddress(
                    gAddr.CharacterSelectorCallbackImpl_OpenBuddySelect))
                DisableAndRemoveHook(t);
            g_OrigOpen      = nullptr;
            g_InstalledOpen = false;
        }

        Log("[OutfitUniformsRow] removed\n");
    }
}
