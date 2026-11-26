#include <pch.h>
#include "ControlTypePageAssign.h"

#include <Windows.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "AddressSet.h"
#include "ControllerAssignMenu.h"
#include "FoxHashes.h"
#include "HookUtils.h"
#include "UiPadLock.h"
#include "log.h"
#include "../player/PadAssign.h"

namespace
{
    using ChangeControllerButton_t = void (__fastcall*)(std::uint8_t* page, std::int32_t pictureState, bool withPicture);
    using PageCall_t               = void (__fastcall*)(std::uint8_t* page);
    using PageAnswer_t             = bool (__fastcall*)(std::uint8_t* page);
    using ProcessDecide_t          = void (__fastcall*)(std::uint8_t* page, std::int32_t kind);
    using RowCall_t                = void (__fastcall*)(std::uint8_t* page, std::uint8_t* row);
    using GetRowHelp_t             = const char* (__fastcall*)(std::uint8_t* page, std::int32_t item);
    using CreateBoxText_t          = void (__fastcall*)(void* textNode, std::uint8_t* textUnits, std::uint32_t unitCount,
                                                        const char* text, bool first, bool second);
    using SetNodeVisible_t         = void (__fastcall*)(void* node, bool visible);
    using LangIdToKey_t            = std::uint64_t* (__fastcall*)(std::uint64_t* out, const char* langId);
    using GetLangText_t            = const char* (__fastcall*)(std::uint64_t key);

    constexpr std::uint8_t kChangeControllerButtonEntry[] =
    {
        0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57,
        0x48, 0x83, 0xEC, 0x30, 0x83, 0xB9, 0xC4, 0x00,
    };
    constexpr std::uint8_t kReadDecideOrCancelEntry[] =
    {
        0x40, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x48, 0xC7, 0x44, 0x24, 0x20, 0xFE, 0xFF, 0xFF, 0xFF, 0x48,
        0x89, 0x5C, 0x24, 0x40, 0x48, 0x89, 0x6C, 0x24,
    };
    constexpr std::uint8_t kSelectRowEntry[] =
    {
        0x48, 0x89, 0x5C, 0x24, 0x18, 0x48, 0x89, 0x7C, 0x24, 0x20, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x30,
        0x8B, 0x81, 0xD0, 0x03, 0x00, 0x00, 0x48, 0x8B,
    };
    constexpr std::uint8_t kProcessDecideEntry[] =
    {
        0x40, 0x57, 0x48, 0x83, 0xEC, 0x40, 0x48, 0x8B, 0xF9, 0x85, 0xD2, 0x0F, 0x84, 0x31, 0x02, 0x00,
        0x00, 0x48, 0x89, 0x5C, 0x24, 0x50, 0xFF, 0xCA,
    };
    constexpr std::uint8_t kStopLabelScrollEntry[] =
    {
        0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0xF6, 0x81, 0xB8, 0x03, 0x00, 0x00, 0x01, 0x48, 0x8B, 0xD9,
        0x74, 0x11, 0x48, 0x8B, 0x89, 0xD8, 0x02, 0x00,
    };
    constexpr std::uint8_t kJump = 0xE9;   // CreateBoxText is a jump into the game's protected code

    // tpp::ui::menu::KeySettingEvCall, the options menu's page
    constexpr std::size_t kPageId          = 0x0C4;
    constexpr std::size_t kRowCount        = 0x0E8;   // page rows plus the OK / CANCEL / DEFAULT row
    constexpr std::size_t kCursorRow       = 0x0EC;
    constexpr std::size_t kShownValue      = 0x0F8;   // the value the page is about to print
    constexpr std::size_t kDecideKind      = 0x100;
    constexpr std::size_t kCommonData      = 0x118;
    constexpr std::size_t kPictureState    = 0x244;
    constexpr std::size_t kLabels          = 0x2D8;   // the controller picture's button labels, one text node each
    constexpr std::size_t kLabelScrollBits = 0x3B8;   // bit n: label n scrolls
    constexpr std::size_t kPictureType     = 0x3BC;   // the Control Type the pause menu's CONTROLS page draws
    constexpr std::size_t kHelpText        = 0x3C0;
    constexpr std::size_t kTextUnits       = 0x770;   // the page's 12 spare text units
    constexpr std::size_t kRows            = 0xBF8;
    constexpr std::size_t kRowStride       = 0x70;
    constexpr std::size_t kRowOriginal     = 0x04;
    constexpr std::size_t kRowCurrent      = 0x08;
    constexpr std::size_t kRowChoices      = 0x0C;
    constexpr std::size_t kRowDefault      = 0x10;
    constexpr std::size_t kPlatform        = 0x60;    // in the common data

