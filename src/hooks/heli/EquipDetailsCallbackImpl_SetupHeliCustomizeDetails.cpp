#include <pch.h>
#include "EquipDetailsCallbackImpl_SetupHeliCustomizeDetails.h"

#include <Windows.h>
#include <cstdint>

#include "EquipDevelopControllerImpl_GetHeliPartsId.h"
#include "MissionCodeGuard.h"
#include "SupportHeliHookInstall.h"
#include "SupportHeliParts.h"

namespace
{
    using SetupDetails_t   = std::uint64_t(__fastcall*)(void* details);
    using SetBadge_t       = void(__fastcall*)(std::uint8_t* details, std::uint8_t* entry, std::uint8_t damageType);
    using RowOfDevelopId_t = std::uint16_t(__fastcall*)(void* develop, std::uint32_t developId);
    using RowValue_t       = std::uint64_t*(__fastcall*)(void* develop, std::uint64_t* value, std::uint32_t rowOrDevelopId);
    using DamageType_t     = std::uint8_t(__fastcall*)(void* develop, std::uint32_t row);
    using LangText_t       = const char*(__fastcall*)(void* text, std::uint64_t langId);
    using SetText_t        = void(__fastcall*)(void* text, void* node, void* layout, const char* string, std::uint8_t show);
    using SetTexture_t     = void(__fastcall*)(void* text, void* node, std::uint64_t file, std::uint64_t slotName);
    using NodeStep_t       = void(__fastcall*)(void* text, void* node);

    constexpr std::size_t   kDetailsDevelop    = 0x40;
    constexpr std::size_t   kDetailsText       = 0x80;
    constexpr std::size_t   kDetailsLayout     = 0x98;
    constexpr std::size_t   kGatlingEntry      = 0x420;  // the panel's second weapon entry, filled with GATLING-GUN 33001 and "DMG"
    constexpr std::size_t   kEntryIconA        = 0x08;
    constexpr std::size_t   kEntryIconB        = 0x10;
    constexpr std::size_t   kEntryName         = 0x18;
    constexpr std::size_t   kEntryLethalFrame  = 0x38;
    constexpr std::size_t   kEntryOtherFrame   = 0x40;
    constexpr std::size_t   kDevelopRowOf      = 0xE0 / sizeof(void*);
    constexpr std::size_t   kDevelopDamageType = 0x500 / sizeof(void*);
    constexpr std::size_t   kDevelopNameOf     = 0x720 / sizeof(void*);
    constexpr std::size_t   kDevelopIconOf     = 0x770 / sizeof(void*);
    constexpr std::size_t   kTextLang          = 0x750 / sizeof(void*);
    constexpr std::size_t   kTextSet           = 0x708 / sizeof(void*);
    constexpr std::size_t   kTextSetTexture    = 0x518 / sizeof(void*);
    constexpr std::size_t   kTextHide          = 0x6A8 / sizeof(void*);
    constexpr std::uint16_t kNoRow             = 0x400;
    constexpr std::uint64_t kIconSlotName      = 0xCAFB3BBF9889;

    constexpr std::size_t  kDetailsLevels      = 0xE78;
    constexpr std::size_t  kLevelsHoverFlag    = 0x65;   // set only when the levels came from a hovered list row
    constexpr std::size_t  kLevelsHoverSlot    = 0x66;
    constexpr std::uint8_t kSubWeaponSlot      = 1;

    constexpr std::uint8_t kSetupDetailsEntry[] =
    {
        0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x48, 0x89, 0x7C, 0x24, 0x20, 0x55,
    };

    SetupDetails_t g_Orig   = nullptr;
    void*          g_Target = nullptr;
    SetBadge_t     g_SetBadge = nullptr;

    template <typename Fn>
    Fn Call(void* object, std::size_t slot)
    {
        return reinterpret_cast<Fn>((*static_cast<void***>(object))[slot]);
    }

