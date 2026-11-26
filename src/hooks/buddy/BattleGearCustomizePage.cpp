#include <pch.h>

#include "BattleGearCustomizePage.h"

#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "BattleGearArmory.h"
#include "BattleGearBuddy.h"
#include "BattleGearLoadoutSlots.h"
#include "BattleGearTexts.h"
#include "BattleGearWeapons.h"
#include "BuddyCodeCave.h"
#include "FoxPathInternal.h"
#include "HookUtils.h"
#include "NativeCallPatch.h"
#include "V_FrameWorkState.h"
#include "log.h"

namespace
{
    using BuildMenuItems_t       = void(__fastcall*)(std::uint8_t* table);
    using ItemDecided_t          = void(__fastcall*)(std::uint8_t* table, std::uint8_t item);
    using SelectorStep_t         = void(__fastcall*)(std::uint8_t* selector);
    using DetailsStep_t          = void(__fastcall*)(std::uint8_t* details);
    using DecidePart_t           = void(__fastcall*)(std::uint8_t* callback, std::uint32_t index);
    using SlotParts_t            = void(__fastcall*)(std::uint8_t* callback, std::uint8_t slot);
    using PartType_t             = std::uint8_t(__fastcall*)(void* partsSetting, std::uint32_t walkerSlot);
    using GetScriptVars_t        = std::uint8_t*(__fastcall*)();
    using DevelopRecords_t       = std::uint8_t*(__fastcall*)(void* source);
    using DevelopRowOf_t         = std::uint64_t(__fastcall*)(void* source, std::uint16_t developId);
    using DevelopRowDone_t       = std::uint64_t(__fastcall*)(void* source, std::uint16_t row);

    constexpr std::size_t   kMenuItems         = 0x08;
    constexpr std::size_t   kMenuItemSize      = 0x28;
    constexpr int           kMenuItemCount     = 78;
    constexpr std::size_t   kMenuItemName      = 0x0C;
    constexpr std::size_t   kMenuItemTrigger   = 0x20;
    constexpr std::uint32_t kBattleGearItem    = 0xDAF1D1FF;
    constexpr std::uint32_t kWalkerItem        = 0x8D38333D;
    constexpr std::uint64_t kWalkerTrigger     = 0x863496C90234;

    constexpr std::uint64_t kWalkerTitle       = 0x52036C2B48FF;   // mbmenu_title_cstm_dwalker
    constexpr std::uint64_t kBattleGearTitle   = 0x173B72B48085;   // mbmenu_title_cstm_bgear
    constexpr std::uint64_t kWalkerSlotHelp    = 0xE84798E13090;   // mb_cstm_sel_dw_slot
    constexpr std::uint64_t kBattleGearHelp    = 0xE6528861C383;   // mbmenu_title_cstm_bgear_help
    constexpr std::uint64_t kWalkerName        = 0xBF4951C92218;   // name_buddy_dw
    constexpr std::uint64_t kBattleGearName    = 0x12E836C25237;   // name_buddy_bg

    constexpr std::size_t   kSelectorMode      = 0x64;
    constexpr std::uint32_t kWalkerPage        = 0x80000;
    constexpr std::uint32_t kMainWeaponList    = 0x80002;
    constexpr std::uint32_t kSubWeaponList     = 0x80004;
    constexpr std::uint32_t kHeadList          = 0x80010;

    constexpr std::size_t   kListMode          = 0x71C;
    constexpr std::size_t   kRowUsable         = 0x140;
    constexpr std::size_t   kRowRecords        = 0x540;
    constexpr std::size_t   kRowGrayed         = 0xB8;
    constexpr int           kWalkerRows        = 7;
    constexpr int           kMainWeaponRow     = 1;
    constexpr int           kSubWeaponRow      = 2;
    constexpr int           kHeadRow           = 3;
    constexpr int           kConfirmRow        = 6;

    constexpr std::size_t   kListCount         = 0x714;
    constexpr std::size_t   kListRows          = 0x730;   // a part list: one R&D row index per entry,
    constexpr std::size_t   kListEntries       = 0xF30;   // and a 0x1C-byte entry each: kind 2, then the row index again
    constexpr std::size_t   kListEntrySize     = 0x1C;
    constexpr std::uint32_t kEntryIsDevelopRow = 2;
    constexpr std::size_t   kEntryRow          = 4;
    constexpr std::size_t   kListCursor        = 0x560;
    constexpr std::size_t   kListTop           = 0x564;   // the first entry on screen
    constexpr std::size_t   kKeepsCursor       = 0x720;
    constexpr std::size_t   kLayout            = 0x8888;
    constexpr std::uint8_t  kLayoutWithCursor  = 2;
    constexpr std::uint32_t kEntriesOnScreen   = 8;
    constexpr std::uint32_t kMostEntries       = 64;

    constexpr std::size_t   kDetailsMode       = 0xE61;   // the details window: what it lays out,
    constexpr std::size_t   kDetailsParam      = 0xE78;   // the model viewer param,
    constexpr std::size_t   kDetailsDevelop    = 0x40;    // and its R&D table
    constexpr std::uint8_t  kDetailsListMode   = 1;       // a buddy's param from an equipment list
    constexpr std::uint8_t  kDetailsSlotMode   = 6;       // a walker slot's from the slot screen
    constexpr std::size_t   kParamBuddyType    = 0x14;

    constexpr std::size_t   kVarsWalkerGear    = 0x2E57;   // head type and grade, arm type and grade, main weapon type and grade,
    constexpr std::size_t   kWalkerGearBytes   = 8;        // sidearm type and grade
    constexpr std::size_t   kHeadType          = 0;
    constexpr std::size_t   kHeadGrade         = 1;
    constexpr std::size_t   kMainType          = 4;
    constexpr std::size_t   kMainGrade         = 5;
    constexpr std::size_t   kSubType           = 6;
    constexpr std::size_t   kSubGrade          = 7;
    constexpr std::uint8_t  kBodyGroup         = 0x72;     // development groups of the D-Walker body, walker ZE'EV, missile pod
    constexpr std::uint8_t  kZeevGroup         = 0x7C;     // and stun gun
    constexpr std::uint8_t  kMissileGroup      = 0x7E;
    constexpr std::uint8_t  kStunGunGroup      = 0x81;
    constexpr std::uint8_t  kWalkerSlotMain    = 1;
    constexpr std::uint8_t  kWalkerSlotSub     = 2;
    constexpr std::uint8_t  kWalkerSlotHead    = 3;

    constexpr std::size_t   kVarsSlotTypes     = 0x1B2C;   // the D-Walker's three loadout slots in the save: eight part types a slot,
    constexpr std::size_t   kVarsSlotGrades    = 0x1B44;   // then eight grades a slot
    constexpr std::size_t   kSlotParts         = 8;
    constexpr int           kSlotOwnParts      = 3;        // walker slots 1-3: main gun, sidearm, head
    constexpr std::size_t   kPartsSetting      = 0x70;                     // the page's working parts,
    constexpr std::size_t   kPartTypeSlot      = 0xD8 / sizeof(void*);    // their vtable: the type picked for a walker slot

