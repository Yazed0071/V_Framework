#include <pch.h>
#include "EquipDevelopControllerImpl_GetHeliPartsId.h"

#include <Windows.h>
#include <atomic>
#include <cstdint>

#include "MissionCodeGuard.h"
#include "SupportHeliHookInstall.h"
#include "SupportHeliParts.h"
#include "SupportHeliWeapon.h"

namespace
{
    using GetHeliPartsId_t   = std::uint64_t(__fastcall*)(void* controller, std::uint32_t row);
    using FindPartsRow_t     = std::uint64_t(__fastcall*)(void* controller, std::uint32_t slot, std::uint32_t level);
    using RowDevelopId_t     = std::uint16_t(__fastcall*)(void* controller, std::uint32_t row);
    using DevelopIdToRow_t   = std::uint16_t(__fastcall*)(void* controller, std::uint32_t developId);

    constexpr std::uint16_t kNoRow              = 0x400;
    constexpr std::uint16_t kNoHoveredRow       = 0xFFFF;
    constexpr std::size_t   kSlotRowDevelopId   = 0x128;
    constexpr std::size_t   kSlotDevelopIdToRow = 0xE0;
    constexpr std::uint32_t kSubWeaponSlot      = 1;
    constexpr std::int32_t  kVanillaVolley      = 8;
    constexpr std::int32_t  kMaxPanelNumber     = 0xFFFF;

    constexpr std::uint8_t kGetHeliPartsIdEntry[] =
    {
        0x48, 0x83, 0xEC, 0x28, 0xB8, 0x00, 0x04, 0x00, 0x00, 0x66, 0x3B, 0xD0, 0x74, 0x32, 0x48, 0x8B,
    };
    constexpr std::uint8_t kFindPartsRowEntry[] =
    {
        0x44, 0x0F, 0xB6, 0xD2, 0x45, 0x32, 0xC9, 0x4C, 0x8D, 0x1D, 0x00, 0x00, 0x00, 0x00, 0x66, 0x90,
    };
    constexpr std::uint8_t kFindPartsRowMask[] =
    {
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1,
    };

    struct PanelNumber
    {
        bool         valid  = false;
        std::int32_t level  = 0;
        std::int32_t number = 0;
    };

    GetHeliPartsId_t g_OrigGetHeliPartsId   = nullptr;
    FindPartsRow_t   g_OrigFindPartsRow     = nullptr;
    void*            g_GetHeliPartsIdTarget = nullptr;
    void*            g_FindPartsRowTarget   = nullptr;

    std::atomic<std::uint16_t> g_HoveredSubRow{ kNoHoveredRow };

    thread_local bool          t_WatchingDecide   = false;
    thread_local std::uint32_t t_DecidedDevelopId = 0;
    thread_local bool          t_HoverPanel       = false;
    thread_local std::uint16_t t_HoverPanelRow    = kNoRow;
    thread_local PanelNumber   t_PanelNumber;

    template <typename Fn>
    Fn VirtualSlot(void* object, std::size_t slotOffset)
    {
        void** vtable = *static_cast<void***>(object);
        return reinterpret_cast<Fn>(vtable[slotOffset / sizeof(void*)]);
    }

    std::int32_t ShownAmmo(const SupportHeliWeapon::Weapon& weapon)
    {
        std::int32_t shown = weapon.ammo;
        if (!weapon.isMissile)
            shown *= weapon.shotCount > 0 ? weapon.shotCount : kVanillaVolley;
        return shown > kMaxPanelNumber ? kMaxPanelNumber : shown;
    }

    void NotePanelNumber(std::uint32_t developId, std::int32_t level)
    {
        SupportHeliWeapon::Weapon weapon{};
        t_PanelNumber.valid = SupportHeliWeapon::FindByDevelopId(developId, weapon);
        t_PanelNumber.level = level;
        if (t_PanelNumber.valid)
            t_PanelNumber.number = ShownAmmo(weapon);
    }

