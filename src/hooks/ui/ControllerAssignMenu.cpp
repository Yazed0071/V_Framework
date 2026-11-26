#include <pch.h>
#include "ControllerAssignMenu.h"

#include <Windows.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "AddressSet.h"
#include "FoxHashes.h"
#include "HookUtils.h"
#include "TextIcons.h"
#include "UiPadLock.h"
#include "log.h"
#include "../player/PadAssign.h"

namespace
{
    struct Row    // a row of the game's Key Assignments screen
    {
        std::uint64_t label;      // lang id
        const char*   iconName;   // drawn as <I=G=name>; the game's own rows hold the action's name here
    };

    struct Page   // a page of that screen
    {
        std::uint64_t title;      // lang id
        Row*          rows;
        std::uint32_t rowCount;
        std::uint32_t unused;
    };
    static_assert(sizeof(Row) == 0x10 && sizeof(Page) == 0x18, "the game reads these as its own rows and pages");

    using GetItemText_t        = std::uint64_t* (__fastcall*)(std::uint64_t* out, std::uint32_t item);
    using CreateDecideEvent_t  = void* (__fastcall*)(void* itemTable, std::uint32_t item);
    using CopyPageChildren_t   = void (__fastcall*)(void* itemTable, std::uint32_t page, std::uint32_t limit,
                                                    std::uint8_t* out, std::uint32_t* count);
    using ScreenCall_t         = void (__fastcall*)(std::uint8_t* screen);
    using ScreenAnswer_t       = bool (__fastcall*)(std::uint8_t* screen);
    using GetCategory_t        = Page* (__fastcall*)(std::uint32_t page);
    using DefaultNodeExec_t    = void (__fastcall*)(void* node, std::uint8_t* screen);
    using SetListTextAndIcon_t = void (__fastcall*)(std::uint8_t* screen, std::uint32_t firstRow);
    using LangIdToKey_t        = std::uint64_t* (__fastcall*)(std::uint64_t* out, const char* langId);
    using GetLangText_t        = const char* (__fastcall*)(std::uint64_t key);

    constexpr std::uint8_t kGetItemTextEntry[] =   // the label and the help getter start alike
    {
        0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0x83, 0xFA, 0x2A, 0x72, 0x16, 0x48, 0xB8,
        0x98, 0x9F, 0x16, 0xBF, 0xA0, 0xB8, 0x00, 0x00,
    };
    constexpr std::uint8_t kCreateDecideEventEntry[] =
    {
        0x8B, 0xC2, 0x48, 0x8D, 0x0C, 0x40, 0x48, 0x8D, 0x05, 0x00, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x0C,
        0xC8, 0x48, 0x85, 0xC9, 0x75, 0x03, 0x33, 0xC0,
    };
    constexpr std::uint8_t kCreateDecideEventMask[] =
    {
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    };
    constexpr std::uint8_t kCopyPageChildrenEntry[] =
    {
        0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x8B, 0xC2, 0x48, 0x8D, 0x14, 0x45, 0x01, 0x00, 0x00, 0x00,
        0x48, 0x03, 0xD0, 0x33, 0xDB, 0x48, 0x8D, 0x14,
    };
    constexpr std::uint8_t kSetupSelectorsEntry[] =
    {
        0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x56, 0x57, 0x41, 0x54, 0x41, 0x56,
        0x41, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x4C, 0x8B,
    };
    constexpr std::uint8_t kGetCategoryEntry[] =
    {
        0x8B, 0xC1, 0x48, 0x8D, 0x0C, 0x40, 0x48, 0x8D, 0x05, 0x00, 0x00, 0x00, 0x00, 0x48, 0x8D, 0x04,
        0xC8, 0xC3,
    };
    constexpr std::uint8_t kGetCategoryMask[] =
    {
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF,
        0xFF, 0xFF,
    };
    constexpr std::uint8_t kCaptureKeyEntry[] =
    {
        0x40, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8B, 0xEC, 0x48,
        0x83, 0xEC, 0x30, 0x48, 0xC7, 0x45, 0xF8, 0xFE,
    };
    constexpr std::uint8_t kIsKeyChangedEntry[] =
    {
        0x40, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x48, 0xC7, 0x44, 0x24, 0x20, 0xFE, 0xFF, 0xFF, 0xFF, 0x48,
        0x89, 0x5C, 0x24, 0x48, 0x48, 0x89, 0x6C, 0x24,
    };
    constexpr std::uint8_t kRestoreKeysEntry[] =
    {
        0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8D, 0x6C,
        0x24, 0xE1, 0x48, 0x81, 0xEC, 0xC8, 0x00, 0x00,
    };
    constexpr std::uint8_t kDefaultNodeExecEntry[] =
    {
        0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xDA, 0xE8, 0x00, 0x00, 0x00, 0x00, 0xE8, 0x00,
        0x00, 0x00, 0x00, 0x33, 0xD2, 0x48, 0x8B, 0xCB,
    };
    constexpr std::uint8_t kDefaultNodeExecMask[] =
    {
        0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x00,
        0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    };