    constexpr std::int32_t  kControlTypePage  = 3;
    constexpr std::int32_t  kPicturePage      = 7;    // the pause menu's CONTROLS page: the picture alone
    constexpr std::int32_t  kItemControlType  = 1;    // the page's own first row
    constexpr std::uint32_t kPageControlTypes = 2;    // what that row offers: Action Type, Shooter Type
    constexpr std::int32_t  kDecideBack       = 0;
    constexpr std::int32_t  kDecideOk         = 1;
    constexpr std::int32_t  kDecideDefault    = 2;
    constexpr std::int32_t  kPictureVehicle   = 2;
    constexpr std::uint32_t kPlatformTriggersFirst     = 2;   // the game's own test: 0, 1, 3 and 4 put the bumper labels first
    constexpr std::uint32_t kPlatformLastBumpersFirst  = 4;
    constexpr int           kMaxPageRows      = 16;
    constexpr std::uint32_t kTextUnitCount    = 12;
    constexpr std::uint32_t kButtonBits       = 0xFFFF;

    constexpr int           kLabelCount       = 18;       // 16 buttons, then the two sticks
    constexpr int           kLabelOfL3        = 10;       // the one label the game hides, outside the Normal picture
    constexpr std::uint32_t kButtonLabelBits  = 0xFFFF;
    constexpr std::uint8_t  kLabelOfButton[2][padassign::kButtonCount] =   // by fox.PAD_ number
    {
        //  X  A  B  Y  L2  R2  L1  R1  START SELECT L3  R3  up down left right
        {   2, 0, 1, 3,  8, 11,  9, 12,   14,    15, 10, 13,  4,   5,   7,    6 },   // trigger labels first
        {   2, 0, 1, 3,  9, 12,  8, 11,   14,    15, 10, 13,  4,   5,   7,    6 },   // bumper labels first
    };

    constexpr int kMaxEntries     = 16;
    constexpr int kButtonEndsWait = 9;   // SELECT pauses the game: this row gives it to nothing, it ends a wait

    constexpr const char* kRowLabelId  = "V_option_assign_button";
    constexpr const char* kRowHelpId   = "V_option_assign_button_info";
    constexpr const char* kWaitValueId = "V_option_assign_button_wait";
    constexpr const char* kWaitHelpId  = "V_option_assign_button_wait_info";
    constexpr const char* kCancelId    = "cmmn_cancel";   // the game's own Cancel

    struct Hook
    {
        const char*         function;
        std::uintptr_t      address;
        const std::uint8_t* entry;
        std::size_t         entrySize;
        void*               detour;
        void**              original;
        void*               target;
    };

    ChangeControllerButton_t g_OrigChangeControllerButton = nullptr;
    PageAnswer_t             g_OrigReadDecideOrCancel     = nullptr;
    PageAnswer_t             g_OrigSelectRow              = nullptr;
    ProcessDecide_t          g_OrigProcessDecide          = nullptr;
    ChangeControllerButton_t g_DrawPicture                = nullptr;   // the game's own entry: our hook runs too
    PageCall_t               g_StopLabelScroll            = nullptr;
    RowCall_t                g_PrintRowValue              = nullptr;
    GetRowHelp_t             g_GetRowHelp                 = nullptr;
    CreateBoxText_t          g_CreateBoxText              = nullptr;
    SetNodeVisible_t         g_SetNodeVisible             = nullptr;
    LangIdToKey_t            g_LangIdToKey                = nullptr;
    GetLangText_t            g_GetLangText                = nullptr;
    std::atomic<bool>        g_Installed{ false };
    std::atomic<bool>        g_TextMissingTold{ false };

