#include <pch.h>
#include "SupportHeliDevelopTab.h"

#include <Windows.h>
#include <cstdint>
#include <cstring>

#include "FoxHashes.h"
#include "MissionCodeGuard.h"
#include "SupportHeliHookInstall.h"
#include "SupportHeliParts.h"

namespace
{
    using GridStep_t    = void(__fastcall*)(std::uint8_t* grid);
    using SetupTabBar_t = void(__fastcall*)(std::uint8_t* grid, std::uint8_t* tabBar);
    using CountBadge_t  = char(__fastcall*)(std::uint8_t* grid, std::uint32_t developType, int* developable, int* newDevelopable, int* developed);
    using FillGrid_t    = void(__fastcall*)(std::uint8_t* grid, char selectFirst);
    using LangText_t    = const char*(__fastcall*)(void* text, std::uint64_t langId);
    using SetText_t     = void(__fastcall*)(void* text, void* node, void* layout, const char* string, std::uint8_t show);
    using NodeStep_t    = void(__fastcall*)(void* text, void* node);
    using RowAnswer_t      = bool(__fastcall*)(std::uint8_t* grid, std::uint32_t row);
    using DecideState_t    = std::int32_t(__fastcall*)(std::uint8_t* grid, std::uint32_t row, std::uint32_t checkOnly);
    using DevelopIdOfRow_t = std::uint16_t(__fastcall*)(void* develop, std::uint32_t row);

    constexpr std::size_t   kGridPage         = 0x2140;
    constexpr std::size_t   kGridDevelopType  = 0x2144;
    constexpr std::size_t   kGridFlags        = 0x2138;
    constexpr std::size_t   kGridTabCount     = 0x2190;
    constexpr std::size_t   kGridTabIds       = 0x2191;
    constexpr std::size_t   kGridTab          = 0x198;     // the tab on screen
    constexpr std::size_t   kGridTabRecords   = 0x1E8;     // one pointer per tab
    constexpr std::size_t   kGridTabIconUv    = 0x270;     // 16 bytes per tab
    constexpr std::size_t   kGridTabIconFile  = 0x370;     // 8 bytes per tab
    constexpr std::size_t   kGridDevelop      = 0x38;
    constexpr std::size_t   kGridText         = 0x80;
    constexpr std::size_t   kGridLayout       = 0xB0;
    constexpr std::size_t   kGridTitleNode    = 0x160;
    constexpr std::size_t   kGridListNode     = 0xC78;
    constexpr std::size_t   kGridFlatNode     = 0xC80;
    constexpr std::size_t   kTabBarOneIcon    = 0x38;
    constexpr std::size_t   kTabBarIconFiles  = 0x40;
    constexpr std::size_t   kTabHasBadge      = 0x20;
    constexpr std::size_t   kTabDevelopable   = 0x68;
    constexpr std::size_t   kTabLayoutA       = 0x70;
    constexpr std::size_t   kTabLayoutB       = 0x78;
    constexpr std::size_t   kTabDeveloped     = 0x88;
    constexpr std::size_t   kTextLang         = 0x750 / sizeof(void*);
    constexpr std::size_t   kTextSet          = 0x708 / sizeof(void*);
    constexpr std::size_t   kTextHide         = 0x6A8 / sizeof(void*);
    constexpr std::size_t   kTextShow         = 0x678 / sizeof(void*);
    constexpr std::size_t   kDevelopIdOfRow   = 0x128 / sizeof(void*);
    constexpr std::uint16_t kGatlingDevelopId = 33001;     // GATLING-GUN: the grid tags every developed gatling row as equipped
    constexpr std::int32_t  kDecideOffersEquip = 0;        // the equip dialog in the ACC, the supply-drop dialog in the field
    constexpr std::int32_t  kDecideRefused     = 3;
    constexpr std::int16_t  kHeliPage         = 2;
    constexpr std::uint32_t kHeliDevelopType  = 0x1F;      // TppMbDev.EQP_DEV_TYPE_Heli
    constexpr std::uint32_t kOutfitDevelopType = 0x20;     // TppMbDev.EQP_DEV_TYPE_HeliBody, no vanilla row uses it
    constexpr std::uint8_t  kOutfitTab        = 1;
    constexpr std::uint64_t kHeliTabIcon      = 0x15695EE3555FC6A8;   // the icon file the one-tab heli page shows
    constexpr std::uint64_t kLangMask         = 0xFFFFFFFFFFFF;
    constexpr std::uint8_t  kDisplayFlat      = 4;

