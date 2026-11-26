#include <pch.h>
#include "SupportHeliCustomizeMenu.h"

#include <Windows.h>
#include <algorithm>
#include <cstring>

#include "FoxHashes.h"
#include "MissionCodeGuard.h"
#include "SupportHeliHookInstall.h"
#include "SupportHeliParts.h"
#include "TppHeli2_Allocate.h"
#include "../equip/EquipDevelop_SetEquipUndeveloped.h"

namespace
{
    using SupportHeliParts::Kind;
    using SupportHeliParts::Part;

    using SetRecord_t      = void(__fastcall*)(std::uint8_t* record, void* argument);
    using QuarkTable_t     = std::uint8_t*(__fastcall*)();
    using GetPage_t        = std::uint32_t(__fastcall*)(void* utility);
    using RecordStep_t     = void(__fastcall*)(std::uint8_t* record);
    using DecideAct_t      = std::uint64_t(__fastcall*)(std::uint8_t* callback, std::uint32_t itemIndex);
    using LangText_t       = const char*(__fastcall*)(void* text, std::uint64_t langId);
    using SetText_t        = void(__fastcall*)(void* text, void* node, void* layout, const char* string, std::uint8_t show);
    using SetTexture_t     = void(__fastcall*)(void* text, void* node, std::uint64_t file, std::uint64_t slotName);
    using FormatText_t     = int(__fastcall*)(void* format, char* buffer, std::size_t size, const char* pattern, ...);
    using SetHeader_t      = void(__fastcall*)(void* header, const char* string);
    using SetGrade_t       = void(__fastcall*)(void* grade, void* node, std::uint16_t row, std::uint32_t zero);
    using SendWindow_t     = void(__fastcall*)(void* ui, void* node, std::uint64_t message, std::uint32_t window);
    using RowOfDevelopId_t = std::uint16_t(__fastcall*)(void* develop, std::uint32_t developId);
    using DevelopIdOfRow_t = std::uint16_t(__fastcall*)(void* develop, std::uint32_t row);
    using RowValue_t       = std::uint64_t*(__fastcall*)(void* develop, std::uint64_t* value, std::uint32_t rowOrDevelopId);

    // CustomizeSelectorCallbackImpl
    constexpr std::size_t   kCallbackUi        = 0x30;
    constexpr std::size_t   kCallbackDevelop   = 0x60;
    constexpr std::size_t   kCallbackRowUsable = 0x140;
    constexpr std::size_t   kCallbackHeader    = 0x548;
    constexpr std::size_t   kCallbackCursor    = 0x560;
    constexpr std::size_t   kCallbackTop       = 0x564;
    constexpr std::size_t   kCallbackCount     = 0x714;
    constexpr std::size_t   kCallbackWindow    = 0x71C;
    constexpr std::size_t   kCallbackKeepsCursor = 0x720;
    constexpr std::size_t   kCallbackRows      = 0x730;    // a part list: one R&D row index per entry,
    constexpr std::size_t   kCallbackEntries   = 0xF30;    // and a 0x1C-byte entry each: kind 2, then the row index again
    constexpr std::size_t   kCallbackLayer     = 0x8888;
    constexpr std::size_t   kCallbackWindowNode = 0x88A0;
    constexpr std::size_t   kEntrySize         = 0x1C;
    constexpr std::uint32_t kEntryIsDevelopRow = 2;
    constexpr std::size_t   kEntryRow          = 4;
    constexpr std::uint8_t  kSubLayer          = 1;        // the page's rows: Body, Main Weapon, ...
    constexpr std::uint8_t  kPartLayer         = 2;        // the list a row opens
    constexpr std::uint32_t kEntriesOnScreen   = 8;
    constexpr std::uint32_t kMostEntries       = 64;

    // CustomizeSelectorRecordCallFunc, one per row on screen
    constexpr std::size_t   kRecordIndex       = 0x08;
    constexpr std::size_t   kRecordText        = 0x38;
    constexpr std::size_t   kRecordGrade       = 0x40;
    constexpr std::size_t   kRecordDevelop     = 0x48;
    constexpr std::size_t   kRecordFormat      = 0x70;
    constexpr std::size_t   kRecordLayout      = 0x78;
    constexpr std::size_t   kRecordTitleNode   = 0x80;
    constexpr std::size_t   kRecordNameNode    = 0x88;
    constexpr std::size_t   kRecordIconNodeA   = 0x90;
    constexpr std::size_t   kRecordIconNodeB   = 0x98;
    constexpr std::size_t   kRecordGradeNode   = 0xF0;
    constexpr std::size_t   kRecordTitle       = 0x108;
    constexpr std::size_t   kRecordName        = 0x148;
    constexpr std::size_t   kRecordTextSize    = 0x40;
    constexpr std::size_t   kRecordLayer       = 0x1B0;

