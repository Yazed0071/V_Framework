#include <pch.h>
#include "TppUIWeaponNumImpl_ShowItemPanel.h"

#include <Windows.h>
#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"
#include "../equip/EquipAliasLease.h"
#include "../equip/EquipControllerImpl_UpdateBattery.h"
#include "../player/PlayerSkill.h"

namespace
{
    using ShowItemPanel_t   = std::uint64_t(__fastcall*)(std::uint8_t* panel);
    using SetBatteryGauge_t = void(__fastcall*)(void* panel, void* gauges, int* shownValue, int value, int max, bool show, float length);

    constexpr std::size_t kPanelItemId     = 0x2E0;
    constexpr std::size_t kPanelItemShown  = 0x32C;
    constexpr std::size_t kPanelItemGauges = 0x360;
    constexpr float       kEffectBarLength = 3.0f;   // what ShowItemPanel passes for the effect bar of the game consumables

    const std::uint8_t kShowItemPanelEntry[] = { 0x40, 0x53, 0x55, 0x56, 0x57, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x50, 0x48, 0x8B, 0x41, 0x48, 0x48,
                                                 0x8B, 0xF1, 0x48, 0x8B, 0x88, 0xC0, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x01, 0xFF, 0x50, 0x48 };

    const std::uint8_t kBatteryGaugeEntry[] = { 0x40, 0x53, 0x55, 0x57, 0x48, 0x81, 0xEC, 0x80, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x05, 0x00, 0x00,
                                                0x00, 0x00, 0x48, 0x33, 0xC4, 0x48, 0x89, 0x44, 0x24, 0x50 };
    const std::uint8_t kBatteryGaugeMask[]  = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0,
                                                0, 0, 1, 1, 1, 1, 1, 1, 1, 1 };

    ShowItemPanel_t   g_Orig            = nullptr;
    void*             g_Target          = nullptr;
    SetBatteryGauge_t g_SetBatteryGauge = nullptr;

    int PanelItemSEH(std::uint8_t* panel)
    {
        __try
        {
            return *reinterpret_cast<int*>(panel + kPanelItemId);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    void DrawEffectBarSEH(std::uint8_t* panel, int item)
    {
        __try
        {
            int current = 0;
            int max = 0;
            std::uint8_t batteryLength = 0;
            if (item <= 0 || ItemBattery::GaugeLevel(item, batteryLength) || !PlayerSkill::ItemGauge(item, current, max))
                return;   // an item with a battery keeps its battery bar, as in the game
            g_SetBatteryGauge(panel, panel + kPanelItemGauges, reinterpret_cast<int*>(panel + kPanelItemShown), current, max, true,
                              kEffectBarLength);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    std::uint64_t __fastcall hkShowItemPanel(std::uint8_t* panel)   // every frame the item panel of the HUD is up
    {
        const std::uint64_t result = g_Orig(panel);   // it hid the bar: the game only draws it for its own consumables (id mask 0xF600000003 from 0x1C2)
        if (panel && ItemBattery::IsActiveHere())
        {
            const int item = PanelItemSEH(panel);
            if (item > 0)
                DrawEffectBarSEH(panel, equip::FromEngine(item));
        }
        return result;
    }
}

bool Install_TppUIWeaponNumImpl_ShowItemPanel_Hook()
{
    void* target = gAddr.TppUIWeaponNumImpl_ShowItemPanel ? ResolveGameAddress(gAddr.TppUIWeaponNumImpl_ShowItemPanel) : nullptr;
    void* gauge  = gAddr.TppUIWeaponNumImpl_SetBatteryGauge ? ResolveGameAddress(gAddr.TppUIWeaponNumImpl_SetBatteryGauge) : nullptr;
    if (!target || !gauge)
    {
        LogError("ItemEffectGauge: HUD item panel addresses missing for %s, custom items show no effect time bar", GetGameBuildName(gGameBuild));
        return false;
    }
    if (CheckHookEntryMasked(gauge, kBatteryGaugeEntry, kBatteryGaugeMask, sizeof(kBatteryGaugeEntry)) == HookEntry::Unknown)
    {
        LogError("ItemEffectGauge: TppUIWeaponNumImpl::SetBatteryGauge does not match this build, custom items show no effect time bar");
        return false;
    }
    const HookEntry entry = CheckHookEntry(target, kShowItemPanelEntry, sizeof(kShowItemPanelEntry));
    if (entry == HookEntry::Unknown)
    {
        LogError("ItemEffectGauge: TppUIWeaponNumImpl::ShowItemPanel does not match this build, custom items show no effect time bar");
        return false;
    }
    if (entry == HookEntry::ForeignHook)
        LogHookedOnTop("ItemEffectGauge: TppUIWeaponNumImpl::ShowItemPanel", target);
    g_SetBatteryGauge = reinterpret_cast<SetBatteryGauge_t>(gauge);
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hkShowItemPanel), reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("ItemEffectGauge: ShowItemPanel hook failed, custom items show no effect time bar");
        g_SetBatteryGauge = nullptr;
        return false;
    }
    g_Target = target;
    return true;
}

void Uninstall_TppUIWeaponNumImpl_ShowItemPanel_Hook()
{
    if (g_Target)
        DisableAndRemoveHook(g_Target);
    g_Target          = nullptr;
    g_Orig            = nullptr;
    g_SetBatteryGauge = nullptr;
}