    constexpr std::uint8_t kBuildSectionsEntry[] = { 0x40, 0x53, 0x48, 0x81, 0xEC, 0xB0, 0x01, 0x00, 0x00, 0x48, 0x8B, 0x05 };
    constexpr std::uint8_t kSetupTabBarEntry[]   = { 0x40, 0x53, 0x55, 0x57, 0x48, 0x81, 0xEC, 0x90, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x05 };
    constexpr std::uint8_t kChangeSectionEntry[] = { 0x4C, 0x8B, 0xDC, 0x55, 0x53, 0x49, 0x8D, 0xAB, 0x48, 0xFF, 0xFF, 0xFF, 0x48, 0x81, 0xEC, 0xA8,
                                                     0x01, 0x00, 0x00, 0x48, 0x8B, 0x05 };
    constexpr std::uint8_t kIsRowEquippedEntry[] = { 0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x56, 0x48, 0x8D, 0x6C, 0x24, 0xC9, 0x48, 0x81, 0xEC, 0xA0 };
    constexpr std::uint8_t kDecideStateEntry[]   = { 0x48, 0x89, 0x5C, 0x24, 0x18, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57 };

    GridStep_t    g_OrigBuildSections = nullptr;
    SetupTabBar_t g_OrigSetupTabBar   = nullptr;
    GridStep_t    g_OrigChangeSection = nullptr;
    void*         g_BuildSectionsTarget = nullptr;
    void*         g_SetupTabBarTarget   = nullptr;
    void*         g_ChangeSectionTarget = nullptr;
    CountBadge_t  g_CountBadge = nullptr;
    FillGrid_t    g_FillGrid   = nullptr;
    RowAnswer_t   g_OrigIsRowEquipped = nullptr;
    void*         g_IsRowEquippedTarget = nullptr;
    DecideState_t g_OrigDecideState     = nullptr;
    void*         g_DecideStateTarget   = nullptr;

    template <typename T>
    T& At(std::uint8_t* base, std::size_t offset)
    {
        return *reinterpret_cast<T*>(base + offset);
    }

    bool HasOutfitTab(std::uint8_t* grid)
    {
        return grid && At<std::int16_t>(grid, kGridPage) == kHeliPage && !MissionCodeGuard::ShouldBypassHooks()
            && SupportHeliParts::HasAny(SupportHeliParts::Kind::Outfit);
    }

    void __fastcall hkBuildSections(std::uint8_t* grid)
    {
        g_OrigBuildSections(grid);
        if (!HasOutfitTab(grid))
            return;
        grid[kGridTabCount]   = 2;
        grid[kGridTabIds]     = 0;
        grid[kGridTabIds + 1] = kOutfitTab;
        std::memcpy(grid + kGridTabIconUv + 16, grid + kGridTabIconUv, 16);
    }

    void __fastcall hkSetupTabBar(std::uint8_t* grid, std::uint8_t* tabBar)
    {
        g_OrigSetupTabBar(grid, tabBar);
        if (!tabBar || !HasOutfitTab(grid) || grid[kGridTabCount] != 2)
            return;
        At<std::uint64_t>(grid, kGridTabIconFile)     = kHeliTabIcon;   // a page with tabs lists one icon file per tab
        At<std::uint64_t>(grid, kGridTabIconFile + 8) = kHeliTabIcon;
        At<std::uint64_t>(tabBar, kTabBarOneIcon)     = 0;
        At<std::uint8_t*>(tabBar, kTabBarIconFiles)   = grid + kGridTabIconFile;
    }