    // the options menu's own thread from here on
    std::uint8_t*           g_Page = nullptr;         // the page whose row was built last
    int                     g_Type = 0;               // the Control Type the row edits: the one the page shows
    controllerassign::Entry g_Entries[kMaxEntries];
    int                     g_EntryCount = 0;
    padassign::Layout       g_LayoutAtOpen;
    bool                    g_Waiting = false;        // the row waits for a button
    bool                    g_Armed = false;          // the press that started the wait has ended
    bool                    g_Settling = false;       // the wait is over, its button is still down
    void*                   g_HiddenLabel = nullptr;  // the label that sat in the L3 label's place when the picture was last drawn

    template <typename T>
    T& At(std::uint8_t* base, std::size_t offset)
    {
        return *reinterpret_cast<T*>(base + offset);
    }

    const char* TextOfKeySEH(std::uint64_t key)
    {
        __try
        {
            const char* text = key ? g_GetLangText(key) : nullptr;
            return text && text[0] ? text : nullptr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

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

    const char* LangText(const char* langId)   // null while the lang pack has no text for it
    {
        return TextOfKeySEH(LangKeySEH(langId));
    }

    int PageRowCount(std::uint8_t* page)
    {
        const int count = At<std::int32_t>(page, kRowCount) - 1;
        return count < 0 ? 0 : (count > kMaxPageRows ? kMaxPageRows : count);
    }

    std::uint8_t* RowAt(std::uint8_t* page, int index)
    {
        return page + kRows + static_cast<std::size_t>(index) * kRowStride;
    }

    std::uint8_t* OurRow(std::uint8_t* page)
    {
        if (!page || page != g_Page || At<std::int32_t>(page, kPageId) != kControlTypePage)
            return nullptr;
        for (int i = 0; i < PageRowCount(page); ++i)
            if (At<std::int32_t>(RowAt(page, i), 0) == controltypepage::kItemAssignButton)
                return RowAt(page, i);
        return nullptr;
    }

    bool CursorIsOn(std::uint8_t* page, const std::uint8_t* row)
    {
        const std::uint32_t cursor = At<std::uint32_t>(page, kCursorRow);
        return cursor < static_cast<std::uint32_t>(PageRowCount(page)) && RowAt(page, static_cast<int>(cursor)) == row;
    }

    const controllerassign::Entry* PickedEntry(std::int32_t picked)
    {
        return picked >= 0 && picked < g_EntryCount ? &g_Entries[picked] : nullptr;
    }

    void ListEntriesOf(int controlType)
    {
        g_Type = controlType;
        g_EntryCount = controllerassign::ListEntries(controlType, g_Entries, kMaxEntries);
    }

    int ShownControlType(std::uint8_t* page)   // the game's own pick for the picture
    {
        if (At<std::int32_t>(page, kPageId) == kPicturePage)
        {
            const int type = static_cast<std::int8_t>(page[kPictureType]);
            return type >= 0 && type < padassign::kControlTypeCount ? type : 0;
        }
        const std::uint32_t picked = At<std::uint32_t>(page, kRows + kRowCurrent);   // the Control Type row is the page's first
        return picked < kPageControlTypes ? static_cast<int>(picked) : 0;
    }

    const char* HelpText(std::uint8_t* row)
    {
        static char text[512];
        const char* help = LangText(g_Waiting ? kWaitHelpId : kRowHelpId);
        if (!help)
            help = "";
        const controllerassign::Entry* entry = PickedEntry(At<std::int32_t>(row, kRowCurrent));
        const int button = entry ? controllerassign::ButtonOf(g_Type, *entry) : padassign::kNoButton;
        if (g_Waiting)
        {
            const char* cancel = LangText(kCancelId);
            if (button == padassign::kNoButton || !cancel)
                return help;
            std::snprintf(text, sizeof(text), "%s\n<I=G=%s>  %s", help, controllerassign::ButtonIconName(button), cancel);   // its own button again changes nothing
            return text;
        }
        const char* action = entry ? TextOfKeySEH(entry->label) : nullptr;
        if (!action)
            return help;
        if (button == padassign::kNoButton)
            std::snprintf(text, sizeof(text), "%s\n%s", action, help);
        else
            std::snprintf(text, sizeof(text), "%s  <I=G=%s>\n%s", action, controllerassign::ButtonIconName(button), help);
        return text;
    }

    void ShowHelp(std::uint8_t* page, std::uint8_t* row)
    {
        void* node = At<void*>(page, kHelpText);
        if (!node)
            return;
        const char* text = CursorIsOn(page, row) ? HelpText(row) : g_GetRowHelp(page, kItemControlType);   // off our row: the Control Type help, which the game never changes on this page
        if (!text)
            return;
        g_CreateBoxText(node, page + kTextUnits, kTextUnitCount, text, true, true);   // as the page prints its own help
        g_SetNodeVisible(node, true);
    }

    void ShowRowValue(std::uint8_t* page, std::uint8_t* row)   // the game's own way to print a row the cursor may not be on
    {
        const std::int32_t shown = At<std::int32_t>(page, kShownValue);
        At<std::int32_t>(page, kShownValue) = At<std::int32_t>(row, kRowCurrent);
        g_PrintRowValue(page, row);
        At<std::int32_t>(page, kShownValue) = shown;
    }

    void EndWait(std::uint8_t* page, std::uint8_t* row, bool given)
    {
        g_Waiting = false;
        g_Settling = true;
        ShowRowValue(page, row);
        ShowHelp(page, row);
        if (given)
            g_DrawPicture(page, At<std::int32_t>(page, kPictureState), false);
    }

    void TakeButton(std::uint8_t* page, std::uint8_t* row)
    {
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
                return;
            }
            if (!held)
                return;
        }
        const controllerassign::Entry* entry = PickedEntry(At<std::int32_t>(row, kRowCurrent));
        bool given = false;
        if (padRead && !keyboardLeaves && entry && !(held & (1u << kButtonEndsWait)))
            for (int button = 0; button < padassign::kButtonCount && !given; ++button)
                if (held & (1u << button))
                    given = controllerassign::GiveButton(g_Type, *entry, button);
        EndWait(page, row, given);   // a button that stays with the game ends the wait too, with nothing changed
    }

