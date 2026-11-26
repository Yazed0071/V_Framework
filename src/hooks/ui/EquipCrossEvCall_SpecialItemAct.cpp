#include <pch.h>

#include "EquipCrossEvCall_SpecialItemAct.h"

#include <Windows.h>
#include <atomic>
#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "../equip/EquipAliasLease.h"
#include "../equip/ItemCount.h"
#include "LuaBroadcaster.h"
#include "MissionCodeGuard.h"
#include "log.h"

namespace
{
    using SpecialItemAct_t   = void(__fastcall*)(std::uint8_t* cross);
    using CursorPanelIndex_t = std::uint32_t(__fastcall*)(std::uint8_t* cross, std::uint8_t cursor);
    using GetItemId_t        = int(__fastcall*)(void* itemList, std::uint32_t listIndex);
    using GetUtility_t       = void**(__fastcall*)();
    using AssignedButton_t   = std::uint32_t(__fastcall*)(void** uiUtility, std::uint64_t name);
    using PressByNumber_t    = bool(__fastcall*)(void** uix, std::uint32_t button);
    using PressByName_t      = bool(__fastcall*)(void** uix, std::uint64_t name);

    constexpr std::size_t   kCrossCursor    = 0x250;
    constexpr std::size_t   kCrossUix       = 0xF8;
    constexpr std::size_t   kCrossItemList  = 0x110;
    constexpr std::size_t   kCrossPanels    = 0xE98;
    constexpr std::size_t   kPanelSize      = 0x1E0;
    constexpr std::size_t   kPanelListIndex = 0xC;
    constexpr std::uint32_t kNoPanel        = 8;

    constexpr std::size_t kAssignedButtonSlot = 0x48;   // UiUtility::GetAssignedButtonNumber
    constexpr std::size_t kPressByNumberSlot  = 0x28;
    constexpr std::size_t kPressByNameSlot    = 0x30;
    constexpr std::size_t kMouseByNameSlot    = 0x188;

    constexpr std::uint64_t kUseButton      = 0x887D1837248Aull;   // PL_UI_DECIDE, the item's "Use" prompt
    constexpr std::uint64_t kUseButtonWheel = 0xE74E4D92EFE4ull;   // the wheel also takes these two (names not in the exe)
    constexpr std::uint64_t kUseMouseWheel  = 0x7F4D42602D29ull;
    constexpr std::uint32_t kWheelQuietFrames = 2;

