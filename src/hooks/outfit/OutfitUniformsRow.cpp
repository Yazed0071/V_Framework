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


    using ChangeDetailsWindowBuddySelect_t =
        void (__fastcall*)(void* self, std::uint64_t index);

    using OpenBuddySelect_t =
        void (__fastcall*)(void* self);

    static ChangeDetailsWindowBuddySelect_t g_OrigChange = nullptr;
    static OpenBuddySelect_t                g_OrigOpen   = nullptr;

    static bool g_InstalledChange = false;
    static bool g_InstalledOpen   = false;


    constexpr std::size_t kOff_ChildObject       = 0x38;
    constexpr std::size_t kOff_PartsTypeArray    = 0x9C70;
    constexpr std::size_t kOff_QuarkWindowHandle = 0x9FE0;
    constexpr std::size_t kOff_HashOutBuffer     = 0xA0D0;


    constexpr std::size_t kOff_PanelRowCount    = 0x94;
    constexpr std::size_t kOff_PanelCursor      = 0x9C;
    constexpr std::size_t kOff_PanelScrollPage  = 0xA0;


    constexpr std::size_t kOff_ChildToUiSetter   = 0x20;


    constexpr std::size_t kVtbl_UiPropertySet    = 0x1E0;


    constexpr std::uint64_t kPropHash_UniformsText = 0x30A0D543E155ULL;


    using QuarkUiSetter_t =
        void (__fastcall*)(void* uiSetterSelf,
                           std::uint64_t windowHandle,
                           std::uint64_t propertyHash,
                           std::uint64_t valueOrPointer);


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
                return;
            }

            const outfit::OutfitEntry* entry = nullptr;
            if (!outfit::TryGetOutfitByPartsType(partsType, &entry) || !entry)
                return;


            // Per-PT lang-equip name: use the live PT's branch hash, with the
            // Snake↔Avatar bridge applied inside GetLangEquipNameHashFor.
            const std::uint8_t  livePT     = outfit::ReadLivePlayerType();
            const std::uint64_t nameHash   = entry->GetLangEquipNameHashFor(livePT);
            if (nameHash == 0)
            {

                return;
            }


            *reinterpret_cast<std::uint64_t*>(base + kOff_HashOutBuffer) = nameHash;

            auto* childObj =
                *reinterpret_cast<std::uint8_t**>(base + kOff_ChildObject);
            if (!childObj) return;

            auto* uiSetterObj =
                *reinterpret_cast<std::uint8_t**>(childObj + kOff_ChildToUiSetter);
            if (!uiSetterObj) return;


            auto* vtable = *reinterpret_cast<std::uint8_t**>(uiSetterObj);
            if (!vtable) return;

            const auto setter = *reinterpret_cast<QuarkUiSetter_t*>(
                vtable + kVtbl_UiPropertySet);
            if (!setter) return;

            const std::uint64_t windowHandle =
                *reinterpret_cast<std::uint64_t*>(base + kOff_QuarkWindowHandle);


            setter(uiSetterObj,
                   windowHandle,
                   kPropHash_UniformsText,
                   reinterpret_cast<std::uint64_t>(base + kOff_HashOutBuffer));

            Log("[OutfitUniformsRow:%s] overrode UNIFORMS hash for "
                "partsType=0x%02X developId=%u slot=%u livePT=%u -> 0x%016llX\n",
                siteTag,
                static_cast<unsigned>(partsType),
                static_cast<unsigned>(entry->developId),
                static_cast<unsigned>(slotIdx),
                static_cast<unsigned>(livePT),
                static_cast<unsigned long long>(nameHash));
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            Log("[OutfitUniformsRow:%s] SEH while overriding UNIFORMS row "
                "(self=%p slot=%u)\n",
                siteTag, self, static_cast<unsigned>(slotIdx));
        }
    }


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


        if (g_OrigChange)
            g_OrigChange(self, index);

        TryOverrideUniformsRowForSlot(
            self,
            static_cast<std::uint32_t>(index & 0xFFFFFFFFu),
            "Change");
    }

    static void __fastcall hkOpenBuddySelect(void* self)
    {


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