    constexpr std::size_t   kRecordSource      = 0x60;                      // the page's R&D table
    constexpr std::size_t   kRecordsSlot       = 0x718 / sizeof(void*);     // its vtable: the 0x68-byte records, develop id first,
    constexpr std::size_t   kRowOfSlot         = 0xE0 / sizeof(void*);      // the row of a develop id, 0x400 for none,
    constexpr std::size_t   kRowDoneSlot       = 0x1C8 / sizeof(void*);     // and whether a row is developed
    constexpr std::uint32_t kTableRows         = 0x400;
    constexpr std::uint16_t kNoRow             = 0x400;
    constexpr std::size_t   kRecordSize        = 0x68;
    constexpr std::size_t   kRecordGroup       = 0x37;
    constexpr std::size_t   kRecordName        = 0x20;
    constexpr std::size_t   kRecordInfo        = 0x28;
    constexpr std::size_t   kRecordIcon        = 0x60;
    constexpr std::uint64_t kLangMask          = battleGearTexts::kLangMask;
    constexpr int           kMostTextSwaps     = 64;
    constexpr int           kTextGroups        = 4;

    // develop ids, best grade first: the D-Walker rows the built-in guns and the three heads show on the page
    constexpr std::uint16_t kStunGunRows[]     = { 31006, 31005, 31004 };
    constexpr std::uint16_t kMissilePodRows[]  = { 30006, 30005, 30004 };
    constexpr std::uint16_t kZeevRows[]        = { 29008 };
    constexpr std::uint16_t kSupportRows[]     = { 27002, 27001 };
    constexpr std::uint16_t kScoutingRows[]    = { 27004 };
    constexpr std::uint16_t kInterceptRows[]   = { 27006 };

    const std::uint8_t kBuildMenuItemsEntry[]       = { 0x40, 0x55, 0x48, 0x8D, 0xAC, 0x24, 0x10, 0xF8, 0xFF, 0xFF, 0x48, 0x81, 0xEC, 0xF0,
                                                        0x08, 0x00 };
    // movzx eax, dl / lea rdx, [rax+rax*4] / inc dword [rcx+rdx*8+0x10] / ret
    const std::uint8_t kItemDecidedEntry[]          = { 0x0F, 0xB6, 0xC2, 0x48, 0x8D, 0x14, 0x80, 0xFF, 0x44, 0xD1, 0x10, 0xC3 };
    const std::uint8_t kSetupDetailsEntry[]         = { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x0F, 0xB6, 0x81, 0x61, 0x0E, 0x00, 0x00,
                                                        0x48, 0x8B, 0xD9 };
    const std::uint8_t kOpenEntry[]                 = { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0x48, 0xB9, 0xA7, 0x5F };
    const std::uint8_t kEndEntry[]                  = { 0x40, 0x53, 0x57, 0x48, 0x83, 0xEC, 0x28, 0x48, 0x8B, 0xF9, 0x48, 0xB9 };
    const std::uint8_t kDecidePartEntry[]           = { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74,
                                                        0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x8B, 0xC2, 0x33, 0xFF, 0x48, 0x8B,
                                                        0xF1, 0x0F, 0xB7, 0xAC, 0x41, 0x30, 0x07, 0x00, 0x00 };
    const std::uint8_t kSlotPartsEntry[]            = { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74,
                                                        0x24, 0x18, 0x48, 0x89, 0x7C, 0x24, 0x20, 0x41, 0x54, 0x41, 0x56, 0x41, 0x57,
                                                        0x48, 0x83, 0xEC, 0x20 };
    // sub cl, 0x7D / cmp cl, 5 / ja skip: the D-Walker main weapon list takes development groups 0x7D-0x82
    constexpr std::uint8_t kMainWeaponFilterSite[]  = { 0x80, 0xE9, 0x7D, 0x80, 0xF9, 0x05, 0x77, 0x57 };
    constexpr std::size_t  kFilterSkip              = sizeof(kMainWeaponFilterSite) + 0x57;
    // mov rax, [rip+x] / ret
    constexpr std::uint8_t kGetScriptVarsHead[]     = { 0x48, 0x8B, 0x05 };
    constexpr std::size_t  kGetScriptVarsRet        = 7;
    // lea eax, [rdi-0xA] / test al, 0xFD / jne: the menu keeps items 10 (Quiet) and 12 (BATTLE GEAR) hidden and inactive behind flags its
    // refresh always sets; testing 0xFF leaves only Quiet behind them, so BATTLE GEAR shows and opens whenever the Battle Gear can sortie
    constexpr std::uint8_t kQuietGateSite[]         = { 0x8D, 0x47, 0xF6, 0xA8, 0xFD, 0x75 };
    constexpr std::uint8_t kQuietGateOpen[]         = { 0x8D, 0x47, 0xF6, 0xA8, 0xFF, 0x75 };
    // every visibility test first asks vt+0x150, which looks the item up in this byte table from item 10 on: 0 hides it for good
    // (Quiet 10 and BATTLE GEAR 12 among them). 1 for item 12 leaves BATTLE GEAR to its flags.
    constexpr std::uint8_t kCutItemIds[]            = { 0x00, 0x01, 0x00, 0x01 };
    constexpr std::uint8_t kCutItemIdsBattleGear[]  = { 0x00, 0x01, 0x01, 0x01 };
    // the same table at items 52 and 53: Missions > BUDDY SUPPORT's BATTLE GEAR and DISMISS BATTLE GEAR
    constexpr std::size_t  kCutCallRowsAt           = 52 - 10;
    constexpr std::uint8_t kCutCallRows[]           = { 0x00, 0x00 };
    constexpr std::uint8_t kCutCallRowsBattleGear[] = { 0x01, 0x01 };
    // cmp dword [rax+0x14], 4 / jne: the details window lays a walker out only for a param of buddy type 4
    constexpr std::uint8_t kDetailsWalkerCheck[]    = { 0x83, 0x78, 0x14, 0x04, 0x75, 0x12 };
    constexpr std::uint8_t kRecordsGetter[]         = { 0x48, 0x8D, 0x41, 0x08, 0xC3 };   // lea rax, [rcx+8] / ret
    constexpr std::size_t  kMovImm64Size            = 10;     // movabs reg, imm64
    constexpr std::uint8_t kRexB                    = 0x01;

    struct Hook
    {
        const char* what;
        void*       target;
        void*       orig;
    };

    Hook g_BuildMenu{ "iDroid menu table" };
    Hook g_ItemDecided{ "iDroid menu decide" };
    Hook g_Open{ "customize page open" };
    Hook g_End{ "customize page end" };
    Hook g_Abort{ "customize page abort" };
    Hook g_Decide{ "customize part decide" };
    Hook g_LoadSlot{ "customize slot load" };
    Hook g_SaveSlot{ "customize slot save" };
    Hook g_SetupDetails{ "details window setup" };
    BuddyCodeCave   g_MainWeaponFilter{ "Battle Gear customize main weapon list" };
    BuddyCodeCave   g_PageTitle{ "Battle Gear customize page title" };
    BuddyCodeCave   g_SlotHelp{ "Battle Gear customize slot help" };
    BuddyCodeCave   g_SlotListTitle{ "Battle Gear customize slot list title" };
    BuddyCodeCave   g_DetailsLayout{ "Battle Gear details layout" };
    BuddyCodeCave   g_DetailsTitle{ "Battle Gear details title" };
    NativeCodePatch g_VisibleGate{};
    NativeCodePatch g_ActiveGate{};
    NativeCodePatch g_CutItemGate{};
    NativeCodePatch g_CutCallGate{};
    GetScriptVars_t g_GetScriptVars = nullptr;

    struct TextSwap
    {
        std::uint64_t* at;
        std::uint64_t  was;
    };

    struct PageTexts   // what the rows of a D-Walker development group show while the Battle Gear's page is open
    {
        std::uint8_t  group;
        std::uint64_t name;
        std::uint64_t info;
        std::uint64_t icon;   // 0 keeps the D-Walker's
    };