    void SetTabBadge(std::uint8_t* grid, std::size_t tab, std::uint32_t developType)
    {
        std::uint8_t* record = At<std::uint8_t*>(grid, kGridTabRecords + tab * sizeof(void*));
        if (!record || !g_CountBadge)
            return;
        int developable = 0;
        int newDevelopable = 0;
        int developed = 0;
        const char any = g_CountBadge(grid, developType, &developable, &newDevelopable, &developed);
        record[kTabHasBadge] = any ? 0x40 : 0;
        At<int>(record, kTabDevelopable)  = developable;
        At<void*>(record, kTabLayoutA)    = At<void*>(grid, kGridLayout);
        At<void*>(record, kTabLayoutB)    = At<void*>(grid, kGridLayout);
        At<int>(record, kTabDeveloped)    = developed;
    }

    const char* OutfitTabTitle(void* text)
    {
        const auto lang = reinterpret_cast<LangText_t>((*static_cast<void***>(text))[kTextLang]);
        const char* title = lang(text, FoxHashes::StrCode64("V_dev_heli_outfit") & kLangMask);
        if (!title || !*title)
            title = lang(text, FoxHashes::StrCode64("cmmn_suit") & kLangMask);   // "Uniform", until the V_FrameWork lang file has the tab's own title
        return title;
    }

    void ShowOutfitTab(std::uint8_t* grid)
    {
        void* text = At<void*>(grid, kGridText);
        void** textCalls = *static_cast<void***>(text);
        const std::uint32_t shownBefore = At<std::uint32_t>(grid, kGridDevelopType);
        At<std::uint32_t>(grid, kGridDevelopType) = kOutfitDevelopType;

        SetTabBadge(grid, 0, kHeliDevelopType);
        SetTabBadge(grid, kOutfitTab, kOutfitDevelopType);
        reinterpret_cast<SetText_t>(textCalls[kTextSet])(text, At<void*>(grid, kGridTitleNode), At<void*>(grid, kGridLayout),
                                                         OutfitTabTitle(text), 1);
        if (shownBefore == 0)   // the page came from the flat list: switch its nodes to the grid, as the game does for a typed tab
        {
            reinterpret_cast<NodeStep_t>(textCalls[kTextHide])(text, At<void*>(grid, kGridFlatNode));
            reinterpret_cast<NodeStep_t>(textCalls[kTextShow])(text, At<void*>(grid, kGridListNode));
        }
        g_FillGrid(grid, (At<std::uint32_t>(grid, kGridFlags) & kDisplayFlat) == 0);
    }

    std::uint16_t DevelopIdOfRow(std::uint8_t* grid, std::uint32_t row)
    {
        void* develop = At<void*>(grid, kGridDevelop);
        return develop ? reinterpret_cast<DevelopIdOfRow_t>((*static_cast<void***>(develop))[kDevelopIdOfRow])(develop, row & 0xFFFF) : 0;
    }

    bool __fastcall hkIsRowEquipped(std::uint8_t* grid, std::uint32_t row)
    {
        const bool equipped = g_OrigIsRowEquipped(grid, row);
        if (!grid || MissionCodeGuard::ShouldBypassHooks())
            return equipped;
        const std::uint16_t developId = DevelopIdOfRow(grid, row);
        SupportHeliParts::Part part{};
        SupportHeliParts::Part picked{};
        if (SupportHeliParts::FindByDevelopId(developId, part))
            return SupportHeliParts::PickedPart(part.kind, picked) && picked.id == part.id;
        if (developId == kGatlingDevelopId && equipped)
            return !SupportHeliParts::PickedPart(SupportHeliParts::Kind::Gatling, picked);   // a picked custom gatling replaces the heli's own
        return equipped;
    }

    // a developed custom heli part is picked in Customize only: no equip dialog in the ACC, no supply drop in the field
    std::int32_t __fastcall hkDecideState(std::uint8_t* grid, std::uint32_t row, std::uint32_t checkOnly)
    {
        const std::int32_t state = g_OrigDecideState(grid, row, checkOnly);
        if (state != kDecideOffersEquip || !grid || MissionCodeGuard::ShouldBypassHooks())
            return state;
        SupportHeliParts::Part part{};
        return SupportHeliParts::FindByDevelopId(DevelopIdOfRow(grid, row), part) ? kDecideRefused : state;
    }