    // the customize page on screen, read the way the game's own record code reads it on the page layer
    constexpr std::size_t   kQuarkMenus        = 0x98;
    constexpr std::size_t   kMenusCustomize    = 0x40;
    constexpr std::size_t   kCustomizeUtility  = 0x30;
    constexpr std::size_t   kUtilityPage       = 0x1D8 / sizeof(void*);

    constexpr std::size_t   kUiSendWindow      = 0x1E0 / sizeof(void*);
    constexpr std::size_t   kTextLang          = 0x750 / sizeof(void*);
    constexpr std::size_t   kTextSet           = 0x708 / sizeof(void*);
    constexpr std::size_t   kTextSetTexture    = 0x518 / sizeof(void*);
    constexpr std::size_t   kFormatPrint       = 0x18 / sizeof(void*);
    constexpr std::size_t   kHeaderSet         = 0x18 / sizeof(void*);
    constexpr std::size_t   kGradeSet          = 0x488 / sizeof(void*);
    constexpr std::size_t   kDevelopRowOf      = 0xE0 / sizeof(void*);
    constexpr std::size_t   kDevelopIdOfRow    = 0x128 / sizeof(void*);
    constexpr std::size_t   kDevelopNameOf     = 0x720 / sizeof(void*);
    constexpr std::size_t   kDevelopIconOf     = 0x770 / sizeof(void*);

    constexpr std::uint32_t kHeliPage          = 0x100000;
    constexpr std::uint32_t kPartWindows[SupportHeliParts::kKindCount] = { 0x100080, 0x100020, 0x100040 };   // window bits the game's heli page leaves free
    constexpr std::uint32_t kVanillaRows       = 6;        // Body, Main Weapon, Option, Armor, Color, OK
    constexpr std::uint32_t kOkRow             = 5;
    constexpr std::uint16_t kNoRow             = 0x400;
    constexpr std::uint32_t kGatlingDevelopId  = 33001;    // GATLING-GUN, the door guns the heli always carries
    constexpr std::uint64_t kOpenWindowMessage = 0x3B155FC0CB29;
    constexpr std::uint64_t kIconSlotName      = 0xCAFB3BBF9889;
    constexpr std::uint64_t kNoPartIcon        = 0x156983D26F5E7C77;
    constexpr std::uint64_t kNoPartName        = 0x56769303B8F3;
    constexpr std::uint64_t kLangMask          = 0xFFFFFFFFFFFF;

    constexpr const char* kRowLabels[SupportHeliParts::kKindCount]        = { "V_heli_prts_outfit", "V_heli_prts_gatling", "V_heli_prts_side_gun" };
    constexpr const char* kVanillaRowLabels[SupportHeliParts::kKindCount] = { "cmmn_suit", "name_hl_33001", "cmmn_wp_machine_gun" };   // "Uniform", "GATLING-GUN", "Machine Gun"

    constexpr std::uint8_t kSetRecordEntry[] =
    {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xFA, 0x48, 0x8B, 0xD9,
    };
    constexpr std::uint8_t kDecideActEntry[] =
    {
        0x0F, 0xB6, 0x81, 0x88, 0x88, 0x00, 0x00, 0x84, 0xC0, 0x0F, 0x84, 0x71, 0x00, 0x00, 0x00, 0x3C,
    };
    constexpr std::uint8_t kSetPartsModeEntry[] =
    {
        0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0x48, 0x8B, 0x49, 0x38, 0x41, 0xB0, 0x01,
    };

    SetRecord_t   g_OrigSetRecord = nullptr;
    DecideAct_t   g_OrigDecideAct = nullptr;
    void*         g_SetRecordTarget = nullptr;
    void*         g_DecideActTarget = nullptr;
    RecordStep_t  g_SetPartsMode = nullptr;
    QuarkTable_t  g_GetQuarkSystemTable = nullptr;

    Kind          g_Rows[SupportHeliParts::kKindCount] = {};   // the custom rows on the heli page, in screen order, before OK
    std::uint32_t g_RowCount = 0;
    std::uint8_t  g_Listed[kMostEntries] = {};                 // the open custom list: the part id per entry, 0 for the first
    std::uint32_t g_ListedCount = 0;
    std::uint32_t g_ListedWindow = 0;

    template <typename T>
    T& At(std::uint8_t* base, std::size_t offset)
    {
        return *reinterpret_cast<T*>(base + offset);
    }