    constexpr std::uint32_t kItemKeyAssignments    = 0x13;   // pause menu items
    constexpr std::uint8_t  kItemControls          = 0x15;
    constexpr std::uint32_t kItemControllerAssign  = 0x2A;   // ours: the first number past the game's own items
    constexpr std::uint32_t kPageControlsAndManual = 3;
    constexpr std::uint32_t kPageItemLimit         = 9;      // what a pause menu page can hold

    constexpr std::size_t kScreenChanged    = 0x0B2;
    constexpr std::size_t kScreenPage       = 0xAD0;   // the page on show
    constexpr std::size_t kScreenListScroll = 0x9F8;
    constexpr std::size_t kScreenListCursor = 0x9FC;
    constexpr std::size_t kScreenPageCount  = 0xAF0;   // the page selector: how many pages, then how many it shows at once
    constexpr std::size_t kScreenPagesShown = 0xAF4;

    constexpr std::uint32_t kGamePageCount = 4;
    constexpr std::uint32_t kGameRowLimit  = 32;
    constexpr std::uint32_t kMaxPages      = 4;
    constexpr std::uint32_t kMaxRows       = 12;       // what a page shows without scrolling
    constexpr ULONGLONG     kCaptureGapMs  = 500;
    constexpr ULONGLONG     kAskedLifeMs   = 20000;    // from the decide on our item to the screen's setup: its pack loads in between
    constexpr std::uint32_t kButtonBits    = 0xFFFF;

    constexpr const char* kEntryLabel = "V_pause_controller_assign";
    constexpr const char* kEntryHelp  = "V_pause_controller_assign_info";

    constexpr const char* kButtonIcon[padassign::kButtonCount] =
    {
        "V_PAD_BUTTON_00", "V_PAD_BUTTON_01", "V_PAD_BUTTON_02", "V_PAD_BUTTON_03",
        "V_PAD_BUTTON_04", "V_PAD_BUTTON_05", "V_PAD_BUTTON_06", "V_PAD_BUTTON_07",
        "V_PAD_BUTTON_08", "V_PAD_BUTTON_09", "V_PAD_BUTTON_10", "V_PAD_BUTTON_11",
        "V_PAD_BUTTON_12", "V_PAD_BUTTON_13", "V_PAD_BUTTON_14", "V_PAD_BUTTON_15",
    };
    constexpr const char* kNoButtonIcon = "V_PAD_BUTTON_NONE";   // no icon has that name: the row shows none

    struct RowSource
    {
        const char* gameRow;    // the game's keyboard row whose label the row takes
        int         action;     // padassign::Action, or -1
        int         name;       // padassign::Name, or -1
        const char* ownLabel;   // lang id, for a row the game has no label for
        int         apartFrom;  // an action: the row only shows while this Control Type keeps the two on different buttons
    };

