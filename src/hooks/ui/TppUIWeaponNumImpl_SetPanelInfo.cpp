#include <pch.h>
#include "TppUIWeaponNumImpl_SetPanelInfo.h"

#include <Windows.h>
#include <atomic>
#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "log.h"
#include "../player/AttackActionImpl_UpdateShootingByPad.h"

namespace
{
    using SetPanelInfo_t = int(__fastcall*)(void* panel); // returns the shown equipId, UpdatePanel hands it on
    using SetWeaponChargeGauge_t = void(__fastcall*)(void* panel, int value, int max, float length);
    using SetBatteryGauge_t = void(__fastcall*)(void* panel, void* gauges, int* shownBattery, int battery, int max, bool show, float length);
    using SetVisible_t = void(__fastcall*)(void* layout, std::uint64_t element, bool visible);

    constexpr std::size_t kPanelContext = 0x48;
    constexpr std::size_t kContextLayout = 0x20;
    constexpr std::size_t kLayoutSetVisibleSlot = 0x2B0;
    constexpr std::size_t kPanelAmmoIcons = 0x70;     // round ammo icons, UpdateAmmo shows them again on every SetPanelInfo
    constexpr std::size_t kPanelGauges = 0x228;       // battery track, then the charge bar at +0x230
    constexpr std::size_t kPanelShownBattery = 0x1CC;
    constexpr std::size_t kPanelInfo = 0x50;
    constexpr std::size_t kInfoArmEquipIds = 0x274;    // equipIds that get the arm battery and its ring
    constexpr int kInfoArmEquipIdCount = 20;
    constexpr int kGaugeCells = 3; // the bar's full width; one cell per charge level, like the D-Walker's stun gun

    const std::uint8_t kSetPanelInfoEntry[] =
    {
        0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x10, 0x48, 0x89, 0x70, 0x18, 0x48, 0x89, 0x78, 0x20, 0x55,
        0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8D, 0x68, 0xA1, 0x48, 0x81, 0xEC, 0xD0,
        0x00, 0x00, 0x00,
    };

    const std::uint8_t kChargeGaugeEntry[] =
    {
        0x40, 0x53, 0x48, 0x83, 0xEC, 0x50, 0x48, 0x8B, 0x05, 0x00, 0x00, 0x00, 0x00, 0x48, 0x33, 0xC4,
        0x48, 0x89, 0x44, 0x24, 0x40,
    };
    const std::uint8_t kChargeGaugeMask[] =
    {
        1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1,
        1, 1, 1, 1, 1,
    };

    const std::uint8_t kBatteryGaugeEntry[] =
    {
        0x40, 0x53, 0x55, 0x57, 0x48, 0x81, 0xEC, 0x80, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x05, 0x00, 0x00,
        0x00, 0x00, 0x48, 0x33, 0xC4, 0x48, 0x89, 0x44, 0x24, 0x50,
    };
    const std::uint8_t kBatteryGaugeMask[] =
    {
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0,
        0, 0, 1, 1, 1, 1, 1, 1, 1, 1,
    };

    SetPanelInfo_t g_Orig = nullptr;
    void* g_Target = nullptr;
    SetWeaponChargeGauge_t g_SetChargeGauge = nullptr;
    SetBatteryGauge_t g_SetBatteryGauge = nullptr;
    std::atomic<void*> g_RingPanel{ nullptr };

    bool ArmListedSEH(void* panel, int shownEquipId)
    {
        __try
        {
            const auto* info = *reinterpret_cast<std::uint8_t**>(static_cast<std::uint8_t*>(panel) + kPanelInfo);
            const auto* ids = reinterpret_cast<const std::int32_t*>(info + kInfoArmEquipIds);
            for (int i = 0; i < kInfoArmEquipIdCount; ++i)
                if (ids[i] == shownEquipId)
                    return true;
            return false;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return true;
        }
    }