    bool BumperLabelsFirst(std::uint8_t* page, std::int32_t pictureState)
    {
        if (pictureState == kPictureVehicle)
            return true;   // the game fills the Vehicle picture that way on every platform
        const std::uint8_t* common = At<std::uint8_t*>(page, kCommonData);
        const std::uint32_t platform = common ? *reinterpret_cast<const std::uint32_t*>(common + kPlatform) : 0;
        return platform <= kPlatformLastBumpersFirst && platform != kPlatformTriggersFirst;
    }

    void ShowLabel(void** labels, void* label)
    {
        for (int i = 0; label && i < kLabelCount; ++i)
            if (labels[i] == label)
            {
                g_SetNodeVisible(label, true);
                return;
            }
    }

    void __fastcall hk_ChangeControllerButton(std::uint8_t* page, std::int32_t pictureState, bool withPicture)
    {
        padassign::Layout layout;
        const bool active = page && padassign::IsActive();
        if (active)
            padassign::Read(layout);
        const std::uint8_t* does = active ? layout.button[ShownControlType(page)] : nullptr;   // [the game's button] = the button that does its work
        bool moved = false;
        for (int button = 0; does && button < padassign::kButtonCount; ++button)
            moved = moved || does[button] != button;
        void** labels = page ? &At<void*>(page, kLabels) : nullptr;
        if (!moved)
        {
            if (labels && g_HiddenLabel != labels[kLabelOfL3])
                ShowLabel(labels, g_HiddenLabel);   // a moved layout left it hidden; the game only looks after its own L3 label
            g_HiddenLabel = nullptr;
            g_OrigChangeControllerButton(page, pictureState, withPicture);
            return;
        }

        g_StopLabelScroll(page);   // the game's own clean-up, while every label still sits in its own place
        At<std::uint32_t>(page, kLabelScrollBits) = 0;
        void* own[kLabelCount];
        std::memcpy(own, labels, sizeof(own));
        ShowLabel(own, own[kLabelOfL3]);
        ShowLabel(own, g_HiddenLabel);

        const std::uint8_t* labelOf = kLabelOfButton[BumperLabelsFirst(page, pictureState) ? 1 : 0];
        for (int button = 0; button < padassign::kButtonCount; ++button)
            labels[labelOf[button]] = own[labelOf[does[button]]];   // the game writes each button's text where that button's work went
        g_HiddenLabel = labels[kLabelOfL3];
        g_OrigChangeControllerButton(page, pictureState, withPicture);
        std::memcpy(labels, own, sizeof(own));

        const std::uint32_t written = At<std::uint32_t>(page, kLabelScrollBits);
        std::uint32_t scrolls = written & ~kButtonLabelBits;
        for (int button = 0; button < padassign::kButtonCount; ++button)
            if (written & (1u << labelOf[button]))
                scrolls |= 1u << labelOf[does[button]];
        At<std::uint32_t>(page, kLabelScrollBits) = scrolls;
    }