    struct ListedPart
    {
        std::uint16_t row;
        std::uint8_t  value;   // the Battle Gear gun or the head type the entry stands for
    };

    std::atomic<bool> g_Requested{ false };   // the BATTLE GEAR item was decided; the next walker page is the Battle Gear's
    std::uint8_t      g_PageActive = 0;       // the main weapon filter and the title caves read it as a byte
    std::uint8_t      g_DetailsBattleGear = 0;   // the details window is laying out a Battle Gear param; its title cave reads it
    std::uint8_t      g_WalkerGear[kWalkerGearBytes] = {};   // the D-Walker's own picks while the Battle Gear page is open
    TextSwap          g_TextSwaps[kMostTextSwaps] = {};
    int               g_TextSwapCount = 0;
    bool              g_TextsSwapped = false;
    ListedPart        g_Listed[kMostEntries] = {};   // the part list on screen, as this page filled it
    std::uint32_t     g_ListedCount = 0;
    std::uint32_t     g_ListedMode = 0;
    std::atomic<std::uint8_t> g_PickedMain{ 0 };     // the guns picked on the open page
    std::atomic<std::uint8_t> g_PickedRoof{ 0 };

    // the D-Walker rows that stand in for the Battle Gear's own parts carry its texts while its page is open: every slot of a
    // record that holds the row's name gets the Battle Gear's name, every one that holds its description the description
    void SwapTextsSEH(std::uint8_t* records, const PageTexts* texts, int textCount, TextSwap* swaps, int& swapCount)
    {
        __try
        {
            for (std::uint32_t row = 0; row < kTableRows; ++row)
            {
                std::uint8_t* record = records + static_cast<std::size_t>(row) * kRecordSize;
                if (*reinterpret_cast<const std::uint16_t*>(record) == 0)
                    continue;
                const PageTexts* own = nullptr;
                for (int i = 0; i < textCount; ++i)
                    if (texts[i].group == record[kRecordGroup])
                        own = &texts[i];
                if (!own)
                    continue;
                const std::uint64_t name = *reinterpret_cast<const std::uint64_t*>(record + kRecordName) & kLangMask;
                const std::uint64_t info = *reinterpret_cast<const std::uint64_t*>(record + kRecordInfo) & kLangMask;
                for (std::size_t at = 0; at + sizeof(std::uint64_t) <= kRecordSize && swapCount < kMostTextSwaps;
                     at += sizeof(std::uint64_t))
                {
                    auto* slot = reinterpret_cast<std::uint64_t*>(record + at);
                    const std::uint64_t held = *slot & kLangMask;
                    const std::uint64_t to = held == 0 ? 0 : held == name ? own->name : held == info ? own->info : 0;
                    if (to == 0)
                        continue;
                    swaps[swapCount++] = { slot, *slot };
                    *slot = (*slot & ~kLangMask) | to;
                }
                if (own->icon && swapCount < kMostTextSwaps)
                {
                    auto* slot = reinterpret_cast<std::uint64_t*>(record + kRecordIcon);
                    swaps[swapCount++] = { slot, *slot };
                    *slot = (*slot & ~battleGearTexts::kPathStemMask) | own->icon;
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    void RestoreTextsSEH(const TextSwap* swaps, int& swapCount)
    {
        __try
        {
            for (int i = swapCount - 1; i >= 0; --i)
                *swaps[i].at = swaps[i].was;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
        swapCount = 0;
    }

    void RestorePageTexts()
    {
        RestoreTextsSEH(g_TextSwaps, g_TextSwapCount);
        g_TextsSwapped = false;
    }

    void* DevelopSourceSEH(const std::uint8_t* callback)
    {
        __try
        {
            return *reinterpret_cast<void* const*>(callback + kRecordSource);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    std::uint8_t* DevelopRecordsSEH(void* source)   // only from the R&D table class that hands out its own records
    {
        __try
        {
            void* getter = (*static_cast<void***>(source))[kRecordsSlot];
            if (std::memcmp(getter, kRecordsGetter, sizeof(kRecordsGetter)) != 0)
                return nullptr;
            return reinterpret_cast<DevelopRecords_t>(getter)(source);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    std::uint16_t RowOfSEH(void* source, std::uint16_t developId)
    {
        __try
        {
            return static_cast<std::uint16_t>(reinterpret_cast<DevelopRowOf_t>((*static_cast<void***>(source))[kRowOfSlot])(source, developId));
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return kNoRow;
        }
    }

    bool RowDevelopedSEH(void* source, std::uint16_t row)
    {
        __try
        {
            return static_cast<std::uint8_t>(reinterpret_cast<DevelopRowDone_t>((*static_cast<void***>(source))[kRowDoneSlot])(source, row)) != 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    // a text or icon the installed packs do not carry yet leaves what the page showed before: the gun's name for its description,
    // the D-Walker's body texts and icons
    int BattleGearPageTexts(PageTexts (&texts)[kTextGroups])
    {
        const auto text = [](std::uint64_t own, std::uint64_t without) { return battleGearArmory::TextInstalled(own) ? own : without; };
        const auto icon = [](std::uint64_t own) { return battleGearArmory::IconInstalled(own) ? own : 0; };
        texts[0] = { kMissileGroup, battleGearTexts::kHMissileName, text(battleGearTexts::kHMissileInfo, battleGearTexts::kHMissileName),
                     icon(battleGearTexts::kHMissileIcon) };
        texts[1] = { kStunGunGroup, battleGearTexts::kRailGunName, text(battleGearTexts::kRailGunInfo, battleGearTexts::kRailGunName),
                     icon(battleGearTexts::kRailGunIcon) };
        int count = 2;
        if (battleGearArmory::TextInstalled(battleGearTexts::kBodyName) && battleGearArmory::TextInstalled(battleGearTexts::kBodyInfo))
            texts[count++] = { kBodyGroup, battleGearTexts::kBodyName, battleGearTexts::kBodyInfo, icon(battleGearTexts::kBodyIcon) };
        if (battleGearArmory::TextInstalled(battleGearTexts::kRoofMgName) && battleGearArmory::TextInstalled(battleGearTexts::kRoofMgInfo))
            texts[count++] = { kZeevGroup, battleGearTexts::kRoofMgName, battleGearTexts::kRoofMgInfo, icon(battleGearTexts::kRoofMgIcon) };
        return count;
    }

    void SwapPageTexts(void* source)
    {
        if (g_TextsSwapped)
            return;
        g_TextsSwapped = true;
        PageTexts texts[kTextGroups];
        const int count = BattleGearPageTexts(texts);
        if (std::uint8_t* records = DevelopRecordsSEH(source))
            SwapTextsSEH(records, texts, count, g_TextSwaps, g_TextSwapCount);
#ifdef _DEBUG
        LogDebug("BattleGear customize page: %d text and icon slots of %d row groups switched to the Battle Gear's", g_TextSwapCount, count);
#endif
    }

    template <std::size_t N>
    std::uint16_t ShownRow(void* source, const std::uint16_t (&developIds)[N])   // the best developed grade, the base grade when none is
    {
        std::uint16_t base = kNoRow;
        for (const std::uint16_t developId : developIds)
        {
            const std::uint16_t row = RowOfSEH(source, developId);
            if (row == kNoRow)
                continue;
            if (RowDevelopedSEH(source, row))
                return row;
            base = row;
        }
        return base;
    }

    std::uint32_t ListMainWeapons(void* source, ListedPart* parts, bool withRegistered)
    {
        std::uint32_t count = 0;
        for (const battleGearArmory::MainWeapon& weapon : battleGearArmory::All())
        {
            if (count == kMostEntries)
                break;
            std::uint16_t row = kNoRow;
            if (weapon.BuiltIn())
                row = weapon.fireType == battleGearArmory::kHMissile ? ShownRow(source, kMissilePodRows) : ShownRow(source, kStunGunRows);
            else if (withRegistered && fox::detail::PathExistsByCode(weapon.fpkPath))
            {
                row = RowOfSEH(source, weapon.developId);
                if (row != kNoRow && !RowDevelopedSEH(source, row))
                    row = kNoRow;
            }
            if (row != kNoRow)
                parts[count++] = { row, weapon.value };
        }
        return count;
    }

    std::uint32_t ListRoofGuns(void* source, ListedPart* parts)
    {
        std::uint32_t count = 0;
        for (const battleGearArmory::RoofGun& gun : battleGearArmory::AllRoofGuns())
        {
            if (count == kMostEntries)
                break;
            std::uint16_t row = gun.BuiltIn() ? ShownRow(source, kZeevRows) : RowOfSEH(source, gun.developId);
            if (!gun.BuiltIn() && row != kNoRow && !RowDevelopedSEH(source, row))
                row = kNoRow;
            if (row != kNoRow)
                parts[count++] = { row, gun.value };
        }
        return count;
    }

    std::uint32_t ListHeads(void* source, ListedPart* parts)
    {
        const std::uint16_t rows[] = { ShownRow(source, kSupportRows), ShownRow(source, kScoutingRows), ShownRow(source, kInterceptRows) };
        const std::uint8_t heads[] = { battleGearWeapons::kSupportHead, battleGearWeapons::kScoutingHead, battleGearWeapons::kInterceptHead };
        std::uint32_t count = 0;
        for (int i = 0; i < 3; ++i)
            if (rows[i] != kNoRow)
                parts[count++] = { rows[i], heads[i] };
        return count;
    }

    std::uint32_t ListModeSEH(const std::uint8_t* callback)
    {
        __try
        {
            return *reinterpret_cast<const std::uint32_t*>(callback + kListMode);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    void WriteListSEH(std::uint8_t* callback, const ListedPart* parts, std::uint32_t count, std::uint32_t picked)
    {
        __try
        {
            for (std::uint32_t i = 0; i < count; ++i)
            {
                *reinterpret_cast<std::uint16_t*>(callback + kListRows + i * sizeof(std::uint16_t)) = parts[i].row;
                std::uint8_t* entry = callback + kListEntries + static_cast<std::size_t>(i) * kListEntrySize;
                std::memset(entry, 0, kListEntrySize);
                *reinterpret_cast<std::uint32_t*>(entry) = kEntryIsDevelopRow;
                *reinterpret_cast<std::uint16_t*>(entry + kEntryRow) = parts[i].row;
                callback[kRowUsable + i] = 1;
            }
            *reinterpret_cast<std::uint32_t*>(callback + kListCount) = count;
            auto& cursor = *reinterpret_cast<std::uint32_t*>(callback + kListCursor);
            auto& top = *reinterpret_cast<std::uint32_t*>(callback + kListTop);
            const bool placesCursor = callback[kKeepsCursor] == 0 && callback[kLayout] == kLayoutWithCursor;
            if (placesCursor || cursor >= count)
                cursor = picked < count ? picked : 0;
            if (count < kEntriesOnScreen)
                top = 0;
            else
            {
                if (placesCursor)
                    top = cursor ? cursor - 1 : 0;
                top = (std::min)(top, count - kEntriesOnScreen);
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    std::uint8_t* WalkerGearVars()
    {
        std::uint8_t* vars = g_GetScriptVars ? g_GetScriptVars() : nullptr;
        return vars ? vars + kVarsWalkerGear : nullptr;
    }

    bool CopyWalkerGearSEH(std::uint8_t* to, const std::uint8_t* from)
    {
        __try
        {
            std::memcpy(to, from, kWalkerGearBytes);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    std::uint8_t PartTypeSEH(const std::uint8_t* callback, std::uint8_t walkerSlot)
    {
        __try
        {
            void* parts = *reinterpret_cast<void* const*>(callback + kPartsSetting);
            return parts ? reinterpret_cast<PartType_t>((*static_cast<void***>(parts))[kPartTypeSlot])(parts, walkerSlot) : 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    void ListBattleGearParts(std::uint8_t* callback, void* source)
    {
        const std::uint32_t mode = ListModeSEH(callback);
        g_ListedMode = mode;
        g_ListedCount = 0;
        if (mode != kMainWeaponList && mode != kHeadList && mode != kSubWeaponList)
            return;
        if (mode == kSubWeaponList && !g_Decide.target)   // without the decide hook the page could not tell the roof guns apart
            return;
        const std::uint32_t count = mode == kMainWeaponList ? ListMainWeapons(source, g_Listed, g_Decide.target != nullptr)
                                  : mode == kSubWeaponList  ? ListRoofGuns(source, g_Listed)
                                                            : ListHeads(source, g_Listed);
        if (count == 0)
            return;
        const std::uint8_t current = mode == kMainWeaponList ? g_PickedMain.load(std::memory_order_relaxed)
                                   : mode == kSubWeaponList  ? g_PickedRoof.load(std::memory_order_relaxed)
                                                             : PartTypeSEH(callback, kWalkerSlotHead);
        std::uint32_t picked = 0;
        for (std::uint32_t i = 0; i < count; ++i)
            if (g_Listed[i].value == current)
                picked = i;
        g_ListedCount = count;
        WriteListSEH(callback, g_Listed, count, picked);
#ifdef _DEBUG
        LogDebug("BattleGear customize page: %s list filled with %u entries, entry %u is the current pick",   // TEMP probe
                 mode == kMainWeaponList ? "main weapon" : mode == kSubWeaponList ? "roof gun" : "head", count, picked);
#endif
    }

    // the BATTLE GEAR row sends the D-Walker row's trigger, so the game opens its walker customize page as for the D-Walker (a trigger
    // no window takes leaves the menu waiting for good)
    void GiveBattleGearItemTheWalkerTriggerSEH(std::uint8_t* table)
    {
        __try
        {
            std::uint8_t* ownRow = nullptr;
            bool walkerRow = false;
            for (int i = 0; i < kMenuItemCount; ++i)
            {
                std::uint8_t* item = table + kMenuItems + static_cast<std::size_t>(i) * kMenuItemSize;
                const std::uint32_t name = *reinterpret_cast<const std::uint32_t*>(item + kMenuItemName);
                if (name == kBattleGearItem)
                    ownRow = item;
                else if (name == kWalkerItem)
                    walkerRow = true;
            }
            if (ownRow && walkerRow)
                *reinterpret_cast<std::uint64_t*>(ownRow + kMenuItemTrigger) = kWalkerTrigger;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    void __fastcall hkBuildMenuItems(std::uint8_t* table)
    {
        reinterpret_cast<BuildMenuItems_t>(g_BuildMenu.orig)(table);
        GiveBattleGearItemTheWalkerTriggerSEH(table);
    }

    std::uint32_t ItemNameSEH(const std::uint8_t* table, std::uint8_t item)
    {
        __try
        {
            return item < kMenuItemCount
                ? *reinterpret_cast<const std::uint32_t*>(table + kMenuItems + static_cast<std::size_t>(item) * kMenuItemSize + kMenuItemName)
                : 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    // the menu sends the walker page's trigger without data, so where the page opens both rows look the same; the row is known here,
    // where the menu notes the decided item right after sending its trigger
    void __fastcall hkItemDecided(std::uint8_t* table, std::uint8_t item)
    {
        reinterpret_cast<ItemDecided_t>(g_ItemDecided.orig)(table, item);
        const bool ownRow = table && ItemNameSEH(table, item) == kBattleGearItem;
        g_Requested.store(ownRow, std::memory_order_relaxed);
#ifdef _DEBUG
        if (ownRow)
            LogDebug("BattleGear customize page: the BATTLE GEAR row was decided, the walker page opens as the Battle Gear's");   // TEMP probe
#endif
    }

    std::uint32_t SelectorModeSEH(const std::uint8_t* selector)
    {
        __try
        {
            return *reinterpret_cast<const std::uint32_t*>(selector + kSelectorMode);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    // the page shows the Battle Gear's picks: the D-Walker's own are kept aside, its grade stays where the type is the same
    void OpenBattleGearPage()
    {
        std::uint8_t* gear = WalkerGearVars();
        if (!gear || !CopyWalkerGearSEH(g_WalkerGear, gear))
            return;
        std::uint8_t shown[kWalkerGearBytes];
        std::memcpy(shown, g_WalkerGear, sizeof(shown));
        const battleGearArmory::MainWeapon fitted = battleGearArmory::PagePicked();
        const std::uint8_t head = battleGearWeapons::PageHead() ? battleGearWeapons::PageHead() : battleGearWeapons::kSupportHead;
        if (shown[kHeadType] != head)
        {
            shown[kHeadType] = head;
            shown[kHeadGrade] = 1;
        }
        if (shown[kMainType] != fitted.fireType)
        {
            shown[kMainType] = fitted.fireType;
            shown[kMainGrade] = 1;
        }
        shown[kSubType] = battleGearArmory::kRoofGunType;
        shown[kSubGrade] = 1;
        if (!CopyWalkerGearSEH(gear, shown))
            return;
        g_PickedMain.store(fitted.value, std::memory_order_relaxed);
        g_PickedRoof.store(battleGearArmory::RoofGunPagePick(), std::memory_order_relaxed);
        g_PageActive = 1;
    }

    void CloseBattleGearPage(bool keep)
    {
        g_Requested.store(false, std::memory_order_relaxed);
        if (!g_PageActive)
            return;
        g_PageActive = 0;
        g_ListedCount = 0;
        RestorePageTexts();
        battleGearLoadoutSlots::ClearShown();
        std::uint8_t* gear = WalkerGearVars();
        std::uint8_t picked[kWalkerGearBytes] = {};
        if (!gear || !CopyWalkerGearSEH(picked, gear))
            return;
        battleGearArmory::MainWeapon fitted;
        if (!battleGearArmory::TryGetByValue(g_Decide.target ? g_PickedMain.load(std::memory_order_relaxed) : picked[kMainType], &fitted))
            fitted = battleGearArmory::PagePicked();
        if (keep && !g_SaveSlot.target)   // without the save hook the page's end is the only place left to keep its picks
        {
            battleGearArmory::Loadout loadout = battleGearArmory::LoadoutSlot(battleGearArmory::CurrentLoadoutSlot());
            loadout.mainWeapon = fitted.value;
            if (g_Decide.target)
                loadout.roofGun = g_PickedRoof.load(std::memory_order_relaxed);
            battleGearArmory::SetLoadoutSlot(battleGearArmory::CurrentLoadoutSlot(), loadout);
        }
        CopyWalkerGearSEH(gear, g_WalkerGear);
#ifdef _DEBUG
        LogDebug("BattleGear customize page %s: page picks main weapon %s, head %u; the D-Walker's own %u/%u restored",
                 keep ? "done" : "aborted", fitted.key.c_str(), picked[kHeadType], g_WalkerGear[kMainType], g_WalkerGear[kHeadType]);
#endif
    }

    void __fastcall hkOpen(std::uint8_t* selector)
    {
        const std::uint32_t mode = SelectorModeSEH(selector);
        if (mode == kWalkerPage && g_Requested.load(std::memory_order_relaxed) && !g_PageActive)
            OpenBattleGearPage();
        else if (mode != kWalkerPage)
            g_Requested.store(false, std::memory_order_relaxed);
        reinterpret_cast<SelectorStep_t>(g_Open.orig)(selector);
    }

    void __fastcall hkEnd(std::uint8_t* selector)
    {
        reinterpret_cast<SelectorStep_t>(g_End.orig)(selector);
        CloseBattleGearPage(true);
    }

    void __fastcall hkAbort(std::uint8_t* selector)
    {
        reinterpret_cast<SelectorStep_t>(g_Abort.orig)(selector);
        CloseBattleGearPage(false);
    }

    void __fastcall hkDecidePart(std::uint8_t* callback, std::uint32_t index)   // the game only keeps the D-Walker type the gun fires as
    {
        if (g_PageActive && callback && index < g_ListedCount && g_ListedMode == ListModeSEH(callback))
        {
            if (g_ListedMode == kMainWeaponList)
                g_PickedMain.store(g_Listed[index].value, std::memory_order_relaxed);
            else if (g_ListedMode == kSubWeaponList)
                g_PickedRoof.store(g_Listed[index].value, std::memory_order_relaxed);
        }
        reinterpret_cast<DecidePart_t>(g_Decide.orig)(callback, index);
    }

    std::uint8_t MainLevel(const battleGearArmory::MainWeapon& weapon)   // a built-in gun's borrowed row has level 1
    {
        return weapon.BuiltIn() ? 1 : weapon.value;
    }

    std::uint8_t RoofLevel(std::uint8_t roofGunValue)   // the walker ZE'EV's row has level 1
    {
        return static_cast<std::uint8_t>(roofGunValue + 1);
    }

    struct SlotBytes   // types and grades of the walker slots a Battle Gear loadout fills
    {
        std::uint8_t types[kSlotOwnParts];
        std::uint8_t grades[kSlotOwnParts];
    };

    bool SwapSlotBytesSEH(std::uint8_t slot, const SlotBytes& in, SlotBytes& was)
    {
        std::uint8_t* vars = g_GetScriptVars ? g_GetScriptVars() : nullptr;
        if (!vars)
            return false;
        __try
        {
            std::uint8_t* types = vars + kVarsSlotTypes + slot * kSlotParts + kWalkerSlotMain;
            std::uint8_t* grades = vars + kVarsSlotGrades + slot * kSlotParts + kWalkerSlotMain;
            for (int i = 0; i < kSlotOwnParts; ++i)
            {
                was.types[i] = types[i];
                was.grades[i] = grades[i];
                types[i] = in.types[i];
                grades[i] = in.grades[i];
            }
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    // the game loads the page's working parts from the walker loadout slot picked on the screen before the page (at the page's
    // start and when changes are discarded). The Battle Gear's page gets the Battle Gear's slot of that number through the D-Walker
    // slot's bytes, which hold the D-Walker's again right after
    void __fastcall hkLoadSlot(std::uint8_t* callback, std::uint8_t slot)
    {
        if (!g_PageActive || !callback || slot >= battleGearArmory::kLoadoutSlots)
        {
            reinterpret_cast<SlotParts_t>(g_LoadSlot.orig)(callback, slot);
            return;
        }
        const battleGearArmory::Loadout loadout = battleGearArmory::LoadoutSlot(slot);
        battleGearArmory::MainWeapon main;
        if (!loadout.mainWeapon || !battleGearArmory::TryGetByValue(loadout.mainWeapon, &main))
            battleGearArmory::TryGetByValue(battleGearArmory::kRailGun, &main);
        const SlotBytes own = { { main.fireType, battleGearArmory::kRoofGunType,
                                  loadout.head ? loadout.head : battleGearWeapons::kSupportHead },
                                { MainLevel(main), RoofLevel(loadout.roofGun), 1 } };
        g_PickedMain.store(main.value, std::memory_order_relaxed);   // the game's load asks for the rows of what it loaded
        g_PickedRoof.store(loadout.roofGun, std::memory_order_relaxed);
        SlotBytes walker{};
        const bool swapped = SwapSlotBytesSEH(slot, own, walker);
        reinterpret_cast<SlotParts_t>(g_LoadSlot.orig)(callback, slot);
        SlotBytes shown{};
        if (swapped)
            SwapSlotBytesSEH(slot, walker, shown);
#ifdef _DEBUG
        LogDebug("BattleGear customize page: working parts loaded from the Battle Gear's slot %u (main weapon %s, roof gun %u, head %u)",
                 static_cast<unsigned>(slot), main.key.c_str(), loadout.roofGun, own.types[2]);   // TEMP probe
#endif
    }

    // the page's save writes its working parts into that walker slot; the Battle Gear's page keeps its picks in the Battle Gear's
    // slot of the same number and leaves the D-Walker's alone
    void __fastcall hkSaveSlot(std::uint8_t* callback, std::uint8_t slot)
    {
        if (!g_PageActive || !callback)
        {
            reinterpret_cast<SlotParts_t>(g_SaveSlot.orig)(callback, slot);
            return;
        }
        if (slot >= battleGearArmory::kLoadoutSlots)
            return;
        battleGearArmory::Loadout loadout;
        loadout.mainWeapon = g_PickedMain.load(std::memory_order_relaxed);
        loadout.roofGun = g_PickedRoof.load(std::memory_order_relaxed);
        loadout.head = PartTypeSEH(callback, kWalkerSlotHead);
        battleGearArmory::SetLoadoutSlot(slot, loadout);
#ifdef _DEBUG
        LogDebug("BattleGear customize page: saved to the Battle Gear's slot %u (main weapon value %u, roof gun %u, head %u)",
                 static_cast<unsigned>(slot), loadout.mainWeapon, loadout.roofGun, loadout.head);   // TEMP probe
#endif
    }

    void GrayWalkerRowsSEH(std::uint8_t* callback)
    {
        __try
        {
            if (*reinterpret_cast<const std::uint32_t*>(callback + kListMode) != kWalkerPage)
                return;
            auto* const* rows = *reinterpret_cast<std::uint8_t* const* const*>(callback + kRowRecords);
            for (int i = 0; i < kWalkerRows; ++i)
            {
                if (i == kMainWeaponRow || i == kHeadRow || i == kConfirmRow || (i == kSubWeaponRow && g_Decide.target))
                    continue;
                callback[kRowUsable + i] = 0;
                if (rows && rows[i])
                    rows[i][kRowGrayed] = 1;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    bool HookOne(Hook& h, std::uintptr_t address, void* detour, const std::uint8_t* entry, std::size_t entrySize, const char* consequence)
    {
        void* target = address ? ResolveGameAddress(address) : nullptr;
        if (!target)
        {
            LogError("BattleGear: %s address missing on this build, %s", h.what, consequence);
            return false;
        }
        const HookEntry state = CheckHookEntry(target, entry, entrySize);
        if (state == HookEntry::Unknown)
        {
            char desc[160] = {};
            DescribeFunctionEntry(target, desc, sizeof(desc));
            LogError("BattleGear: %s differs on this build (%s), %s", h.what, desc, consequence);
            return false;
        }
        if (state == HookEntry::ForeignHook)
            LogHookedOnTop(h.what, target);
        if (!CreateAndEnableHook(target, detour, &h.orig))
        {
            LogError("BattleGear: %s hook failed, %s", h.what, consequence);
            return false;
        }
        h.target = target;
        return true;
    }

    void UnhookOne(Hook& h)
    {
        if (!h.target)
            return;
        DisableAndRemoveHook(h.target);
        h.target = nullptr;
        h.orig = nullptr;
    }

    bool ResolveGetScriptVars()
    {
        const auto* at = static_cast<const std::uint8_t*>(gAddr.Tpp_GetScriptVars ? ResolveGameAddress(gAddr.Tpp_GetScriptVars) : nullptr);
        if (!at || std::memcmp(at, kGetScriptVarsHead, sizeof(kGetScriptVarsHead)) != 0 || at[kGetScriptVarsRet] != 0xC3)
        {
            LogError("BattleGear: script vars getter missing or changed on this build, Customize > BATTLE GEAR stays closed");
            return false;
        }
        g_GetScriptVars = reinterpret_cast<GetScriptVars_t>(const_cast<std::uint8_t*>(at));
        return true;
    }

    bool InstallMainWeaponFilter()   // on the Battle Gear page the main weapon list keeps the missile pod and the stun gun
    {
        const std::uintptr_t siteAbs = gAddr.CustomizeSelectorCallbackImpl_SetupItemList_MainWeaponFilter;
        const auto base = reinterpret_cast<std::uintptr_t>(siteAbs ? ResolveGameAddress(siteAbs) : nullptr);
        if (!base)
        {
            LogError("BattleGear: customize main weapon list address missing on this build, Customize > BATTLE GEAR stays closed");
            return false;
        }
        CaveCode c;
        c.Emit({ 0x48, 0xB8 });                                              // mov rax, &page active
        c.Imm64(reinterpret_cast<std::uint64_t>(&g_PageActive));
        c.Emit({ 0x80, 0x38, 0x00 });                                        // cmp byte [rax], 0
        const std::size_t walkerPage = c.Jcc8(0x74);                         // je
        c.Emit({ 0x80, 0xF9, kMissileGroup });                               // cmp cl, missile pod
        const std::size_t missile = c.Jcc8(0x74);                            // je
        c.Emit({ 0x80, 0xF9, kStunGunGroup });                               // cmp cl, stun gun
        const std::size_t stunGun = c.Jcc8(0x74);                            // je
        c.JmpAbs(base + kFilterSkip);
        c.Bind8(walkerPage);
        c.bytes.insert(c.bytes.end(), kMainWeaponFilterSite, kMainWeaponFilterSite + 6);   // the original sub / cmp
        const std::size_t outside = c.Jcc8(0x77);                            // ja
        c.Bind8(missile);
        c.Bind8(stunGun);
        c.JmpAbs(base + sizeof(kMainWeaponFilterSite));
        c.Bind8(outside);
        c.JmpAbs(base + kFilterSkip);
        return InstallBuddyCodeCave(g_MainWeaponFilter, siteAbs, kMainWeaponFilterSite, sizeof(kMainWeaponFilterSite), c);
    }

    // movabs reg, a D-Walker text id: the thunk loads the Battle Gear's instead while the flag byte is set
    bool InstallTextSwitch(BuddyCodeCave& cave, std::uintptr_t siteAbs, std::uint8_t rex, std::uint8_t movOpcode, std::uint64_t walkerText,
                           std::uint64_t battleGearText, const std::uint8_t* flag)
    {
        const auto base = reinterpret_cast<std::uintptr_t>(siteAbs ? ResolveGameAddress(siteAbs) : nullptr);
        if (!base)
        {
            LogError("BattleGear: %s address missing on this build, it keeps the D-Walker's text", cave.name);
            return false;
        }
        std::uint8_t site[kMovImm64Size] = { rex, movOpcode };
        std::memcpy(site + 2, &walkerText, sizeof(walkerText));
        const auto modRm = static_cast<std::uint8_t>(0x38 | (movOpcode & 7));
        CaveCode c;
        c.Emit({ rex, movOpcode });                         // mov reg, &flag
        c.Imm64(reinterpret_cast<std::uint64_t>(flag));
        if (rex & kRexB)
            c.Emit({ 0x41, 0x80, modRm, 0x00 });            // cmp byte [reg], 0
        else
            c.Emit({ 0x80, modRm, 0x00 });
        c.Emit({ rex, movOpcode });                         // mov reg, the D-Walker's text (the flags stay)
        c.Imm64(walkerText);
        const std::size_t walker = c.Jcc8(0x74);            // je
        c.Emit({ rex, movOpcode });                         // mov reg, the Battle Gear's
        c.Imm64(battleGearText);
        c.Bind8(walker);
        c.JmpAbs(base + kMovImm64Size);
        return InstallBuddyCodeCave(cave, siteAbs, site, sizeof(site), c);
    }

    bool InstallDetailsLayout()   // a Battle Gear param (buddy type 5) gets the walker's details instead of none
    {
        const std::uintptr_t siteAbs = gAddr.EquipDetailsCallbackImpl_SetupDetails_WalkerCheck;
        const auto base = reinterpret_cast<std::uintptr_t>(siteAbs ? ResolveGameAddress(siteAbs) : nullptr);
        if (!base)
        {
            LogError("BattleGear: %s address missing on this build, the Battle Gear's details stay empty", g_DetailsLayout.name);
            return false;
        }
        CaveCode c;
        c.Emit({ 0x83, 0x78, 0x14, 0x04 });                                       // cmp dword [rax+0x14], 4
        const std::size_t walker = c.Jcc8(0x74);                                  // je
        c.Emit({ 0x83, 0x78, 0x14, battleGear::kBuddyType });                     // cmp dword [rax+0x14], 5
        const std::size_t ownType = c.Jcc8(0x74);                                 // je
        c.JmpAbs(base + sizeof(kDetailsWalkerCheck) + kDetailsWalkerCheck[5]);    // the horse's, the dog's and Quiet's details
        c.Bind8(walker);
        c.Bind8(ownType);
        c.JmpAbs(base + sizeof(kDetailsWalkerCheck));
        return InstallBuddyCodeCave(g_DetailsLayout, siteAbs, kDetailsWalkerCheck, sizeof(kDetailsWalkerCheck), c);
    }

    bool DetailsShowBattleGearSEH(const std::uint8_t* details)
    {
        __try
        {
            const std::uint8_t mode = details[kDetailsMode];
            const auto* param = *reinterpret_cast<const std::uint8_t* const*>(details + kDetailsParam);
            return (mode == kDetailsListMode || mode == kDetailsSlotMode) && param
                && *reinterpret_cast<const std::uint32_t*>(param + kParamBuddyType) == battleGear::kBuddyType;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void* DetailsDevelopSEH(const std::uint8_t* details)
    {
        __try
        {
            return *reinterpret_cast<void* const*>(details + kDetailsDevelop);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    // a Battle Gear param in the details window (Sortie Prep, the equipment lists, the slot screen): the walker's layout through the
    // layout cave, the Battle Gear's name through the title cave, its part texts for the length of the call
    void __fastcall hkSetupDetails(std::uint8_t* details)
    {
        if (!details || !DetailsShowBattleGearSEH(details))
        {
            reinterpret_cast<DetailsStep_t>(g_SetupDetails.orig)(details);
            return;
        }
        TextSwap swaps[kMostTextSwaps];
        int swapCount = 0;
        void* source = g_TextsSwapped ? nullptr : DetailsDevelopSEH(details);   // an open Battle Gear page has switched them already
        if (std::uint8_t* records = source ? DevelopRecordsSEH(source) : nullptr)
        {
            PageTexts texts[kTextGroups];
            const int count = BattleGearPageTexts(texts);
            SwapTextsSEH(records, texts, count, swaps, swapCount);
        }
        g_DetailsBattleGear = 1;
        reinterpret_cast<DetailsStep_t>(g_SetupDetails.orig)(details);
        g_DetailsBattleGear = 0;
        RestoreTextsSEH(swaps, swapCount);
    }

    bool OpenGate(NativeCodePatch& gate, std::uintptr_t address, const std::uint8_t* expected, const std::uint8_t* replacement,
                  std::size_t size, const char* what)
    {
        void* site = address ? ResolveGameAddress(address) : nullptr;
        if (!site)
        {
            LogError("BattleGear: iDroid menu %s address missing on this build, Customize > BATTLE GEAR stays hidden", what);
            return false;
        }
        gate = NativeCodePatch{};
        gate.target = site;
        gate.size = static_cast<std::uint8_t>(size);
        std::memcpy(gate.expected, expected, size);
        std::memcpy(gate.replacement, replacement, size);
        if (InstallNativeCodePatches(&gate, 1))
            return true;
        LogError("BattleGear: iDroid menu %s code differs on this build (another mod patched it?), Customize > BATTLE GEAR stays hidden", what);
        return false;
    }

    void ShowCallRows()
    {
        void* site = gAddr.MotherBaseDeviceTopMenuTableImpl_IsCutItem_Ids
                       ? static_cast<std::uint8_t*>(ResolveGameAddress(gAddr.MotherBaseDeviceTopMenuTableImpl_IsCutItem_Ids)) + kCutCallRowsAt
                       : nullptr;
        if (!site)
        {
            LogError("BattleGear: iDroid item list address missing on this build, BUDDY SUPPORT keeps no Battle Gear call");
            return;
        }
        g_CutCallGate = NativeCodePatch{};
        g_CutCallGate.target = site;
        g_CutCallGate.size = static_cast<std::uint8_t>(sizeof(kCutCallRows));
        std::memcpy(g_CutCallGate.expected, kCutCallRows, sizeof(kCutCallRows));
        std::memcpy(g_CutCallGate.replacement, kCutCallRowsBattleGear, sizeof(kCutCallRowsBattleGear));
        if (!InstallNativeCodePatches(&g_CutCallGate, 1))
            LogError("BattleGear: iDroid item list differs on this build (another mod patched it?), BUDDY SUPPORT keeps no Battle Gear call");
    }
}

namespace battleGearCustomizePage
{
    void AfterSetupItemList(std::uintptr_t callback)
    {
        if (!g_PageActive || !callback)
            return;
        auto* page = reinterpret_cast<std::uint8_t*>(callback);
        void* source = DevelopSourceSEH(page);
        if (source)
            SwapPageTexts(source);
        GrayWalkerRowsSEH(page);
        if (source)
            ListBattleGearParts(page, source);
    }

    bool Active()
    {
        return g_PageActive != 0;
    }

    bool RegisteredPartRow(std::uint16_t developId, std::uint8_t* slot, std::uint8_t* type, std::uint8_t* level)
    {
        if (!g_PageActive)
            return false;
        battleGearArmory::MainWeapon weapon;
        if (battleGearArmory::TryGetByDevelopId(developId, &weapon))
        {
            *slot = kWalkerSlotMain;
            *type = weapon.fireType;
            *level = MainLevel(weapon);
            return true;
        }
        battleGearArmory::RoofGun gun;
        if (!battleGearArmory::TryGetRoofGunByDevelopId(developId, &gun))
            return false;
        *slot = kWalkerSlotSub;
        *type = battleGearArmory::kRoofGunType;
        *level = RoofLevel(gun.value);
        return true;
    }

    battleGearArmory::Loadout PickedGuns()
    {
        battleGearArmory::Loadout loadout;
        loadout.mainWeapon = g_PickedMain.load(std::memory_order_relaxed);
        loadout.roofGun = g_PickedRoof.load(std::memory_order_relaxed);
        return loadout;
    }

    std::uint16_t PickedRegisteredRow(std::uint8_t slot, std::uint8_t type)
    {
        if (!g_PageActive)
            return 0;
        if (slot == kWalkerSlotSub)
        {
            battleGearArmory::RoofGun gun;
            return type == battleGearArmory::kRoofGunType
                    && battleGearArmory::TryGetRoofGunByValue(g_PickedRoof.load(std::memory_order_relaxed), &gun) && !gun.BuiltIn()
                ? gun.developId : 0;
        }
        battleGearArmory::MainWeapon weapon;
        if (slot != kWalkerSlotMain || !battleGearArmory::TryGetByValue(g_PickedMain.load(std::memory_order_relaxed), &weapon)
            || weapon.BuiltIn() || weapon.fireType != type)
            return 0;
        return weapon.developId;
    }
}

bool Install_BattleGearCustomizePage_Patches()
{
    constexpr const char* kStaysClosed = "Customize > BATTLE GEAR stays closed";
    battleGearArmory::LoadLoadoutSlots();
    const bool ok = ResolveGetScriptVars() && InstallMainWeaponFilter()
                 && HookOne(g_End, gAddr.CustomizeSelectorSystemImpl_EndCustomizeSelector, reinterpret_cast<void*>(&hkEnd), kEndEntry,
                            sizeof(kEndEntry), kStaysClosed)
                 && HookOne(g_Abort, gAddr.CustomizeSelectorSystemImpl_AbortCustomizeSelector, reinterpret_cast<void*>(&hkAbort), kEndEntry,
                            sizeof(kEndEntry), kStaysClosed)
                 && HookOne(g_Open, gAddr.CustomizeSelectorSystemImpl_OpenCustomizeSelector, reinterpret_cast<void*>(&hkOpen), kOpenEntry,
                            sizeof(kOpenEntry), kStaysClosed)
                 && HookOne(g_ItemDecided, gAddr.MotherBaseDeviceTopMenuTableImpl_OnItemDecided, reinterpret_cast<void*>(&hkItemDecided),
                            kItemDecidedEntry, sizeof(kItemDecidedEntry), kStaysClosed)
                 && HookOne(g_BuildMenu, gAddr.MotherBaseDeviceTopMenuTableImpl_BuildMenuItems, reinterpret_cast<void*>(&hkBuildMenuItems),
                            kBuildMenuItemsEntry, sizeof(kBuildMenuItemsEntry), kStaysClosed)
                 && OpenGate(g_VisibleGate, gAddr.MotherBaseDeviceTopMenuTableImpl_IsVisibleItem_QuietBattleGate, kQuietGateSite,
                             kQuietGateOpen, sizeof(kQuietGateSite), "visibility")
                 && OpenGate(g_ActiveGate, gAddr.MotherBaseDeviceTopMenuTableImpl_IsActiveItem_QuietBattleGate, kQuietGateSite,
                             kQuietGateOpen, sizeof(kQuietGateSite), "item state")
                 && OpenGate(g_CutItemGate, gAddr.MotherBaseDeviceTopMenuTableImpl_IsCutItem_Ids, kCutItemIds, kCutItemIdsBattleGear,
                             sizeof(kCutItemIds), "item list");
    if (!ok)
    {
        Uninstall_BattleGearCustomizePage_Patches();
        return false;
    }
    ShowCallRows();
    HookOne(g_Decide, gAddr.CustomizeSelectorCallbackImpl_DecideActWindowPartBuddyDWalker, reinterpret_cast<void*>(&hkDecidePart),
            kDecidePartEntry, sizeof(kDecidePartEntry), "the page lists only the two built-in Battle Gear guns and no roof guns");
    constexpr const char* kSharesSlot = "the Battle Gear's page starts from and saves into the D-Walker's loadout slot";
    if (HookOne(g_LoadSlot, gAddr.CustomizeSelectorCallbackImpl_RestoreCustomizedDWalkerPartsFromVars, reinterpret_cast<void*>(&hkLoadSlot),
                kSlotPartsEntry, sizeof(kSlotPartsEntry), kSharesSlot)
        && !HookOne(g_SaveSlot, gAddr.CustomizeSelectorCallbackImpl_SetCustomizedDWalkerPartsToVars, reinterpret_cast<void*>(&hkSaveSlot),
                    kSlotPartsEntry, sizeof(kSlotPartsEntry), kSharesSlot))
        UnhookOne(g_LoadSlot);
    InstallTextSwitch(g_PageTitle, gAddr.CustomizeSelectorCallbackImpl_UpdateMenuTitleText_WalkerTitle, 0x49, 0xB9, kWalkerTitle,
                      kBattleGearTitle, &g_PageActive);
    InstallTextSwitch(g_SlotHelp, gAddr.CustomizeSelectorCallbackImpl_UpdateSettingMenuHelpText_WalkerHelp, 0x48, 0xBA, kWalkerSlotHelp,
                      kBattleGearHelp, &g_PageActive);
    InstallTextSwitch(g_SlotListTitle, gAddr.CustomizeSlotSelectorCallbackImpl_SetupListElementWalkerGear_Title, 0x48, 0xBA, kWalkerTitle,
                      kBattleGearTitle, &g_PageActive);
    if (InstallDetailsLayout()
        && HookOne(g_SetupDetails, gAddr.EquipDetailsCallbackImpl_SetupDetails, reinterpret_cast<void*>(&hkSetupDetails), kSetupDetailsEntry,
                   sizeof(kSetupDetailsEntry), "the Battle Gear's details show the D-Walker's part names"))
        InstallTextSwitch(g_DetailsTitle, gAddr.EquipDetailsCallbackImpl_SetupWalkerDetails_Title, 0x48, 0xBA, kWalkerName, kBattleGearName,
                          &g_DetailsBattleGear);
    return true;
}

void Uninstall_BattleGearCustomizePage_Patches()
{
    UninstallBuddyCodeCave(g_DetailsTitle);
    UnhookOne(g_SetupDetails);
    UninstallBuddyCodeCave(g_DetailsLayout);
    UninstallBuddyCodeCave(g_SlotListTitle);
    UninstallBuddyCodeCave(g_SlotHelp);
    UninstallBuddyCodeCave(g_PageTitle);
    UnhookOne(g_SaveSlot);
    UnhookOne(g_LoadSlot);
    UnhookOne(g_Decide);
    if (g_CutCallGate.installed)
        RemoveNativeCodePatches(&g_CutCallGate, 1);
    if (g_CutItemGate.installed)
        RemoveNativeCodePatches(&g_CutItemGate, 1);
    if (g_ActiveGate.installed)
        RemoveNativeCodePatches(&g_ActiveGate, 1);
    if (g_VisibleGate.installed)
        RemoveNativeCodePatches(&g_VisibleGate, 1);
    UnhookOne(g_BuildMenu);
    UnhookOne(g_ItemDecided);
    UnhookOne(g_Open);
    UnhookOne(g_Abort);
    UnhookOne(g_End);
    CloseBattleGearPage(false);
    UninstallBuddyCodeCave(g_MainWeaponFilter);
    g_GetScriptVars = nullptr;
}