    template <typename Fn>
    Fn Call(void* object, std::size_t slot)
    {
        return reinterpret_cast<Fn>((*static_cast<void***>(object))[slot]);
    }

    bool KindOfWindow(std::uint32_t window, Kind& kind)
    {
        for (std::size_t i = 0; i < SupportHeliParts::kKindCount; ++i)
        {
            if (kPartWindows[i] == window)
            {
                kind = static_cast<Kind>(i);
                return true;
            }
        }
        return false;
    }

    const char* RowLabel(void* text, Kind kind)
    {
        const auto lang = Call<LangText_t>(text, kTextLang);
        const char* label = lang(text, FoxHashes::StrCode64(kRowLabels[static_cast<std::size_t>(kind)]) & kLangMask);
        if (!label || !*label)   // the V_FrameWork lang file has no label yet: a vanilla word stands in
            label = lang(text, FoxHashes::StrCode64(kVanillaRowLabels[static_cast<std::size_t>(kind)]) & kLangMask);
        return label;
    }

    std::uint16_t RowOf(void* develop, std::uint32_t developId)
    {
        return developId ? Call<RowOfDevelopId_t>(develop, kDevelopRowOf)(develop, developId) : kNoRow;
    }

    // the row a custom page row shows as its current pick
    std::uint16_t PickedRow(Kind kind, void* develop)
    {
        Part part{};
        if (SupportHeliParts::PickedPart(kind, part))
            return RowOf(develop, part.developId);
        return kind == Kind::Outfit ? kNoRow : RowOf(develop, kGatlingDevelopId);
    }

    void ListCustomRows(std::uint8_t* callback)
    {
        g_RowCount = 0;
        for (std::size_t i = 0; i < SupportHeliParts::kKindCount; ++i)
        {
            if (SupportHeliParts::HasAny(static_cast<Kind>(i)))
                g_Rows[g_RowCount++] = static_cast<Kind>(i);
        }
        if (g_RowCount == 0)
            return;
        for (std::uint32_t row = kVanillaRows; row < kVanillaRows + g_RowCount; ++row)
            callback[kCallbackRowUsable + row] = 1;
        At<std::uint32_t>(callback, kCallbackCount) = kVanillaRows + g_RowCount;
    }