    bool __fastcall hk_ReadDecideOrCancel(std::uint8_t* page)
    {
        std::uint8_t* row = OurRow(page);
        const std::int32_t kindBefore = row ? At<std::int32_t>(page, kDecideKind) : 0;
        const bool decided = g_OrigReadDecideOrCancel(page);
        if (!decided || !row)
            return decided;
        const std::int32_t kind = At<std::int32_t>(page, kDecideKind);
        if (kind == kDecideOk && CursorIsOn(page, row))   // the game takes a confirm on any row as OK
        {
            At<std::int32_t>(page, kDecideKind) = kindBefore;
            if (PickedEntry(At<std::int32_t>(row, kRowCurrent)))
            {
                g_Waiting = true;
                g_Armed = false;
                ShowRowValue(page, row);
                ShowHelp(page, row);
            }
            return false;
        }
        if (kind == kDecideBack)   // the game asks before dropping changes only when a row's value is not its first one
            At<std::int32_t>(row, kRowOriginal) = At<std::int32_t>(row, kRowCurrent) + (padassign::Differs(g_LayoutAtOpen) ? 1 : 0);
        return decided;
    }

    bool __fastcall hk_SelectRow(std::uint8_t* page)
    {
        if (g_Waiting || g_Settling)
        {
            std::uint8_t* row = OurRow(page);
            if (!row)
                g_Waiting = g_Settling = false;   // the page went away in the middle of it
            else if (g_Waiting)
            {
                TakeButton(page, row);
                return true;   // the page reads nothing else this frame
            }
            else
            {
                std::uint32_t held = 0, pressed = 0;
                if (uipad::ButtonWords(held, pressed) && (held & kButtonBits))
                    return true;
                g_Settling = false;
            }
        }
        const std::uint32_t cursor = page ? At<std::uint32_t>(page, kCursorRow) : 0;
        const bool moved = g_OrigSelectRow(page);
        if (page && At<std::uint32_t>(page, kCursorRow) != cursor)
            if (std::uint8_t* row = OurRow(page))
                ShowHelp(page, row);
        return moved;
    }

    void __fastcall hk_ProcessDecide(std::uint8_t* page, std::int32_t kind)
    {
        if (kind == kDecideDefault && OurRow(page))
            for (int type = 0; type < padassign::kControlTypeCount; ++type)
                padassign::UseGameLayout(type);
        g_OrigProcessDecide(page, kind);
    }