    struct PageSource
    {
        std::uint32_t    gamePage;   // the game's keyboard page whose title the page takes
        const RowSource* rows;
        int              rowCount;
    };

    constexpr RowSource kOnFootRows[] =
    {
        { "UI_DASH",   padassign::kActionDash,   -1, nullptr, -1 },
        { "UI_STANCE", padassign::kActionStance, -1, nullptr, -1 },
        { "UI_ACTION", padassign::kActionAction, -1, nullptr, -1 },
        { "UI_EVADE",  padassign::kActionEvade,  -1, nullptr, -1 },
        { "UI_STOCK",  padassign::kActionStock,  -1, nullptr, -1 },
    };
    constexpr RowSource kOnFootRows2[] =
    {
        { "UI_RELOAD",  padassign::kActionReload,   -1, nullptr, -1 },
        { "UI_HOLD",    padassign::kActionHold,     -1, nullptr, -1 },
        { "UI_CQC",     padassign::kActionFire,     -1, nullptr, -1 },                       // the game's text reads Attack/CQC
        { "UI_CQC",     padassign::kActionCqc,      -1, nullptr, padassign::kActionFire },   // Control Types that split the two
        { "UI_SUBJECT", padassign::kActionSubject,  -1, nullptr, -1 },
        { "UI_RADIO",   padassign::kActionCall,     -1, nullptr, -1 },
        { "UI_START",   padassign::kActionMbDevice, -1, nullptr, -1 },
        { nullptr, -1, padassign::kNameLaserWhileAiming, "V_key_laser_aim", -1 },
    };
    constexpr RowSource kWeaponMenuRows[] =
    {
        { nullptr, -1, padassign::kNameLaserInWeaponMenu, "V_key_laser_menu", -1 },
    };
    constexpr PageSource kPageSources[] =
    {
        { 0, kOnFootRows,     sizeof(kOnFootRows) / sizeof(kOnFootRows[0]) },           // Player Controls (on foot)
        { 1, kOnFootRows2,    sizeof(kOnFootRows2) / sizeof(kOnFootRows2[0]) },         // Player Controls (on foot [2])
        { 3, kWeaponMenuRows, sizeof(kWeaponMenuRows) / sizeof(kWeaponMenuRows[0]) },   // Menu Controls (selecting weapons)
    };

    struct Hook
    {
        const char*         function;
        std::uintptr_t      address;
        const std::uint8_t* entry;
        const std::uint8_t* mask;      // null: every byte counts
        std::size_t         entrySize;
        void*               detour;
        void**              original;
        void*               target;
    };

    GetItemText_t        g_OrigGetLabel          = nullptr;
    GetItemText_t        g_OrigGetHelp           = nullptr;
    CreateDecideEvent_t  g_OrigCreateDecideEvent = nullptr;
    CopyPageChildren_t   g_OrigCopyPageChildren  = nullptr;
    ScreenCall_t         g_OrigSetupSelectors    = nullptr;
    GetCategory_t        g_OrigGetCategory       = nullptr;
    ScreenAnswer_t       g_OrigCaptureKey        = nullptr;
    ScreenAnswer_t       g_OrigIsKeyChanged      = nullptr;
    ScreenCall_t         g_OrigRestoreKeys       = nullptr;
    DefaultNodeExec_t    g_OrigDefaultNodeExec   = nullptr;
    SetListTextAndIcon_t g_SetListTextAndIcon    = nullptr;
    LangIdToKey_t        g_LangIdToKey           = nullptr;
    GetLangText_t        g_GetLangText           = nullptr;
    bool                 g_Installed             = false;

    std::uint64_t     g_EntryLabel = 0;
    std::uint64_t     g_EntryHelp  = 0;
    std::atomic<bool> g_EntryTextMissingTold{ false };
    std::atomic<ULONGLONG> g_ControllerAskedAt{ 0 };   // when our item was decided: tells the screen which of the two it opens
    std::atomic<bool>      g_NoPageTold{ false };