    void WriteList(std::uint8_t* callback, const std::uint16_t* rows, std::uint32_t count, std::uint32_t picked)
    {
        for (std::uint32_t i = 0; i < count; ++i)
        {
            At<std::uint16_t>(callback, kCallbackRows + i * sizeof(std::uint16_t)) = rows[i];
            std::uint8_t* entry = callback + kCallbackEntries + static_cast<std::size_t>(i) * kEntrySize;
            std::memset(entry, 0, kEntrySize);
            At<std::uint32_t>(entry, 0)         = kEntryIsDevelopRow;
            At<std::uint16_t>(entry, kEntryRow) = rows[i];
            callback[kCallbackRowUsable + i]    = 1;
        }
        At<std::uint32_t>(callback, kCallbackCount) = count;
        auto& cursor = At<std::uint32_t>(callback, kCallbackCursor);
        auto& top    = At<std::uint32_t>(callback, kCallbackTop);
        const bool placesCursor = callback[kCallbackKeepsCursor] == 0;
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

    void ListParts(std::uint8_t* callback, Kind kind, std::uint32_t window)
    {
        void* develop = At<void*>(callback, kCallbackDevelop);
        void* ui      = At<void*>(callback, kCallbackUi);
        void* header  = At<void*>(callback, kCallbackHeader);
        g_ListedCount  = 0;
        g_ListedWindow = window;
        if (!develop || !ui)
            return;

        static Part parts[SupportHeliParts::kMostParts];
        const std::size_t partCount = SupportHeliParts::List(kind, parts, SupportHeliParts::kMostParts);
        const std::uint8_t current = SupportHeliParts::PickedId(kind);

        std::uint16_t rows[kMostEntries];
        rows[0]     = kind == Kind::Outfit ? kNoRow : RowOf(develop, kGatlingDevelopId);   // the Pequod as it is, or its own door gun
        g_Listed[0] = 0;
        std::uint32_t count  = 1;
        std::uint32_t picked = 0;
        for (std::size_t i = 0; i < partCount && count < kMostEntries; ++i)
        {
            if (!EquipDevelop_IsDevelopedByDevelopId(parts[i].developId))
                continue;
            const std::uint16_t row = RowOf(develop, parts[i].developId);
            if (row == kNoRow)
                continue;
            if (parts[i].id == current)
                picked = count;
            rows[count]     = row;
            g_Listed[count] = parts[i].id;
            ++count;
        }
        g_ListedCount = count;
        WriteList(callback, rows, count, picked);
        if (header)
            Call<SetHeader_t>(header, kHeaderSet)(header, RowLabel(ui, kind));
    }

    // what the game's own row function does for Body / Main Weapon / Option / Armor, with our label and row
    void DrawPartRow(std::uint8_t* record, Kind kind)
    {
        void* text    = At<void*>(record, kRecordText);
        void* format  = At<void*>(record, kRecordFormat);
        void* develop = At<void*>(record, kRecordDevelop);
        void* grade   = At<void*>(record, kRecordGrade);
        if (!text || !format || !develop || !grade)
            return;

        g_SetPartsMode(record);
        const auto lang  = Call<LangText_t>(text, kTextLang);
        const auto print = Call<FormatText_t>(format, kFormatPrint);
        char* title = reinterpret_cast<char*>(record + kRecordTitle);
        char* name  = reinterpret_cast<char*>(record + kRecordName);
        print(format, title, kRecordTextSize, "%s", RowLabel(text, kind));

        const std::uint16_t row = PickedRow(kind, develop);
        std::uint64_t icon = kNoPartIcon;
        if (row == kNoRow)
            print(format, name, kRecordTextSize, "%s", lang(text, kNoPartName));
        else
        {
            std::uint64_t value = 0;
            icon = *Call<RowValue_t>(develop, kDevelopIconOf)(develop, &value, row);
            const std::uint16_t developId = Call<DevelopIdOfRow_t>(develop, kDevelopIdOfRow)(develop, row);
            const std::uint64_t nameId = *Call<RowValue_t>(develop, kDevelopNameOf)(develop, &value, developId);
            print(format, name, kRecordTextSize, "%s", lang(text, nameId));
        }

        const auto setText    = Call<SetText_t>(text, kTextSet);
        const auto setTexture = Call<SetTexture_t>(text, kTextSetTexture);
        setText(text, At<void*>(record, kRecordTitleNode), At<void*>(record, kRecordLayout), title, 1);
        setText(text, At<void*>(record, kRecordNameNode), At<void*>(record, kRecordLayout), name, 1);
        setTexture(text, At<void*>(record, kRecordIconNodeA), icon, kIconSlotName);
        setTexture(text, At<void*>(record, kRecordIconNodeB), icon, kIconSlotName);
        Call<SetGrade_t>(grade, kGradeSet)(grade, At<void*>(record, kRecordGradeNode), row, 0);
    }

    bool OnCustomHeliPage(std::uint8_t layer, std::uint32_t window)
    {
        return layer == kSubLayer && window == kHeliPage && g_RowCount != 0 && !MissionCodeGuard::ShouldBypassHooks();
    }

    std::uint32_t CustomizePageSEH()
    {
        __try
        {
            std::uint8_t* table     = g_GetQuarkSystemTable();
            std::uint8_t* menus     = table ? At<std::uint8_t*>(table, kQuarkMenus) : nullptr;
            std::uint8_t* customize = menus ? At<std::uint8_t*>(menus, kMenusCustomize) : nullptr;
            void*         utility   = customize ? At<void*>(customize, kCustomizeUtility) : nullptr;
            return utility ? Call<GetPage_t>(utility, kUtilityPage)(utility) : 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    void __fastcall hkSetRecord(std::uint8_t* record, void* argument)
    {
        if (!record)
        {
            g_OrigSetRecord(record, argument);
            return;
        }
        auto& index = At<std::uint32_t>(record, kRecordIndex);
        const std::uint32_t shown = index;
        if (shown < kOkRow || shown > kOkRow + g_RowCount
            || !OnCustomHeliPage(record[kRecordLayer], CustomizePageSEH()))   // the record's window field is left stale on this layer
        {
            g_OrigSetRecord(record, argument);
            return;
        }
        if (shown == kOkRow + g_RowCount)   // OK moved down past our rows
        {
            index = kOkRow;
            g_OrigSetRecord(record, argument);
            index = shown;
            return;
        }
        index = kVanillaRows;               // a row past OK: the game clears it and draws nothing
        g_OrigSetRecord(record, argument);
        index = shown;
        DrawPartRow(record, g_Rows[shown - kOkRow]);
    }

    std::uint64_t __fastcall hkDecideAct(std::uint8_t* callback, std::uint32_t itemIndex)
    {
        if (!callback || itemIndex < kOkRow
            || !OnCustomHeliPage(callback[kCallbackLayer], At<std::uint32_t>(callback, kCallbackWindow))
            || itemIndex > kOkRow + g_RowCount)
            return g_OrigDecideAct(callback, itemIndex);
        if (itemIndex == kOkRow + g_RowCount)
            return g_OrigDecideAct(callback, kOkRow);

        void* ui = At<void*>(callback, kCallbackUi);
        const Kind kind = g_Rows[itemIndex - kOkRow];
        Call<SendWindow_t>(ui, kUiSendWindow)(ui, At<void*>(callback, kCallbackWindowNode), kOpenWindowMessage,
                                              kPartWindows[static_cast<std::size_t>(kind)]);
        return 0;
    }
}

namespace SupportHeliCustomizeMenu
{
    void AfterSetupItemList(std::uintptr_t callbackAddress)
    {
        auto* callback = reinterpret_cast<std::uint8_t*>(callbackAddress);
        if (!callback || !g_SetRecordTarget || !g_DecideActTarget || MissionCodeGuard::ShouldBypassHooks())
            return;
        const std::uint8_t  layer  = callback[kCallbackLayer];
        const std::uint32_t window = At<std::uint32_t>(callback, kCallbackWindow);
        Kind kind{};
        if (layer == kSubLayer && window == kHeliPage)
            ListCustomRows(callback);
        else if (layer == kPartLayer && KindOfWindow(window, kind))
            ListParts(callback, kind, window);
    }

    bool DecidePart(void* callbackObject, std::uint32_t itemIndex)
    {
        auto* callback = static_cast<std::uint8_t*>(callbackObject);
        Kind kind{};
        if (!callback || !KindOfWindow(At<std::uint32_t>(callback, kCallbackWindow), kind))
            return false;
        if (g_ListedWindow == At<std::uint32_t>(callback, kCallbackWindow) && itemIndex < g_ListedCount)
        {
            SupportHeliParts::SetPicked(kind, g_Listed[itemIndex]);
            if (SupportHeliParts::RebuildNeeded())
                TppHeli2_RebuildPequodModels();
        }
        return true;
    }

    bool HoverPart(void* callbackObject)
    {
        auto* callback = static_cast<std::uint8_t*>(callbackObject);
        Kind kind{};
        return callback && KindOfWindow(At<std::uint32_t>(callback, kCallbackWindow), kind);   // a picked part shows on the heli the next time it is built
    }
}

bool Install_SupportHeliCustomizeMenu_Hooks()
{
    constexpr const char* kNoRows = "custom Pequod outfits, gatlings and side guns get no customize row";
    void* setPartsMode = gAddr.CustomizeSelectorRecordCallFunc_SetPartsMode ? ResolveGameAddress(gAddr.CustomizeSelectorRecordCallFunc_SetPartsMode) : nullptr;
    void* quarkTable   = gAddr.Fox_GetQuarkSystemTable ? ResolveGameAddress(gAddr.Fox_GetQuarkSystemTable) : nullptr;
    if (!setPartsMode || !quarkTable || CheckHookEntry(setPartsMode, kSetPartsModeEntry, sizeof(kSetPartsModeEntry)) == HookEntry::Unknown)
    {
        LogError("SupportHeliWeapon: customize row code missing or changed on this build, %s", kNoRows);
        return false;
    }
    g_SetPartsMode        = reinterpret_cast<RecordStep_t>(setPartsMode);
    g_GetQuarkSystemTable = reinterpret_cast<QuarkTable_t>(quarkTable);

    if (!g_SetRecordTarget)
    {
        g_SetRecordTarget = InstallSupportHeliHook(
            "Pequod customize row", gAddr.CustomizeSelectorRecordCallFunc_SetRecord, kSetRecordEntry, nullptr, sizeof(kSetRecordEntry),
            reinterpret_cast<void*>(&hkSetRecord), reinterpret_cast<void**>(&g_OrigSetRecord), kNoRows);
    }
    if (g_SetRecordTarget && !g_DecideActTarget)
    {
        g_DecideActTarget = InstallSupportHeliHook(
            "Pequod customize row pick", gAddr.CustomizeSelectorCallbackImpl_DecideAct, kDecideActEntry, nullptr, sizeof(kDecideActEntry),
            reinterpret_cast<void*>(&hkDecideAct), reinterpret_cast<void**>(&g_OrigDecideAct), kNoRows);
    }
    return g_SetRecordTarget != nullptr && g_DecideActTarget != nullptr;
}

void Uninstall_SupportHeliCustomizeMenu_Hooks()
{
    UninstallSupportHeliHook(g_DecideActTarget);
    UninstallSupportHeliHook(g_SetRecordTarget);
    g_RowCount = 0;
    g_ListedCount = 0;
}