    Hook g_Hooks[] =
    {
        { "KeySettingEvCall::ChangeControllerButton", 0, kChangeControllerButtonEntry, sizeof(kChangeControllerButtonEntry),
          reinterpret_cast<void*>(&hk_ChangeControllerButton), reinterpret_cast<void**>(&g_OrigChangeControllerButton), nullptr },
        { "KeySettingEvCall decide reader", 0, kReadDecideOrCancelEntry, sizeof(kReadDecideOrCancelEntry),
          reinterpret_cast<void*>(&hk_ReadDecideOrCancel), reinterpret_cast<void**>(&g_OrigReadDecideOrCancel), nullptr },
        { "KeySettingEvCall::SelectKeySettingMenu", 0, kSelectRowEntry, sizeof(kSelectRowEntry),
          reinterpret_cast<void*>(&hk_SelectRow), reinterpret_cast<void**>(&g_OrigSelectRow), nullptr },
        { "KeySettingEvCall::ProcessDecide", 0, kProcessDecideEntry, sizeof(kProcessDecideEntry),
          reinterpret_cast<void*>(&hk_ProcessDecide), reinterpret_cast<void**>(&g_OrigProcessDecide), nullptr },
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
            LogError("ControlTypePage: no %s address on %s, ASSIGN BUTTON off", hook.function, GetGameBuildName(gGameBuild));
            return false;
        }
        const HookEntry state = CheckHookEntry(fn, hook.entry, hook.entrySize);
        if (state == HookEntry::Unknown)
        {
            char found[160] = {};
            DescribeFunctionEntry(fn, found, sizeof(found));
            LogError("ControlTypePage: %s is not the known code on %s (%s), ASSIGN BUTTON off",
                     hook.function, GetGameBuildName(gGameBuild), found);
            return false;
        }
        if (state == HookEntry::ForeignHook)
        {
            char what[96] = {};
            std::snprintf(what, sizeof(what), "ControlTypePage: %s", hook.function);
            LogHookedOnTop(what, fn);
        }
        if (!CreateAndEnableHook(fn, hook.detour, hook.original))
        {
            LogError("ControlTypePage: %s hook failed at %p, ASSIGN BUTTON off", hook.function, fn);
            return false;
        }
        hook.target = fn;
        return true;
    }
}

namespace controltypepage
{
    bool RowIsOffered()
    {
        if (!g_Installed.load(std::memory_order_acquire) || !padassign::IsActive())
            return false;
        if (!LangText(kRowLabelId))
        {
            if (!g_TextMissingTold.exchange(true))
                LogWarn("ControlTypePage: no text for %s, is the V_FrameWork lang pack installed? ASSIGN BUTTON stays off "
                        "the Control Type page", kRowLabelId);
            return false;
        }
        controllerassign::Entry entries[kMaxEntries];
        return controllerassign::ListEntries(padassign::ControlType(), entries, kMaxEntries) > 0;
    }

    const char* OpenRow(std::uint8_t* page, std::uint8_t* row)
    {
        g_Page = page;
        g_Waiting = g_Settling = false;
        ListEntriesOf(padassign::ControlType());
        padassign::Read(g_LayoutAtOpen);
        At<std::int32_t>(row, kRowOriginal) = 0;
        At<std::int32_t>(row, kRowCurrent)  = 0;
        At<std::int32_t>(row, kRowChoices)  = g_EntryCount;
        At<std::int32_t>(row, kRowDefault)  = 0;
        const char* label = LangText(kRowLabelId);
        return label ? label : kRowLabelId;
    }

    const char* RowHelp(std::uint8_t* page)
    {
        std::uint8_t* row = OurRow(page);
        return row ? HelpText(row) : "";
    }

    const char* RowValue(std::uint8_t* page, std::int32_t shown)
    {
        const char* text = nullptr;
        if (g_Waiting && OurRow(page))
            text = LangText(kWaitValueId);
        else if (const controllerassign::Entry* entry = PickedEntry(shown))
            text = TextOfKeySEH(entry->label);
        return text ? text : "";
    }