    // the screen's own thread from here on
    bool              g_ControllerMode = false;
    int               g_ControlType = 0;
    std::uint32_t     g_PageCount = 0;
    Page              g_Pages[kMaxPages];
    Row               g_Rows[kMaxPages][kMaxRows];
    controllerassign::Entry g_RowEntry[kMaxPages][kMaxRows];
    padassign::Layout g_LayoutAtOpen;
    bool              g_Waiting = false;      // a row waits for a button
    bool              g_Armed = false;        // the press that opened the row has ended
    ULONGLONG         g_LastWaitTick = 0;

    std::uint64_t LangKeySEH(const char* langId)
    {
        std::uint64_t key = 0;
        __try
        {
            g_LangIdToKey(&key, langId);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            key = 0;
        }
        return key;
    }

    bool HasTextSEH(std::uint64_t key)
    {
        __try
        {
            const char* text = key ? g_GetLangText(key) : nullptr;
            return text && text[0];
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool EntryTextIsLoaded()
    {
        if (HasTextSEH(g_EntryLabel))
            return true;
        if (!g_EntryTextMissingTold.exchange(true))
            LogWarn("ControllerAssign: no text for %s, is the V_FrameWork lang pack installed? CONTROLLER ASSIGNMENTS stays "
                    "off the pause menu", kEntryLabel);
        return false;
    }

    void AddEntrySEH(std::uint8_t* items, std::uint32_t* count)
    {
        __try
        {
            const std::uint32_t had = *count;
            if (had >= kPageItemLimit)
                return;
            std::uint32_t at = had;   // after CONTROLS, or last when the page has no CONTROLS
            for (std::uint32_t i = 0; i < had; ++i)
            {
                if (items[i] == kItemControllerAssign)
                    return;
                if (items[i] == kItemControls)
                    at = i + 1;
            }
            std::memmove(items + at + 1, items + at, had - at);
            items[at] = static_cast<std::uint8_t>(kItemControllerAssign);
            *count = had + 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    std::uint64_t GamePageTitleSEH(std::uint32_t gamePage)
    {
        __try
        {
            const Page* page = g_OrigGetCategory(gamePage);
            return page ? page->title : 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    std::uint64_t GameRowLabelSEH(const char* gameRow)
    {
        __try
        {
            for (std::uint32_t p = 0; p < kGamePageCount; ++p)
            {
                const Page* page = g_OrigGetCategory(p);
                if (!page || !page->rows)
                    continue;
                for (std::uint32_t r = 0; r < page->rowCount && r < kGameRowLimit; ++r)
                    if (page->rows[r].iconName && std::strcmp(page->rows[r].iconName, gameRow) == 0)
                        return page->rows[r].label;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
        return 0;
    }

    bool ResolveRow(const RowSource& from, int controlType, controllerassign::Entry& entry)
    {
        entry.action = from.action;
        entry.name = from.name;
        entry.label = from.ownLabel ? LangKeySEH(from.ownLabel) : GameRowLabelSEH(from.gameRow);
        const int button = controllerassign::ButtonOf(controlType, entry);
        if (!entry.label || button == padassign::kNoButton)   // a row whose feature is off, or an action this Control Type gives no button
            return false;
        return from.apartFrom < 0 || button != padassign::ButtonOfAction(controlType, from.apartFrom);
    }

    void ShowButtons()
    {
        for (std::uint32_t p = 0; p < g_PageCount; ++p)
            for (std::uint32_t r = 0; r < g_Pages[p].rowCount; ++r)
                g_Rows[p][r].iconName = controllerassign::ButtonIconName(controllerassign::ButtonOf(g_ControlType, g_RowEntry[p][r]));
    }

    bool OpenControllerMode()
    {
        g_ControlType = padassign::ControlType();
        g_PageCount = 0;
        for (const PageSource& source : kPageSources)
        {
            Page& page = g_Pages[g_PageCount];
            page.title = GamePageTitleSEH(source.gamePage);
            page.rows = g_Rows[g_PageCount];
            page.rowCount = 0;
            page.unused = 0;
            for (int i = 0; i < source.rowCount && page.rowCount < kMaxRows; ++i)
            {
                controllerassign::Entry entry;
                if (!ResolveRow(source.rows[i], g_ControlType, entry))
                    continue;
                page.rows[page.rowCount].label = entry.label;
                page.rows[page.rowCount].iconName = kNoButtonIcon;
                g_RowEntry[g_PageCount][page.rowCount] = entry;
                ++page.rowCount;
            }
            if (page.title && page.rowCount)
                ++g_PageCount;
        }
        if (!g_PageCount)
            return false;
        ShowButtons();
        padassign::Read(g_LayoutAtOpen);
        g_Waiting = false;
        return true;
    }

    void SetPageCountSEH(std::uint8_t* screen, std::uint32_t pages)
    {
        __try
        {
            *reinterpret_cast<std::uint32_t*>(screen + kScreenPageCount) = pages;
            *reinterpret_cast<std::uint32_t*>(screen + kScreenPagesShown) = pages;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    bool FocusedRowSEH(const std::uint8_t* screen, controllerassign::Entry& entry)
    {
        __try
        {
            const Page* page = *reinterpret_cast<const Page* const*>(screen + kScreenPage);
            if (page < g_Pages || page >= g_Pages + g_PageCount)
                return false;
            const std::uint32_t row = *reinterpret_cast<const std::uint32_t*>(screen + kScreenListCursor)
                                    + *reinterpret_cast<const std::uint32_t*>(screen + kScreenListScroll);
            if (row >= page->rowCount)
                return false;
            entry = g_RowEntry[page - g_Pages][row];
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void MarkChangedSEH(std::uint8_t* screen, bool changed)
    {
        __try
        {
            screen[kScreenChanged] = changed ? 1 : 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    bool TakeButton(std::uint8_t* screen)
    {
        const ULONGLONG now = GetTickCount64();
        if (!g_Waiting || now - g_LastWaitTick > kCaptureGapMs)
        {
            g_Waiting = true;
            g_Armed = false;
        }
        g_LastWaitTick = now;

        static const std::uint32_t cancel = FoxHashes::StrCode32("UI_CANCEL");
        std::uint32_t held = 0, pressed = 0;
        const bool padRead = uipad::ButtonWords(held, pressed);
        held &= kButtonBits;
        const bool keyboardLeaves = uipad::KeyboardHalf(uipad::KeyRead::Press, cancel, 0.0f, true) != 0;   // Esc: the way out without a controller
        if (padRead && !keyboardLeaves)
        {
            if (!g_Armed)
            {
                g_Armed = held == 0;
                return false;
            }
            if (!held)
                return false;
        }
        controllerassign::Entry entry;
        bool given = false;
        if (padRead && !keyboardLeaves && FocusedRowSEH(screen, entry))
            for (int button = 0; button < padassign::kButtonCount && !given; ++button)
                if (held & (1u << button))
                    given = controllerassign::GiveButton(g_ControlType, entry, button);
        g_Waiting = false;
        if (given)
        {
            ShowButtons();
            g_SetListTextAndIcon(screen, 0);
            MarkChangedSEH(screen, padassign::Differs(g_LayoutAtOpen));
        }
        return true;   // a button that stays with the game ends the wait too, with nothing changed: the screen has no other way out of it
    }

    std::uint64_t* __fastcall hk_GetLabel(std::uint64_t* out, std::uint32_t item)
    {
        if (item != kItemControllerAssign || !out)
            return g_OrigGetLabel(out, item);
        *out = g_EntryLabel;
        return out;
    }

    std::uint64_t* __fastcall hk_GetHelp(std::uint64_t* out, std::uint32_t item)
    {
        if (item != kItemControllerAssign || !out)
            return g_OrigGetHelp(out, item);
        *out = g_EntryHelp;
        return out;
    }

    void* __fastcall hk_CreateDecideEvent(void* itemTable, std::uint32_t item)
    {
        const bool ours = item == kItemControllerAssign;
        if (ours)
            g_ControllerAskedAt.store(GetTickCount64(), std::memory_order_release);
        else if (item == kItemKeyAssignments)   // other items leave it: the menu asks for some by itself between a decide and the screen
            g_ControllerAskedAt.store(0, std::memory_order_release);
        return g_OrigCreateDecideEvent(itemTable, ours ? kItemKeyAssignments : item);   // the game's table ends before our number
    }

    void __fastcall hk_CopyPageChildren(void* itemTable, std::uint32_t page, std::uint32_t limit, std::uint8_t* out,
                                        std::uint32_t* count)
    {
        g_OrigCopyPageChildren(itemTable, page, limit, out, count);
        if (page == kPageControlsAndManual && out && count && padassign::IsActive() && EntryTextIsLoaded())
            AddEntrySEH(out, count);
    }

    void __fastcall hk_SetupSelectors(std::uint8_t* screen)
    {
        const ULONGLONG askedAt = g_ControllerAskedAt.exchange(0, std::memory_order_acq_rel);
        const bool asked = askedAt != 0 && GetTickCount64() - askedAt < kAskedLifeMs;
        g_ControllerMode = asked && OpenControllerMode();
        if (asked && !g_ControllerMode && !g_NoPageTold.exchange(true))
            LogWarn("ControllerAssign: no row to show, CONTROLLER ASSIGNMENTS opened the game's keyboard screen");
        g_OrigSetupSelectors(screen);
        if (g_ControllerMode)
            SetPageCountSEH(screen, g_PageCount);
    }

    Page* __fastcall hk_GetCategory(std::uint32_t page)
    {
        if (g_ControllerMode && page < g_PageCount)
            return &g_Pages[page];
        return g_OrigGetCategory(page);
    }

    bool __fastcall hk_CaptureKey(std::uint8_t* screen)
    {
        return g_ControllerMode ? TakeButton(screen) : g_OrigCaptureKey(screen);
    }

    bool __fastcall hk_IsKeyChanged(std::uint8_t* screen)
    {
        return g_ControllerMode ? padassign::Differs(g_LayoutAtOpen) : g_OrigIsKeyChanged(screen);
    }

    void __fastcall hk_RestoreKeys(std::uint8_t* screen)
    {
        if (!g_ControllerMode)
        {
            g_OrigRestoreKeys(screen);
            return;
        }
        padassign::Write(g_LayoutAtOpen);
        ShowButtons();
        MarkChangedSEH(screen, false);
    }

    void __fastcall hk_DefaultNodeExec(void* node, std::uint8_t* screen)
    {
        if (!g_ControllerMode)
        {
            g_OrigDefaultNodeExec(node, screen);
            return;
        }
        padassign::UseGameLayout(g_ControlType);
        ShowButtons();
        g_SetListTextAndIcon(screen, 0);
        MarkChangedSEH(screen, true);
    }

    Hook g_Hooks[] =
    {
        { "GamePauseMenuItemTable label getter", 0, kGetItemTextEntry, nullptr, sizeof(kGetItemTextEntry),
          reinterpret_cast<void*>(&hk_GetLabel), reinterpret_cast<void**>(&g_OrigGetLabel), nullptr },
        { "GamePauseMenuItemTable help getter", 0, kGetItemTextEntry, nullptr, sizeof(kGetItemTextEntry),
          reinterpret_cast<void*>(&hk_GetHelp), reinterpret_cast<void**>(&g_OrigGetHelp), nullptr },
        { "GamePauseMenuItemTable decide event", 0, kCreateDecideEventEntry, kCreateDecideEventMask, sizeof(kCreateDecideEventEntry),
          reinterpret_cast<void*>(&hk_CreateDecideEvent), reinterpret_cast<void**>(&g_OrigCreateDecideEvent), nullptr },
        { "GamePauseMenuItemTable page children", 0, kCopyPageChildrenEntry, nullptr, sizeof(kCopyPageChildrenEntry),
          reinterpret_cast<void*>(&hk_CopyPageChildren), reinterpret_cast<void**>(&g_OrigCopyPageChildren), nullptr },
        { "KeyMappingAct selector setup", 0, kSetupSelectorsEntry, nullptr, sizeof(kSetupSelectorsEntry),
          reinterpret_cast<void*>(&hk_SetupSelectors), reinterpret_cast<void**>(&g_OrigSetupSelectors), nullptr },
        { "GetKeyMappingCategory", 0, kGetCategoryEntry, kGetCategoryMask, sizeof(kGetCategoryEntry),
          reinterpret_cast<void*>(&hk_GetCategory), reinterpret_cast<void**>(&g_OrigGetCategory), nullptr },
        { "KeyMappingAct key capture", 0, kCaptureKeyEntry, nullptr, sizeof(kCaptureKeyEntry),
          reinterpret_cast<void*>(&hk_CaptureKey), reinterpret_cast<void**>(&g_OrigCaptureKey), nullptr },
        { "KeyMappingAct changed check", 0, kIsKeyChangedEntry, nullptr, sizeof(kIsKeyChangedEntry),
          reinterpret_cast<void*>(&hk_IsKeyChanged), reinterpret_cast<void**>(&g_OrigIsKeyChanged), nullptr },
        { "KeyMappingAct key restore", 0, kRestoreKeysEntry, nullptr, sizeof(kRestoreKeysEntry),
          reinterpret_cast<void*>(&hk_RestoreKeys), reinterpret_cast<void**>(&g_OrigRestoreKeys), nullptr },
        { "KeyMappingAct default", 0, kDefaultNodeExecEntry, kDefaultNodeExecMask, sizeof(kDefaultNodeExecEntry),
          reinterpret_cast<void*>(&hk_DefaultNodeExec), reinterpret_cast<void**>(&g_OrigDefaultNodeExec), nullptr },
    };

    void RemoveHooks()
    {
        for (Hook& hook : g_Hooks)
        {
            if (hook.target)
                DisableAndRemoveHook(hook.target);
            hook.target = nullptr;
            *hook.original = nullptr;
        }
    }

    bool InstallHook(Hook& hook)
    {
        void* fn = hook.address ? ResolveGameAddress(hook.address) : nullptr;
        if (!fn)
        {
            LogError("ControllerAssign: no %s address on %s, CONTROLLER ASSIGNMENTS off", hook.function, GetGameBuildName(gGameBuild));
            return false;
        }
        const HookEntry state = hook.mask ? CheckHookEntryMasked(fn, hook.entry, hook.mask, hook.entrySize)
                                          : CheckHookEntry(fn, hook.entry, hook.entrySize);
        if (state == HookEntry::Unknown)
        {
            char found[160] = {};
            DescribeFunctionEntry(fn, found, sizeof(found));
            LogError("ControllerAssign: the %s is not the known code on %s (%s), CONTROLLER ASSIGNMENTS off",
                     hook.function, GetGameBuildName(gGameBuild), found);
            return false;
        }
        if (state == HookEntry::ForeignHook)
        {
            char what[96] = {};
            std::snprintf(what, sizeof(what), "ControllerAssign: %s", hook.function);
            LogHookedOnTop(what, fn);
        }
        if (!CreateAndEnableHook(fn, hook.detour, hook.original))
        {
            LogError("ControllerAssign: %s hook failed at %p, CONTROLLER ASSIGNMENTS off", hook.function, fn);
            return false;
        }
        hook.target = fn;
        return true;
    }
}

namespace controllerassign
{
    int ListEntries(int controlType, Entry* entries, int limit)
    {
        int count = 0;
        if (!g_Installed || !entries)
            return 0;
        for (const PageSource& source : kPageSources)
            for (int i = 0; i < source.rowCount && count < limit; ++i)
                if (ResolveRow(source.rows[i], controlType, entries[count]))
                    ++count;
        return count;
    }

    int ButtonOf(int controlType, const Entry& entry)
    {
        return entry.action >= 0 ? padassign::ButtonOfAction(controlType, entry.action) : padassign::ButtonOfName(entry.name);
    }

    bool GiveButton(int controlType, const Entry& entry, int button)
    {
        return entry.action >= 0 ? padassign::GiveActionButton(controlType, entry.action, button)
                                 : padassign::GiveNameButton(entry.name, button);
    }

    const char* ButtonIconName(int button)
    {
        return button >= 0 && button < padassign::kButtonCount ? kButtonIcon[button] : kNoButtonIcon;
    }
}

bool Install_ControllerAssignMenu_Hooks()
{
    if (g_Installed)
        return true;

    g_SetListTextAndIcon = reinterpret_cast<SetListTextAndIcon_t>(ResolveGameAddress(gAddr.KeyMappingAct_SetListTextAndIcon));
    g_LangIdToKey        = reinterpret_cast<LangIdToKey_t>(ResolveGameAddress(gAddr.Ui_LangIdToKey));
    g_GetLangText        = reinterpret_cast<GetLangText_t>(ResolveGameAddress(gAddr.Ui_GetLangText));
    if (!g_SetListTextAndIcon || !g_LangIdToKey || !g_GetLangText)
    {
        LogError("ControllerAssign: no address on %s, CONTROLLER ASSIGNMENTS off", GetGameBuildName(gGameBuild));
        return false;
    }
    g_EntryLabel = LangKeySEH(kEntryLabel);
    g_EntryHelp  = LangKeySEH(kEntryHelp);
    if (!g_EntryLabel || !g_EntryHelp)
    {
        LogError("ControllerAssign: the game's lang id hash failed on %s, CONTROLLER ASSIGNMENTS off", GetGameBuildName(gGameBuild));
        return false;
    }

    const std::uintptr_t addresses[] =
    {
        gAddr.GamePauseMenuItemTable_GetLabelLangId,
        gAddr.GamePauseMenuItemTable_GetHelpLangId,
        gAddr.GamePauseMenuItemTable_CreateDecideEvent,
        gAddr.GamePauseMenuItemTable_CopyPageChildren,
        gAddr.KeyMappingAct_SetupSelectors,
        gAddr.KeyMapping_GetCategory,
        gAddr.KeyMappingAct_CaptureKey,
        gAddr.KeyMappingAct_IsKeyChanged,
        gAddr.KeyMappingAct_RestoreKeys,
        gAddr.KeyMappingAct_DefaultNode_Exec,
    };
    static_assert(sizeof(addresses) / sizeof(addresses[0]) == sizeof(g_Hooks) / sizeof(g_Hooks[0]), "one address per hook, same order");
    for (std::size_t i = 0; i < sizeof(g_Hooks) / sizeof(g_Hooks[0]); ++i)
    {
        g_Hooks[i].address = addresses[i];
        if (!InstallHook(g_Hooks[i]))
        {
            RemoveHooks();
            return false;
        }
    }
    for (int button = 0; button < padassign::kButtonCount; ++button)
        TextIcons::SetPadPicture(kButtonIcon[button], button);
    g_Installed = true;
    return true;
}

void Uninstall_ControllerAssignMenu_Hooks()
{
    if (!g_Installed)
        return;
    RemoveHooks();
    g_Installed = false;
}