    void ShowPickedGatlingSEH(std::uint8_t* details, std::uint32_t developId)
    {
        __try
        {
            void* develop = *reinterpret_cast<void**>(details + kDetailsDevelop);
            void* text    = *reinterpret_cast<void**>(details + kDetailsText);
            void* layout  = *reinterpret_cast<void**>(details + kDetailsLayout);
            std::uint8_t* entry = details + kGatlingEntry;
            if (!develop || !text)
                return;
            const std::uint16_t row = Call<RowOfDevelopId_t>(develop, kDevelopRowOf)(develop, developId);
            if (row == kNoRow)
                return;
            std::uint64_t value = 0;
            const std::uint64_t nameId = *Call<RowValue_t>(develop, kDevelopNameOf)(develop, &value, developId);
            const std::uint64_t icon   = *Call<RowValue_t>(develop, kDevelopIconOf)(develop, &value, row);
            const char* name = Call<LangText_t>(text, kTextLang)(text, nameId);
            Call<SetText_t>(text, kTextSet)(text, *reinterpret_cast<void**>(entry + kEntryName), layout, name, 1);
            Call<SetTexture_t>(text, kTextSetTexture)(text, *reinterpret_cast<void**>(entry + kEntryIconA), icon, kIconSlotName);
            Call<SetTexture_t>(text, kTextSetTexture)(text, *reinterpret_cast<void**>(entry + kEntryIconB), icon, kIconSlotName);
            Call<NodeStep_t>(text, kTextHide)(text, *reinterpret_cast<void**>(entry + kEntryLethalFrame));
            Call<NodeStep_t>(text, kTextHide)(text, *reinterpret_cast<void**>(entry + kEntryOtherFrame));
            g_SetBadge(details, entry, Call<DamageType_t>(develop, kDevelopDamageType)(develop, row));
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    // the game fills the gatling entry with GATLING-GUN and a fixed "DMG": a picked custom gatling shows itself instead
    void ShowPickedGatling(void* details)
    {
        SupportHeliParts::Part gatling{};
        if (g_SetBadge && details && SupportHeliParts::PickedPart(SupportHeliParts::Kind::Gatling, gatling))
            ShowPickedGatlingSEH(static_cast<std::uint8_t*>(details), gatling.developId);
    }

    bool ShowsHoveredSubWeaponSEH(const std::uint8_t* details)
    {
        __try
        {
            const std::uint8_t* levels = *reinterpret_cast<std::uint8_t* const*>(details + kDetailsLevels);
            return levels && levels[kLevelsHoverFlag] != 0 && levels[kLevelsHoverSlot] == kSubWeaponSlot;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    std::uint64_t __fastcall hkSetupDetails(void* details)
    {
        if (MissionCodeGuard::ShouldBypassHooks())
            return g_Orig(details);
        const bool hoverPanel = ShowsHoveredSubWeaponSEH(static_cast<const std::uint8_t*>(details)) && HeliParts_BeginHoverPanel();
        const std::uint64_t result = g_Orig(details);
        if (hoverPanel)
            HeliParts_EndHoverPanel();
        ShowPickedGatling(details);
        return result;
    }
}

bool Install_EquipDetailsCallbackImpl_SetupHeliCustomizeDetails_Hook()
{
    if (g_Target)
        return true;
    constexpr std::uint8_t kSetBadgeEntry[] = { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x41 };
    void* setBadge = gAddr.EquipDetailsCallbackImpl_SetDamageBadge ? ResolveGameAddress(gAddr.EquipDetailsCallbackImpl_SetDamageBadge) : nullptr;
    if (setBadge && CheckHookEntry(setBadge, kSetBadgeEntry, sizeof(kSetBadgeEntry)) != HookEntry::Unknown)
        g_SetBadge = reinterpret_cast<SetBadge_t>(setBadge);
    else
        LogError("SupportHeliWeapon: details badge code missing or changed on this build, the panel shows the vanilla gatling for a custom one");
    g_Target = InstallSupportHeliHook(
        "Pequod customize details", gAddr.EquipDetailsCallbackImpl_SetupHeliCustomizeDetails,
        kSetupDetailsEntry, nullptr, sizeof(kSetupDetailsEntry),
        reinterpret_cast<void*>(&hkSetupDetails), reinterpret_cast<void**>(&g_Orig),
        "the customize panel shows the base row while a custom Pequod weapon is hovered");
    return g_Target != nullptr;
}

void Uninstall_EquipDetailsCallbackImpl_SetupHeliCustomizeDetails_Hook()
{
    UninstallSupportHeliHook(g_Target);
}