    std::uint64_t __fastcall hkGetHeliPartsId(void* controller, std::uint32_t row)
    {
        if (MissionCodeGuard::ShouldBypassHooks() || static_cast<std::uint16_t>(row) == kNoRow)
            return g_OrigGetHeliPartsId(controller, row);

        const std::uint16_t developId = VirtualSlot<RowDevelopId_t>(controller, kSlotRowDevelopId)(controller, row);
        if (t_WatchingDecide)
            t_DecidedDevelopId = developId;

        SupportHeliWeapon::Weapon weapon{};
        if (SupportHeliWeapon::FindByDevelopId(developId, weapon))
        {
            NotePanelNumber(developId, weapon.fallbackLevel);
            return weapon.fallbackLevel;
        }
        t_PanelNumber.valid = false;
        SupportHeliParts::Part part{};
        if (SupportHeliParts::FindByDevelopId(developId, part))
            return 0;   // an outfit, gatling or side gun row holds no level
        return g_OrigGetHeliPartsId(controller, row);
    }

    std::uint64_t __fastcall hkFindPartsRow(void* controller, std::uint32_t slot, std::uint32_t level)
    {
        if (MissionCodeGuard::ShouldBypassHooks())
            return g_OrigFindPartsRow(controller, slot, level);
        if (static_cast<std::uint8_t>(slot) != kSubWeaponSlot)
            return g_OrigFindPartsRow(controller, slot, level);

        if (t_HoverPanel)
        {
            NotePanelNumber(VirtualSlot<RowDevelopId_t>(controller, kSlotRowDevelopId)(controller, t_HoverPanelRow),
                            static_cast<std::uint8_t>(level));
            return t_HoverPanelRow;
        }

        const std::uint8_t choice = SupportHeliWeapon::MenuChoiceForSortie(static_cast<std::uint8_t>(level));
        SupportHeliWeapon::Weapon weapon{};
        if (!SupportHeliWeapon::FindByCustomId(choice, weapon))
        {
            t_PanelNumber.valid = false;
            return g_OrigFindPartsRow(controller, slot, level);
        }
        NotePanelNumber(weapon.developId, static_cast<std::uint8_t>(level));
        return VirtualSlot<DevelopIdToRow_t>(controller, kSlotDevelopIdToRow)(controller, weapon.developId);
    }
}

void HeliParts_BeginDecideWatch()
{
    t_WatchingDecide   = true;
    t_DecidedDevelopId = 0;
}

std::uint32_t HeliParts_EndDecideWatch()
{
    t_WatchingDecide = false;
    return t_DecidedDevelopId;
}

void HeliParts_SetHoveredSubRow(std::uint16_t row)
{
    g_HoveredSubRow.store(row);
}

bool HeliParts_BeginHoverPanel()
{
    const std::uint16_t row = g_HoveredSubRow.load();
    if (row == kNoHoveredRow || row == kNoRow)
        return false;
    t_HoverPanel    = true;
    t_HoverPanelRow = row;
    return true;
}

void HeliParts_EndHoverPanel()
{
    t_HoverPanel = false;
}

bool HeliParts_TakePanelNumber(std::int32_t level, std::int32_t& number)
{
    const PanelNumber taken = t_PanelNumber;
    t_PanelNumber.valid = false;
    if (!taken.valid || taken.level != level)
        return false;
    number = taken.number;
    return true;
}

bool Install_EquipDevelopControllerImpl_GetHeliPartsId_Hooks()
{
    if (!g_GetHeliPartsIdTarget)
    {
        g_GetHeliPartsIdTarget = InstallSupportHeliHook(
            "Pequod parts level lookup", gAddr.EquipDevelopControllerImpl_GetHeliPartsId,
            kGetHeliPartsIdEntry, nullptr, sizeof(kGetHeliPartsIdEntry),
            reinterpret_cast<void*>(&hkGetHeliPartsId), reinterpret_cast<void**>(&g_OrigGetHeliPartsId),
            "custom Pequod weapons cannot be picked in the customize menu");
    }
    if (!g_FindPartsRowTarget)
    {
        g_FindPartsRowTarget = InstallSupportHeliHook(
            "Pequod parts row lookup", gAddr.EquipDevelopControllerImpl_FindHeliPartsRow,
            kFindPartsRowEntry, kFindPartsRowMask, sizeof(kFindPartsRowEntry),
            reinterpret_cast<void*>(&hkFindPartsRow), reinterpret_cast<void**>(&g_OrigFindPartsRow),
            "the customize menu shows the vanilla row for a custom Pequod weapon");
    }
    return g_GetHeliPartsIdTarget != nullptr;
}

void Uninstall_EquipDevelopControllerImpl_GetHeliPartsId_Hooks()
{
    UninstallSupportHeliHook(g_FindPartsRowTarget);
    UninstallSupportHeliHook(g_GetHeliPartsIdTarget);
}