    const std::uint8_t kPrologue[] = { 0x40, 0x57, 0x48, 0x83, 0xEC, 0x70, 0x48, 0x8B, 0x05, 0x00, 0x00, 0x00, 0x00,
                                       0x48, 0x33, 0xC4, 0x48, 0x89, 0x44, 0x24, 0x48, 0x0F, 0xB6, 0x91, 0x50, 0x02, 0x00, 0x00 };
    const std::uint8_t kPrologueMask[] = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0,
                                           1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 };

    SpecialItemAct_t g_Orig   = nullptr;
    void*            g_Target = nullptr;

    std::atomic<std::uint32_t> g_PlayerFrame{ 0 };
    std::atomic<std::uint32_t> g_WheelFrame{ 0 };
    std::atomic<bool>          g_WheelEverOpen{ false };

    template <typename Fn>
    Fn Slot(void** object, std::size_t offset)
    {
        return reinterpret_cast<Fn>((*reinterpret_cast<void***>(object))[offset / sizeof(void*)]);
    }

    bool UseButtonPressedSEH()
    {
        __try
        {
            void** ui  = reinterpret_cast<GetUtility_t>(ResolveGameAddress(gAddr.Uix_UiUtility_GetUiUtility))();
            void** uix = reinterpret_cast<GetUtility_t>(ResolveGameAddress(gAddr.GetUixUtilityToFeedQuarkEnvironment))();
            if (!ui || !uix)
                return false;
            const std::uint32_t button = Slot<AssignedButton_t>(ui, kAssignedButtonSlot)(ui, kUseButton);
            return Slot<PressByNumber_t>(uix, kPressByNumberSlot)(uix, button);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    bool WheelUsePressedSEH(std::uint8_t* cross)   // the same three reads SpecialItemAct makes
    {
        if (UseButtonPressedSEH())
            return true;
        __try
        {
            void** uix = *reinterpret_cast<void***>(cross + kCrossUix);
            return uix && (Slot<PressByName_t>(uix, kPressByNameSlot)(uix, kUseButtonWheel)
                           || Slot<PressByName_t>(uix, kMouseByNameSlot)(uix, kUseMouseWheel));
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    int CursorItemSEH(std::uint8_t* cross)
    {
        __try
        {
            const auto panelIndex = reinterpret_cast<CursorPanelIndex_t>(ResolveGameAddress(gAddr.EquipCrossEvCall_CursorPanelIndex))(
                cross, cross[kCrossCursor]);
            if (panelIndex >= kNoPanel)
                return 0;
            const std::uint8_t* panel = cross + kCrossPanels + panelIndex * kPanelSize;
            const auto listIndex = *reinterpret_cast<const std::uint32_t*>(panel + kPanelListIndex);
            return reinterpret_cast<GetItemId_t>(ResolveGameAddress(gAddr.Barrier_GetItemId))(
                *reinterpret_cast<void**>(cross + kCrossItemList), listIndex);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    }

    void SendItemUse(int item, int fromWheel)
    {
        bool lastOne = false;
        if (!ItemCount::TakeUse(static_cast<std::uint16_t>(item), lastOne))
            return;
        V_FrameWork::EmitMessage("Player", "ItemUse", item, fromWheel);
        if (lastOne)
            V_FrameWork::EmitMessage("Player", "ItemCountEmpty", item);
    }

    void __fastcall hkSpecialItemAct(std::uint8_t* cross)   // runs every frame the item wheel is open
    {
        g_WheelFrame.store(g_PlayerFrame.load());
        g_WheelEverOpen.store(true);
        if (cross && !MissionCodeGuard::ShouldBypassHooks() && WheelUsePressedSEH(cross))
        {
            const int item = CursorItemSEH(cross);
            if (item > 0)
                SendItemUse(equip::FromEngine(item), 1);
        }
        g_Orig(cross);
    }
}

void ItemUse::OnPlayerFrame(std::uint16_t selectedItem)
{
    const std::uint32_t frame = g_PlayerFrame.fetch_add(1) + 1;
    if (!g_Target || selectedItem == 0 || MissionCodeGuard::ShouldBypassHooks())
        return;
    if (g_WheelEverOpen.load() && frame - g_WheelFrame.load() <= kWheelQuietFrames)
        return;   // the wheel handles this press
    if (UseButtonPressedSEH())
        SendItemUse(static_cast<int>(selectedItem), 0);
}

bool Install_EquipCrossEvCall_SpecialItemAct_Hook()
{
    void* target = gAddr.EquipCrossEvCall_SpecialItemAct ? ResolveGameAddress(gAddr.EquipCrossEvCall_SpecialItemAct) : nullptr;
    if (!target || !gAddr.EquipCrossEvCall_CursorPanelIndex || !gAddr.Barrier_GetItemId || !gAddr.Uix_UiUtility_GetUiUtility)
    {
        LogError("ItemUse: item wheel addresses missing for %s, Player.ItemUse is off", GetGameBuildName(gGameBuild));
        return false;
    }
    const HookEntry entry = CheckHookEntryMasked(target, kPrologue, kPrologueMask, sizeof(kPrologue));
    if (entry == HookEntry::Unknown)
    {
        LogError("ItemUse: EquipCrossEvCall::SpecialItemAct does not match this build, Player.ItemUse is off");
        return false;
    }
    if (entry == HookEntry::ForeignHook)
        LogHookedOnTop("ItemUse: EquipCrossEvCall::SpecialItemAct", target);
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hkSpecialItemAct), reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("ItemUse: SpecialItemAct hook failed, Player.ItemUse is off");
        return false;
    }
    g_Target = target;
    return true;
}

void Uninstall_EquipCrossEvCall_SpecialItemAct_Hook()
{
    if (g_Target)
        DisableAndRemoveHook(g_Target);
    g_Target = nullptr;
    g_Orig = nullptr;
}