    void RowChanged(std::uint8_t* page, std::uint8_t* row)
    {
        std::uint8_t* ours = OurRow(page);
        if (!ours || !row)
            return;
        if (row == ours)
        {
            ShowHelp(page, ours);
            return;
        }
        if (At<std::int32_t>(row, 0) != kItemControlType)
            return;
        const controllerassign::Entry* was = PickedEntry(At<std::int32_t>(ours, kRowCurrent));
        const int action = was ? was->action : -1;
        const int name   = was ? was->name : -1;
        ListEntriesOf(ShownControlType(page));
        std::int32_t picked = 0;   // the same action stays picked where the other Control Type lists it
        for (int i = 0; i < g_EntryCount; ++i)
            if (g_Entries[i].action == action && g_Entries[i].name == name)
                picked = i;
        At<std::int32_t>(ours, kRowOriginal) = picked;
        At<std::int32_t>(ours, kRowCurrent)  = picked;
        At<std::int32_t>(ours, kRowChoices)  = g_EntryCount;
        ShowRowValue(page, ours);
    }

    void Kept(std::uint8_t* page)
    {
        if (!OurRow(page))
            return;
        g_Waiting = g_Settling = false;
        padassign::Read(g_LayoutAtOpen);
    }

    void Restored(std::uint8_t* page)
    {
        if (!OurRow(page))
            return;
        g_Waiting = g_Settling = false;
        padassign::Write(g_LayoutAtOpen);
    }
}

bool Install_ControlTypePageAssign_Hooks()
{
    if (g_Installed.load(std::memory_order_acquire))
        return true;

    void* stopLabelScroll = ResolveGameAddress(gAddr.KeySettingEvCall_StopControllerLabelScroll);
    void* createBoxText   = ResolveGameAddress(gAddr.Ui_CreateBoxText);
    g_DrawPicture     = reinterpret_cast<ChangeControllerButton_t>(ResolveGameAddress(gAddr.KeySettingEvCall_ChangeControllerButton));
    g_StopLabelScroll = reinterpret_cast<PageCall_t>(stopLabelScroll);
    g_PrintRowValue   = reinterpret_cast<RowCall_t>(ResolveGameAddress(gAddr.KeySettingEvCall_DispKeySettingParam));
    g_GetRowHelp      = reinterpret_cast<GetRowHelp_t>(ResolveGameAddress(gAddr.KeySettingEvCall_GetListInfoText));
    g_CreateBoxText   = reinterpret_cast<CreateBoxText_t>(createBoxText);
    g_SetNodeVisible  = reinterpret_cast<SetNodeVisible_t>(ResolveGameAddress(gAddr.ModelNode_SetVisible));
    g_LangIdToKey     = reinterpret_cast<LangIdToKey_t>(ResolveGameAddress(gAddr.Ui_LangIdToKey));
    g_GetLangText     = reinterpret_cast<GetLangText_t>(ResolveGameAddress(gAddr.Ui_GetLangText));
    if (!g_DrawPicture || !g_StopLabelScroll || !g_PrintRowValue || !g_GetRowHelp || !g_CreateBoxText || !g_SetNodeVisible
        || !g_LangIdToKey || !g_GetLangText)
    {
        LogError("ControlTypePage: no address on %s, ASSIGN BUTTON off", GetGameBuildName(gGameBuild));
        return false;
    }
    std::uint8_t boxTextEntry = 0;
    if (CheckHookEntry(stopLabelScroll, kStopLabelScrollEntry, sizeof(kStopLabelScrollEntry)) == HookEntry::Unknown
        || !ReadEntryBytesSEH(createBoxText, &boxTextEntry, sizeof(boxTextEntry)) || boxTextEntry != kJump)
    {
        LogError("ControlTypePage: the options menu's helpers are not the known code on %s, ASSIGN BUTTON off",
                 GetGameBuildName(gGameBuild));
        return false;
    }

    const std::uintptr_t addresses[] =
    {
        gAddr.KeySettingEvCall_ChangeControllerButton,
        gAddr.KeySettingEvCall_ReadDecideOrCancel,
        gAddr.KeySettingEvCall_SelectKeySettingMenu,
        gAddr.KeySettingEvCall_ProcessDecide,
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
    g_Installed.store(true, std::memory_order_release);
    return true;
}

void Uninstall_ControlTypePageAssign_Hooks()
{
    if (!g_Installed.exchange(false, std::memory_order_acq_rel))
        return;
    RemoveHooks();
}