    void SetChargeGaugeSEH(void* panel, int value, int max, int cells)
    {
        __try
        {
            g_SetChargeGauge(panel, value, max, static_cast<float>(cells));
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    void ShowBatteryTrackSEH(void* panel, int cells)
    {
        __try
        {
            auto* p = static_cast<std::uint8_t*>(panel);
            g_SetBatteryGauge(panel, p + kPanelGauges, reinterpret_cast<int*>(p + kPanelShownBattery), 0, 1, true, static_cast<float>(cells));
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    void SetPanelElementVisibleSEH(void* panel, std::size_t element, bool visible)
    {
        __try
        {
            auto* p = static_cast<std::uint8_t*>(panel);
            void* layout = *reinterpret_cast<void**>(*reinterpret_cast<std::uint8_t**>(p + kPanelContext) + kContextLayout);
            const std::uint64_t handle = *reinterpret_cast<std::uint64_t*>(p + element);
            (*reinterpret_cast<SetVisible_t**>(layout))[kLayoutSetVisibleSlot / sizeof(void*)](layout, handle, visible);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    void DrawChargeGauge(void* panel, int shownEquipId)
    {
        int chargeEquipId = 0;
        int value = 0;
        int max = 1;
        equip::ChargeShot_HudCharge(&chargeEquipId, &value, &max);
        const int realEquipId = equip::ChargeShot_RealEquipId(shownEquipId);
        const int levels = equip::ChargeShot_HudLevels(realEquipId);
        if (levels > 0)
        {
            if (chargeEquipId != realEquipId)
                value = 0; // the charge belongs to the gun in hand, which follows the panel only once it is raised
            const int cells = levels > kGaugeCells ? kGaugeCells : levels;
            ShowBatteryTrackSEH(panel, cells); // the D-Walker's stun gun panel draws this track under its charge bar
            SetChargeGaugeSEH(panel, value, max, cells);
            SetPanelElementVisibleSEH(panel, kPanelAmmoIcons, false);
            g_RingPanel.store(panel);
            return;
        }
        if (g_RingPanel.load() != panel)
            return;
        g_RingPanel.store(nullptr);
        if (!ArmListedSEH(panel, shownEquipId))
            SetChargeGaugeSEH(panel, 0, max, kGaugeCells); // the arm's own ring was drawn by SetPanelInfo, anything else hides it
    }

    int __fastcall hkSetPanelInfo(void* panel)
    {
        const int shownEquipId = g_Orig(panel);
        if (!MissionCodeGuard::ShouldBypassHooks())
            DrawChargeGauge(panel, shownEquipId);
        return shownEquipId;
    }
}

bool Install_TppUIWeaponNumImpl_SetPanelInfo_Hook()
{
    if (g_Target)
        return true;
    void* target = gAddr.TppUIWeaponNumImpl_SetPanelInfo
        ? ResolveGameAddress(gAddr.TppUIWeaponNumImpl_SetPanelInfo) : nullptr;
    void* gauge = gAddr.TppUIWeaponNumImpl_SetWeaponChargeGauge
        ? ResolveGameAddress(gAddr.TppUIWeaponNumImpl_SetWeaponChargeGauge) : nullptr;
    void* battery = gAddr.TppUIWeaponNumImpl_SetBatteryGauge
        ? ResolveGameAddress(gAddr.TppUIWeaponNumImpl_SetBatteryGauge) : nullptr;
    if (!target || !gauge || !battery)
    {
        LogError("ChargeShot: HUD panel addresses missing for this build, no charge ring");
        return false;
    }
    if (CheckHookEntryMasked(gauge, kChargeGaugeEntry, kChargeGaugeMask, sizeof(kChargeGaugeEntry)) == HookEntry::Unknown)
    {
        char found[160] = {};
        DescribeFunctionEntry(gauge, found, sizeof(found));
        LogError("ChargeShot: SetWeaponChargeGauge does not match (%s), no charge ring", found);
        return false;
    }
    if (CheckHookEntryMasked(battery, kBatteryGaugeEntry, kBatteryGaugeMask, sizeof(kBatteryGaugeEntry)) == HookEntry::Unknown)
    {
        char found[160] = {};
        DescribeFunctionEntry(battery, found, sizeof(found));
        LogError("ChargeShot: SetBatteryGauge does not match (%s), no charge ring", found);
        return false;
    }
    const HookEntry entry = CheckHookEntry(target, kSetPanelInfoEntry, sizeof(kSetPanelInfoEntry));
    if (entry == HookEntry::Unknown)
    {
        char found[160] = {};
        DescribeFunctionEntry(target, found, sizeof(found));
        LogError("ChargeShot: SetPanelInfo does not match (%s), no charge ring", found);
        return false;
    }
    if (entry == HookEntry::ForeignHook)
        LogHookedOnTop("ChargeShot: SetPanelInfo", target);
    g_SetChargeGauge = reinterpret_cast<SetWeaponChargeGauge_t>(gauge);
    g_SetBatteryGauge = reinterpret_cast<SetBatteryGauge_t>(battery);
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hkSetPanelInfo), reinterpret_cast<void**>(&g_Orig)))
    {
        g_SetChargeGauge = nullptr;
        g_SetBatteryGauge = nullptr;
        LogError("ChargeShot: SetPanelInfo hook failed @ %p, no charge ring", target);
        return false;
    }
    g_Target = target;
    return true;
}

void Uninstall_TppUIWeaponNumImpl_SetPanelInfo_Hook()
{
    if (g_Target)
        DisableAndRemoveHook(g_Target);
    g_Target = nullptr;
    g_Orig = nullptr;
    g_SetChargeGauge = nullptr;
    g_SetBatteryGauge = nullptr;
    g_RingPanel.store(nullptr);
}