    void __fastcall hkChangeSection(std::uint8_t* grid)
    {
        if (!HasOutfitTab(grid) || grid[kGridTabCount] != 2 || !g_FillGrid)
        {
            g_OrigChangeSection(grid);
            return;
        }
        if (At<std::uint32_t>(grid, kGridTab) == kOutfitTab)
        {
            ShowOutfitTab(grid);
            return;
        }
        g_OrigChangeSection(grid);
        SetTabBadge(grid, kOutfitTab, kOutfitDevelopType);
    }
}

bool Install_SupportHeliDevelopTab_Hooks()
{
    constexpr const char* kNoEquip = "custom heli parts show no EQUIPPED tag in R&D";
    constexpr const char* kNoRefuse = "R&D offers equip and supply drop for custom heli parts, which picks nothing";
    if (!g_DecideStateTarget)
    {
        g_DecideStateTarget = InstallSupportHeliHook(
            "R&D decide", gAddr.MenuDevelopGrid_GetDecideState, kDecideStateEntry, nullptr, sizeof(kDecideStateEntry),
            reinterpret_cast<void*>(&hkDecideState), reinterpret_cast<void**>(&g_OrigDecideState), kNoRefuse);
    }
    if (!g_IsRowEquippedTarget)
    {
        g_IsRowEquippedTarget = InstallSupportHeliHook(
            "R&D equipped tag", gAddr.MenuDevelopGrid_IsRowEquipped, kIsRowEquippedEntry, nullptr, sizeof(kIsRowEquippedEntry),
            reinterpret_cast<void*>(&hkIsRowEquipped), reinterpret_cast<void**>(&g_OrigIsRowEquipped), kNoEquip);
    }

    g_CountBadge = reinterpret_cast<CountBadge_t>(gAddr.MenuDevelopGrid_CountBadge ? ResolveGameAddress(gAddr.MenuDevelopGrid_CountBadge) : nullptr);
    g_FillGrid   = reinterpret_cast<FillGrid_t>(gAddr.MenuDevelopGrid_FillGrid ? ResolveGameAddress(gAddr.MenuDevelopGrid_FillGrid) : nullptr);
    if (!g_CountBadge || !g_FillGrid)
    {
        LogError("SupportHeliWeapon: R&D grid addresses missing on this build, heli outfits get no R&D tab");
        return false;
    }
    constexpr const char* kNoTab = "heli outfits get no R&D tab";
    if (!g_ChangeSectionTarget)
    {
        g_ChangeSectionTarget = InstallSupportHeliHook(
            "R&D tab change", gAddr.MenuDevelopGrid_ChangeSection, kChangeSectionEntry, nullptr, sizeof(kChangeSectionEntry),
            reinterpret_cast<void*>(&hkChangeSection), reinterpret_cast<void**>(&g_OrigChangeSection), kNoTab);
    }
    if (g_ChangeSectionTarget && !g_SetupTabBarTarget)
    {
        g_SetupTabBarTarget = InstallSupportHeliHook(
            "R&D tab bar", gAddr.MenuDevelopGrid_SetupTabBar, kSetupTabBarEntry, nullptr, sizeof(kSetupTabBarEntry),
            reinterpret_cast<void*>(&hkSetupTabBar), reinterpret_cast<void**>(&g_OrigSetupTabBar), kNoTab);
    }
    if (g_SetupTabBarTarget && !g_BuildSectionsTarget)   // last: without the two hooks above a second tab would have no icon and no rows
    {
        g_BuildSectionsTarget = InstallSupportHeliHook(
            "R&D tab list", gAddr.MenuDevelopGrid_BuildSections, kBuildSectionsEntry, nullptr, sizeof(kBuildSectionsEntry),
            reinterpret_cast<void*>(&hkBuildSections), reinterpret_cast<void**>(&g_OrigBuildSections), kNoTab);
    }
    return g_BuildSectionsTarget != nullptr;
}

void Uninstall_SupportHeliDevelopTab_Hooks()
{
    UninstallSupportHeliHook(g_DecideStateTarget);
    UninstallSupportHeliHook(g_IsRowEquippedTarget);
    UninstallSupportHeliHook(g_BuildSectionsTarget);
    UninstallSupportHeliHook(g_SetupTabBarTarget);
    UninstallSupportHeliHook(g_ChangeSectionTarget);
}
