#include "pch.h"
#include "CallMenuCommandSet_UpdateState.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "AddressSet.h"
#include "CallComradeCommand.h"
#include "CallMenuManagerImpl_UpdateInShowing.h"
#include "FalseRadioResponseCommand.h"
#include "HookUtils.h"
#include "log.h"
#include "LuaBroadcaster.h"
#include "MissionCodeGuard.h"
#include "RadioControllerImpl_StepRadioWaitResponse.h"
#include "V_FrameWorkState.h"

namespace
{
    using CallMenuItems::Column;
    using CallMenuItems::Condition;
    using CallMenuItems::Icon;
    using CallMenuItems::IconPicture;

    using UpdateState_t    = void(__fastcall*)(std::uint8_t* commandSet, std::uint8_t* setParam, std::uint8_t setIndex);
    using FillCommandSet_t = void(__fastcall*)(std::uint8_t* setParam, std::uint8_t* commandSet, std::uint8_t setIndex);
    using ExecCommand_t    = bool(__fastcall*)(std::uint8_t* execParam, std::uint8_t* block);
    using SetItemText_t    = void(__fastcall*)(void* ui, std::uint8_t hudItem, std::uint64_t name, std::uint64_t description);
    using GetCallMenuInfo_t = std::uint8_t*(__fastcall*)(void* hud);
    using SetCategoryId_t  = void(__fastcall*)(void* ui, std::uint8_t column, std::uint8_t icon);

    constexpr std::size_t  kCommandSetSize          = 0x68;
    constexpr std::size_t  kSetFillFunction         = 0x60;
    constexpr std::size_t  kCommandSlotSize         = 0x18;
    constexpr std::size_t  kSlotExecOffset          = 0x08;
    constexpr std::size_t  kSlotFlagsOffset         = 0x10;
    constexpr std::size_t  kSlotCount               = 4;
    constexpr std::uint8_t kSlotSelectable          = 1;
    constexpr std::uint8_t kSlotShown               = 2;
    constexpr std::uint8_t kSlotShownAndSelectable  = kSlotSelectable | kSlotShown;

    constexpr std::size_t  kSetParamUi              = 0x18;   // the HUD controller that owns the menu text
    constexpr std::size_t  kSetParamHeldSoldierId   = 0x24;
    constexpr std::size_t  kSetParamColumnStatus    = 0x34;   // one status byte per column
    constexpr std::uint8_t kHoldingInCqc            = 1;
    constexpr std::uint8_t kHoldingUp               = 3;
    constexpr std::size_t  kSetItemTextVtblIndex    = 0x148 / 8;
    constexpr std::size_t  kSetCategoryIdVtblIndex  = 0x138 / 8;
    constexpr std::size_t  kUiHud                   = 0x40;
    constexpr std::size_t  kGetCallMenuInfoVtblIndex = 0x1D8 / 8;
    constexpr std::size_t  kCallMenuInfoItems       = 0x10;   // name and description of each HUD item
    constexpr std::size_t  kCallMenuInfoItemSize    = 0x18;
    constexpr std::size_t  kItemNameOffset          = 0x00;
    constexpr std::size_t  kItemDescriptionOffset   = 0x08;

    constexpr std::size_t  kExecSlot                = 0x24;   // which row of its column the command was picked from
    constexpr std::size_t  kExecHeldSoldierId       = 0x26;
    constexpr std::size_t  kBlockState              = 0x24;
    constexpr std::size_t  kBlockUpdate             = 0x38;
    constexpr std::uint8_t kBlockFinished           = 3;
    constexpr std::uint16_t kNullGameObjectId       = 0xFFFF;

    constexpr std::uint8_t kNoHudItem = 0xFF;
    constexpr std::uint8_t kHudItemOfSlot[CallMenuItems::ColumnCount][kSlotCount] = {  // the game's own row -> HUD item table
        { 0, kNoHudItem, kNoHudItem, kNoHudItem },
        { 1, 2, 3, 4 },
        { 5, kNoHudItem, kNoHudItem, kNoHudItem },
        { 6, 7, 8, 9 },
    };
    constexpr int kFillOrderForFreeRows[kSlotCount] = { 1, 2, 3, 0 };  // keeps 1, 2 and 3 row columns in layouts the HUD can draw

    constexpr std::uint8_t kVanillaIcon[CallMenuItems::ColumnCount] = {  // what CallMenuInfo starts each column with
        CallMenuItems::KnockIcon, CallMenuItems::BuddyIcon, CallMenuItems::SonarIcon, CallMenuItems::InterrogationIcon,
    };

    struct Home
    {
        int column = -1;
        int slot   = -1;
    };

    constexpr Home kBuiltInHome[CallMenuItems::BuiltInCount] = {
        { CallMenuItems::KnockColumn,         0 },
        { CallMenuItems::SonarColumn,         0 },
        { CallMenuItems::BuddyColumn,         0 },
        { CallMenuItems::BuddyColumn,         1 },
        { CallMenuItems::BuddyColumn,         2 },
        { CallMenuItems::BuddyColumn,         3 },
        { CallMenuItems::InterrogationColumn, 1 },
        { CallMenuItems::InterrogationColumn, 2 },
        { CallMenuItems::InterrogationColumn, 3 },
        { CallMenuItems::InterrogationColumn, -1 },
        { CallMenuItems::InterrogationColumn, 1 },
        { CallMenuItems::SonarColumn,         -1 },
    };

    enum class Override : std::int8_t
    {
        None,
        Off,
        On,
    };

    struct ItemSettings
    {
        Override enabled      = Override::None;
        Override visible      = Override::None;
        int      inPlaceOf    = CallMenuItems::kNoItem;
        bool     inPlaceOfSet = false;   // SetInPlaceOf wins over AddCallMenuItem's inPlaceOf until Reset
    };

    struct StoredCustomItem
    {
        CallMenuItems::CustomItem desc;
        ItemSettings              settings;
        int                       savedPage = 0;       // its page and row, kept so it never moves
        int                       savedRow  = -1;
        int                       misses    = 0;
    };

    std::mutex                    g_Mutex;
    std::array<ItemSettings, CallMenuItems::BuiltInCount> g_BuiltInSettings{};
    std::vector<StoredCustomItem> g_CustomItems;
    bool                          g_SavedItemsLoaded = false;

    enum class EntryKind
    {
        Vanilla,
        CallComrade,
        FalseRadioResponse,
        Custom,
    };

    constexpr int kGamePage  = 1;
    constexpr int kNotShown  = 0;
    constexpr int kNoRow     = -1;
    constexpr int kDisplaced = -2;   // a vanilla option whose row another option took
    constexpr int kMostSavedPage = 64;

    struct Entry
    {
        int           item        = CallMenuItems::kNoItem;
        EntryKind     kind        = EntryKind::Vanilla;
        int           column      = 0;
        int           page        = kNotShown;
        int           slot        = kNoRow;
        int           claimedSlot = kNoRow;
        std::uint32_t exclusive   = 0;
        bool          selectable  = true;
        Home          home;
        void*         vanillaExec = nullptr;
        std::uint64_t name        = 0;
        std::uint64_t description = 0;
        std::uint32_t message     = 0;
        Icon          icon        = CallMenuItems::NoIcon;
        IconPicture   picture;
        bool          present     = true;    // false = not offered now, only keeps its saved row from others
        int           savedPage   = 0;
        int           savedRow    = kNoRow;
    };

    struct RowContent
    {
        bool  used = false;
        Entry entry;
    };

    RowContent g_Rows[CallMenuItems::ColumnCount][kSlotCount];
    int        g_PageCount = 1;
    bool       g_Holding   = false;
    std::mutex                 g_PictureMutex;
    CallMenuItems::IconPicture g_ColumnPictures[CallMenuItems::ColumnCount];

    UpdateState_t    g_Orig              = nullptr;
    void*            g_HookTarget        = nullptr;
    FillCommandSet_t g_FillInterrogation = nullptr;
    FillCommandSet_t g_FillRescueBoy     = nullptr;

    template <typename T>
    T& At(std::uint8_t* base, std::size_t offset)
    {
        return *reinterpret_cast<T*>(base + offset);
    }

    template <typename Fn>
    Fn VirtualAt(void* object, std::size_t index)
    {
        return (*reinterpret_cast<Fn**>(object))[index];
    }

    std::uint8_t& SlotFlags(std::uint8_t* commandSet, int slot)
    {
        return commandSet[kSlotFlagsOffset + slot * kCommandSlotSize];
    }

    void*& SlotExec(std::uint8_t* commandSet, int slot)
    {
        return At<void*>(commandSet, kSlotExecOffset + slot * kCommandSlotSize);
    }

    int ColumnCapacity(int column)
    {
        return (column == CallMenuItems::BuddyColumn || column == CallMenuItems::InterrogationColumn)
            ? static_cast<int>(kSlotCount) : 1;
    }

    ItemSettings GameSettingsOf(int item)
    {
        ItemSettings settings;
        if (item == CallMenuItems::CallComrade)
            settings.inPlaceOf = CallMenuItems::Knock;   // knock is greyed out while you hold someone, the only time call em is offered
        else if (item == CallMenuItems::FalseRadioResponse)
            settings.inPlaceOf = CallMenuItems::ActiveSonar;
        return settings;
    }

    void ResetBuiltIns()
    {
        for (int item = 0; item < CallMenuItems::BuiltInCount; ++item)
            g_BuiltInSettings[item] = GameSettingsOf(item);
    }

    StoredCustomItem* CustomAt(int item)
    {
        const int index = item - CallMenuItems::kFirstCustomItem;
        if (index < 0 || index >= static_cast<int>(g_CustomItems.size()))
            return nullptr;
        return &g_CustomItems[index];
    }

    ItemSettings* SettingsOf(int item)
    {
        if (item >= 0 && item < CallMenuItems::BuiltInCount)
            return &g_BuiltInSettings[item];

        StoredCustomItem* custom = CustomAt(item);
        return custom ? &custom->settings : nullptr;
    }

    int IndexOfKey(const std::string& key)
    {
        for (std::size_t index = 0; index < g_CustomItems.size(); ++index)
            if (g_CustomItems[index].desc.key == key)
                return static_cast<int>(index);
        return -1;
    }

    std::int8_t SavedOverride(Override value)
    {
        return value == Override::None ? V_FrameWorkState::kNoOverride : (value == Override::On ? 1 : 0);
    }

    Override LoadedOverride(std::int8_t value)
    {
        return value == V_FrameWorkState::kNoOverride ? Override::None : (value ? Override::On : Override::Off);
    }

    void SaveItem(const StoredCustomItem& stored)
    {
        const CallMenuItems::CustomItem& desc = stored.desc;
        V_FrameWorkState::CallMenuItem saved;
        saved.key             = desc.key;
        saved.label           = desc.label;
        saved.description     = desc.description;
        saved.column          = desc.column;
        saved.when            = desc.when;
        saved.message         = desc.message;
        saved.exclusive       = desc.exclusive;
        saved.inPlaceOf       = desc.inPlaceOf;
        saved.icon            = desc.icon;
        saved.iconTexture     = desc.iconTexture;
        saved.iconCell        = desc.iconCell;
        saved.hasIconUvShift  = desc.hasIconUvShift;
        saved.iconUvShift[0]  = desc.iconUvShift[0];
        saved.iconUvShift[1]  = desc.iconUvShift[1];
        saved.iconUvRepeat[0] = desc.iconUvRepeat[0];
        saved.iconUvRepeat[1] = desc.iconUvRepeat[1];
        saved.voices          = desc.voices;
        saved.enabled         = SavedOverride(stored.settings.enabled);
        saved.visible         = SavedOverride(stored.settings.visible);
        saved.hasInPlaceOfOverride = stored.settings.inPlaceOfSet;
        saved.inPlaceOfOverride    = stored.settings.inPlaceOf;
        saved.page            = stored.savedPage;
        saved.row             = stored.savedRow;
        saved.misses          = stored.misses;
        V_FrameWorkState::SetCallMenuItem(saved);
    }

    void SaveCustom(int item)
    {
        if (const StoredCustomItem* custom = CustomAt(item))
            SaveItem(*custom);
    }

    constexpr int kIconSheetCells = 4;   // an icon .ftex is a 2x2 sheet

    int SavedRowToTake(std::int32_t item)
    {
        return CallMenuItems::HasFixedRow(item) ? item : CallMenuItems::kNoItem;
    }

    StoredCustomItem FromSaved(const V_FrameWorkState::CallMenuItem& saved)
    {
        StoredCustomItem stored;
        CallMenuItems::CustomItem& desc = stored.desc;
        desc.key         = saved.key;
        desc.label       = saved.label;
        desc.description = saved.description;
        desc.column      = (saved.column >= 0 && saved.column < CallMenuItems::ColumnCount)
            ? static_cast<CallMenuItems::Column>(saved.column) : CallMenuItems::InterrogationColumn;
        desc.when        = (saved.when >= 0 && saved.when < CallMenuItems::ConditionCount)
            ? static_cast<CallMenuItems::Condition>(saved.when) : CallMenuItems::Always;
        desc.message     = saved.message;
        desc.exclusive   = saved.exclusive;
        desc.inPlaceOf   = SavedRowToTake(saved.inPlaceOf);
        desc.icon        = (saved.icon > CallMenuItems::NoIcon && saved.icon < CallMenuItems::IconCount)
            ? static_cast<CallMenuItems::Icon>(saved.icon) : CallMenuItems::NoIcon;
        desc.iconTexture = saved.iconTexture;
        desc.iconCell    = static_cast<std::uint8_t>((saved.iconCell >= 0 && saved.iconCell < kIconSheetCells) ? saved.iconCell : 0);
        desc.hasIconUvShift = saved.hasIconUvShift && std::isfinite(saved.iconUvShift[0]) && std::isfinite(saved.iconUvShift[1]);
        if (desc.hasIconUvShift)
        {
            desc.iconUvShift[0] = saved.iconUvShift[0];
            desc.iconUvShift[1] = saved.iconUvShift[1];
        }
        if (std::isfinite(saved.iconUvRepeat[0]) && std::isfinite(saved.iconUvRepeat[1])
            && saved.iconUvRepeat[0] > 0.0f && saved.iconUvRepeat[1] > 0.0f)
        {
            desc.iconUvRepeat[0] = saved.iconUvRepeat[0];
            desc.iconUvRepeat[1] = saved.iconUvRepeat[1];
        }
        desc.voices      = saved.voices;

        stored.settings.enabled      = LoadedOverride(saved.enabled);
        stored.settings.visible      = LoadedOverride(saved.visible);
        stored.settings.inPlaceOfSet = saved.hasInPlaceOfOverride;
        stored.settings.inPlaceOf    = saved.hasInPlaceOfOverride ? SavedRowToTake(saved.inPlaceOfOverride) : desc.inPlaceOf;
        stored.savedPage             = saved.page;
        stored.savedRow              = saved.row;
        stored.misses                = saved.misses;
        return stored;
    }

    void LoadSavedItems()
    {
        if (g_SavedItemsLoaded)
            return;
        g_SavedItemsLoaded = true;

        std::vector<V_FrameWorkState::CallMenuItem> saved = V_FrameWorkState::GetCallMenuItems();
        std::sort(saved.begin(), saved.end(), [](const auto& a, const auto& b) { return a.key < b.key; });   // same ids every launch while the set doesn't change
        for (const V_FrameWorkState::CallMenuItem& item : saved)
            if (IndexOfKey(item.key) < 0)
                g_CustomItems.push_back(FromSaved(item));
    }

    std::unique_lock<std::mutex> LockItems()
    {
        std::unique_lock<std::mutex> lock(g_Mutex);
        LoadSavedItems();   // mod Lua can add or look up items before the hook is installed
        return lock;
    }


    struct HudLabel
    {
        std::uint64_t name        = 0;
        std::uint64_t description = 0;
    };

    HudLabel ReadHudLabel(std::uint8_t* setParam, std::uint8_t hudItem)
    {
        void* ui  = At<void*>(setParam, kSetParamUi);
        void* hud = At<void*>(static_cast<std::uint8_t*>(ui), kUiHud);
        std::uint8_t* info = VirtualAt<GetCallMenuInfo_t>(hud, kGetCallMenuInfoVtblIndex)(hud);

        std::uint8_t* record = info + kCallMenuInfoItems + hudItem * kCallMenuInfoItemSize;
        return { At<std::uint64_t>(record, kItemNameOffset), At<std::uint64_t>(record, kItemDescriptionOffset) };
    }

    constexpr std::size_t kHudItemCount = 10;
    HudLabel g_WrittenLabels[kHudItemCount];   // what we last put on each row; empty while the game's own text is there
    HudLabel g_GameLabels[kHudItemCount];      // each row's own text, kept while ours covers it

    bool IsEmpty(const HudLabel& label)
    {
        return label.name == 0 && label.description == 0;
    }

    HudLabel GameLabel(std::uint8_t* setParam, std::uint8_t hudItem)
    {
        const HudLabel shown = ReadHudLabel(setParam, hudItem);
        HudLabel& written = g_WrittenLabels[hudItem];
        const bool showsOurs = !IsEmpty(written) && shown.name == written.name && shown.description == written.description;
        if (!showsOurs)
        {
            g_GameLabels[hudItem] = shown;   // the game set it, or a new HUD came up
            written = HudLabel{};
        }
        return g_GameLabels[hudItem];
    }

    void WriteRowLabel(std::uint8_t* setParam, std::uint8_t hudItem, std::uint64_t name, std::uint64_t description)
    {
        void* ui = At<void*>(setParam, kSetParamUi);
        VirtualAt<SetItemText_t>(ui, kSetItemTextVtblIndex)(ui, hudItem, name, description);
    }

    void SetRowLabel(std::uint8_t* setParam, std::uint8_t hudItem, std::uint64_t name, std::uint64_t description)
    {
        WriteRowLabel(setParam, hudItem, name, description);
        g_WrittenLabels[hudItem] = { name, description };
    }

    void GiveRowItsOwnLabel(std::uint8_t* setParam, std::uint8_t hudItem)
    {
        if (IsEmpty(g_WrittenLabels[hudItem]))
            return;
        WriteRowLabel(setParam, hudItem, g_GameLabels[hudItem].name, g_GameLabels[hudItem].description);
        g_WrittenLabels[hudItem] = HudLabel{};
    }

    bool ConditionMet(Condition when)
    {
        switch (when)
        {
        case CallMenuItems::Holding:    return g_Holding;
        case CallMenuItems::NotHolding: return !g_Holding;
        default:                        return true;
        }
    }

    bool ApplyOverrides(const ItemSettings& settings, Entry& entry)
    {
        if (settings.visible == Override::Off)
            return false;
        if (settings.enabled == Override::Off)
            entry.selectable = false;
        else if (settings.enabled == Override::On)
            entry.selectable = true;
        return true;
    }

    void PlaceAccordingToSettings(const ItemSettings& settings, Entry& entry)
    {
        if (settings.inPlaceOf != CallMenuItems::kNoItem)
        {
            const Home target = kBuiltInHome[settings.inPlaceOf];
            entry.column      = target.column;
            entry.claimedSlot = target.slot;
        }
    }

    void AddVanillaEntry(std::vector<Entry>& entries, std::uint8_t* setParam, std::uint8_t* commandSet,
                         int item, int column, int slot)
    {
        const std::uint8_t flags = SlotFlags(commandSet, slot);
        if ((flags & kSlotShown) == 0)
            return;

        Entry entry;
        entry.item        = item;
        entry.kind        = EntryKind::Vanilla;
        entry.column      = column;
        entry.home        = { column, slot };
        entry.selectable  = (flags & kSlotSelectable) != 0;
        entry.vanillaExec = SlotExec(commandSet, slot);

        const HudLabel label = GameLabel(setParam, kHudItemOfSlot[column][slot]);
        entry.name        = label.name;
        entry.description = label.description;

        const ItemSettings& settings = g_BuiltInSettings[item];
        if (!ApplyOverrides(settings, entry))
            return;
        PlaceAccordingToSettings(settings, entry);
        entries.push_back(entry);
    }

    void CollectVanillaEntries(std::vector<Entry>& entries, std::uint8_t* setParam, std::uint8_t* const sets[],
                               const FillCommandSet_t fills[], bool& interrogationOffered)
    {
        AddVanillaEntry(entries, setParam, sets[CallMenuItems::KnockColumn], CallMenuItems::Knock, CallMenuItems::KnockColumn, 0);
        for (int slot = 0; slot < static_cast<int>(kSlotCount); ++slot)
            AddVanillaEntry(entries, setParam, sets[CallMenuItems::BuddyColumn], CallMenuItems::Buddy1 + slot,
                            CallMenuItems::BuddyColumn, slot);
        AddVanillaEntry(entries, setParam, sets[CallMenuItems::SonarColumn], CallMenuItems::ActiveSonar, CallMenuItems::SonarColumn, 0);

        std::uint8_t* interrogation = sets[CallMenuItems::InterrogationColumn];
        const FillCommandSet_t fill = fills[CallMenuItems::InterrogationColumn];
        interrogationOffered = fill == g_FillInterrogation
            && (SlotFlags(interrogation, 1) & kSlotShownAndSelectable) == kSlotShownAndSelectable;

        if (fill == g_FillInterrogation)
        {
            AddVanillaEntry(entries, setParam, interrogation, CallMenuItems::RequestInfo,    CallMenuItems::InterrogationColumn, 1);
            AddVanillaEntry(entries, setParam, interrogation, CallMenuItems::SellOutComrade, CallMenuItems::InterrogationColumn, 2);
            AddVanillaEntry(entries, setParam, interrogation, CallMenuItems::RequestCrawl,   CallMenuItems::InterrogationColumn, 3);
        }
        else if (fill == g_FillRescueBoy)
        {
            AddVanillaEntry(entries, setParam, interrogation, CallMenuItems::RescueBoy, CallMenuItems::InterrogationColumn, 1);
        }
    }

    void AddHoldingOrderEntry(std::vector<Entry>& entries, bool interrogationOffered, int item, EntryKind kind,
                              std::uint64_t label, std::uint64_t description, bool selectable)
    {
        if (!g_Holding || !interrogationOffered)
            return;

        Entry entry;
        entry.item        = item;
        entry.kind        = kind;
        entry.column      = kBuiltInHome[item].column;
        entry.name        = label;
        entry.description = description;
        entry.selectable  = selectable;

        const ItemSettings& settings = g_BuiltInSettings[item];
        if (!ApplyOverrides(settings, entry))
            return;
        PlaceAccordingToSettings(settings, entry);
        entries.push_back(entry);
    }

    bool IsHoldingOrder(const Entry& entry)
    {
        return entry.kind == EntryKind::CallComrade || entry.kind == EntryKind::FalseRadioResponse;
    }

    CallMenuItems::IconPicture PictureOf(const CallMenuItems::CustomItem& item)
    {
        constexpr int   kSheetColumns = 2;
        constexpr float kCellSize     = 1.0f / kSheetColumns;

        CallMenuItems::IconPicture picture;
        picture.texture = item.iconTexture;
        if (item.hasIconUvShift)
        {
            picture.shift[0] = item.iconUvShift[0];
            picture.shift[1] = item.iconUvShift[1];
        }
        else
        {
            picture.shift[0] = (item.iconCell % kSheetColumns) * kCellSize;
            picture.shift[1] = (item.iconCell / kSheetColumns) * kCellSize;
        }
        picture.repeat[0] = item.iconUvRepeat[0];
        picture.repeat[1] = item.iconUvRepeat[1];
        return picture;
    }

    void AddCustomEntries(std::vector<Entry>& entries)
    {
        for (std::size_t index = 0; index < g_CustomItems.size(); ++index)
        {
            const StoredCustomItem& custom = g_CustomItems[index];

            Entry entry;
            entry.item        = CallMenuItems::kFirstCustomItem + static_cast<int>(index);
            entry.kind        = EntryKind::Custom;
            entry.column      = custom.desc.column;
            entry.name        = custom.desc.label;
            entry.description = custom.desc.description;
            entry.message     = custom.desc.message;
            entry.exclusive   = custom.desc.exclusive;
            entry.icon        = custom.desc.icon;
            entry.picture     = PictureOf(custom.desc);
            entry.savedPage   = custom.savedPage;
            entry.savedRow    = custom.savedRow;

            entry.present = ConditionMet(custom.desc.when) && ApplyOverrides(custom.settings, entry);
            PlaceAccordingToSettings(custom.settings, entry);
            entries.push_back(entry);
        }
    }


    bool IsAtHome(const Entry& entry, int column, int slot)
    {
        return entry.kind == EntryKind::Vanilla && entry.home.column == column && entry.home.slot == slot;
    }

    struct PageRows
    {
        bool          inUse     = false;
        std::uint32_t exclusive = 0;
        bool          taken[CallMenuItems::ColumnCount][kSlotCount] = {};
    };

    void Take(PageRows& page, Entry& entry, int pageNumber, int slot)
    {
        page.taken[entry.column][slot] = true;
        entry.page = pageNumber;
        entry.slot = slot;
    }

    int FirstFreeRow(const PageRows& page, int column)
    {
        for (int slot : kFillOrderForFreeRows)
            if (slot < ColumnCapacity(column) && !page.taken[column][slot])
                return slot;
        return -1;
    }

    bool WantsHomeRow(const Entry& entry)
    {
        return entry.kind == EntryKind::Vanilla && entry.claimedSlot == kNoRow;
    }

    void PlaceOnGamePage(std::vector<Entry>& entries, PageRows& gamePage)
    {
        for (Entry& entry : entries)
        {
            const bool canClaim = entry.present && entry.claimedSlot >= 0 && entry.claimedSlot < ColumnCapacity(entry.column);
            if (canClaim && !gamePage.taken[entry.column][entry.claimedSlot])
                Take(gamePage, entry, kGamePage, entry.claimedSlot);
        }

        for (Entry& entry : entries)
        {
            if (!WantsHomeRow(entry))
                continue;
            if (gamePage.taken[entry.column][entry.home.slot])
                entry.slot = kDisplaced;
            else
                Take(gamePage, entry, kGamePage, entry.home.slot);
        }

        for (Entry& entry : entries)
        {
            if (!IsHoldingOrder(entry) || entry.claimedSlot != kNoRow)
                continue;
            const int slot = FirstFreeRow(gamePage, entry.column);   // beside the game's rows, as in GZ; on page 2 it pushed every later option along
            if (slot >= 0)
                Take(gamePage, entry, kGamePage, slot);
        }
    }

    void PlaceOnSavedRows(std::vector<Entry>& entries, std::vector<PageRows>& pages)
    {
        for (Entry& entry : entries)
        {
            if (entry.slot != kNoRow || entry.savedPage <= kGamePage || entry.savedPage > kMostSavedPage
                || entry.savedRow < 0 || entry.savedRow >= ColumnCapacity(entry.column))
                continue;

            const std::size_t number = static_cast<std::size_t>(entry.savedPage);
            if (number >= pages.size())
                pages.resize(number + 1);

            PageRows& page = pages[number];
            if ((page.inUse && page.exclusive != entry.exclusive) || page.taken[entry.column][entry.savedRow])
                continue;   // someone else holds it now; the item gets a new row and keeps that one

            page.inUse     = true;
            page.exclusive = entry.exclusive;
            Take(page, entry, entry.savedPage, entry.savedRow);
        }
    }

    void PlaceOnLaterPages(std::vector<Entry>& entries, std::vector<PageRows>& pages)
    {
        for (Entry& entry : entries)
        {
            if (entry.slot != kNoRow || !entry.present)
                continue;

            for (std::size_t number = kGamePage + 1; ; ++number)
            {
                if (number >= pages.size())
                    pages.emplace_back();

                PageRows& page = pages[number];
                if (page.inUse && page.exclusive != entry.exclusive)
                    continue;

                const int slot = FirstFreeRow(page, entry.column);
                if (slot < 0)
                    continue;

                page.inUse     = true;
                page.exclusive = entry.exclusive;
                Take(page, entry, static_cast<int>(number), slot);
                break;
            }
        }
    }

    void RememberPlaces(const std::vector<Entry>& entries)
    {
        const auto lock = LockItems();
        for (const Entry& entry : entries)
        {
            if (entry.kind != EntryKind::Custom || !entry.present || entry.page <= kGamePage
                || entry.page > kMostSavedPage || entry.slot < 0)
                continue;
            if (entry.page == entry.savedPage && entry.slot == entry.savedRow)
                continue;

            StoredCustomItem* custom = CustomAt(entry.item);
            if (!custom)
                continue;
            custom->savedPage = entry.page;
            custom->savedRow  = entry.slot;
            SaveItem(*custom);
        }
    }

    void AssignPages(std::vector<Entry>& entries)
    {
        std::vector<PageRows> pages(kGamePage + 1);
        pages[kGamePage].inUse = true;

        PlaceOnGamePage(entries, pages[kGamePage]);
        PlaceOnSavedRows(entries, pages);
        PlaceOnLaterPages(entries, pages);
    }

    int ShowOnlyFilledPages(std::vector<Entry>& entries)
    {
        entries.erase(std::remove_if(entries.begin(), entries.end(), [](const Entry& entry) { return !entry.present; }),
                      entries.end());

        std::vector<int> filled;
        for (const Entry& entry : entries)
            if (entry.page > kGamePage)
                filled.push_back(entry.page);
        std::sort(filled.begin(), filled.end());
        filled.erase(std::unique(filled.begin(), filled.end()), filled.end());

        for (Entry& entry : entries)
            if (entry.page > kGamePage)
                entry.page = kGamePage + 1 + static_cast<int>(std::lower_bound(filled.begin(), filled.end(), entry.page) - filled.begin());
        return kGamePage + static_cast<int>(filled.size());
    }

    void RepackForHudLayout(RowContent (&rows)[kSlotCount])
    {
        int count = 0;
        for (const RowContent& row : rows)
            count += row.used ? 1 : 0;

        if (count != 3 || !rows[0].used)
            return;

        RowContent packed[kSlotCount];
        int next = 1;
        for (const RowContent& row : rows)
            if (row.used)
                packed[next++] = row;
        for (std::size_t slot = 0; slot < kSlotCount; ++slot)
            rows[slot] = packed[slot];
    }

    void LayOutColumn(const std::vector<Entry>& entries, int column, int page, RowContent (&rows)[kSlotCount])
    {
        for (const Entry& entry : entries)
            if (entry.column == column && entry.page == page && entry.slot >= 0)
                rows[entry.slot] = { true, entry };

        if (ColumnCapacity(column) == static_cast<int>(kSlotCount))
            RepackForHudLayout(rows);
    }


    void __fastcall UpdateCommandFinished(std::uint8_t* block)
    {
        At<std::uint8_t>(block, kBlockState) = kBlockFinished;
    }

    bool RunCustomItem(int item, std::uint8_t* execParam, std::uint8_t* block)
    {
        std::vector<std::uint32_t> voices;
        {
            const auto lock = LockItems();
            if (const StoredCustomItem* custom = CustomAt(item))
                voices = custom->desc.voices;
        }

        CallComradeCommand::PlayRandomPlayerLine(execParam, voices.data(), voices.size());
        At<void*>(block, kBlockUpdate) = reinterpret_cast<void*>(&UpdateCommandFinished);
        return true;
    }

    bool RunVanillaItem(const Entry& entry, int column, int slot, std::uint8_t* execParam, std::uint8_t* block)
    {
        if (IsAtHome(entry, column, slot))
            return reinterpret_cast<ExecCommand_t>(entry.vanillaExec)(execParam, block);

        std::uint8_t& pickedSlot = At<std::uint8_t>(execParam, kExecSlot);
        const std::uint8_t realPickedSlot = pickedSlot;
        pickedSlot = static_cast<std::uint8_t>(entry.home.slot);   // some commands work out what to do from their row

        const bool result = reinterpret_cast<ExecCommand_t>(entry.vanillaExec)(execParam, block);
        pickedSlot = realPickedSlot;
        return result;
    }

    bool RunRow(int column, int slot, std::uint8_t* execParam, std::uint8_t* block)
    {
        const RowContent& row = g_Rows[column][slot];
        if (!row.used)
        {
            At<void*>(block, kBlockUpdate) = reinterpret_cast<void*>(&UpdateCommandFinished);
            return true;
        }

        const Entry entry = row.entry;
        const double sentItem = entry.message != 0 ? static_cast<double>(entry.message) : static_cast<double>(entry.item);
        const std::uint16_t heldSoldier = g_Holding ? At<std::uint16_t>(execParam, kExecHeldSoldierId) : kNullGameObjectId;
        V_FrameWork::EmitMessage("Player", "ItemSelected", sentItem, static_cast<int>(heldSoldier));

        switch (entry.kind)
        {
        case EntryKind::CallComrade:        return CallComradeCommand::Exec(execParam, block);
        case EntryKind::FalseRadioResponse: return FalseRadioResponseCommand::Exec(execParam, block);
        case EntryKind::Custom:             return RunCustomItem(entry.item, execParam, block);
        default:                            return RunVanillaItem(entry, column, slot, execParam, block);
        }
    }

    template <int ColumnIndex, int SlotIndex>
    bool __fastcall ExecRow(std::uint8_t* execParam, std::uint8_t* block)
    {
        return RunRow(ColumnIndex, SlotIndex, execParam, block);
    }

    constexpr ExecCommand_t kExecOfRow[CallMenuItems::ColumnCount][kSlotCount] = {
        { &ExecRow<0, 0>, &ExecRow<0, 1>, &ExecRow<0, 2>, &ExecRow<0, 3> },
        { &ExecRow<1, 0>, &ExecRow<1, 1>, &ExecRow<1, 2>, &ExecRow<1, 3> },
        { &ExecRow<2, 0>, &ExecRow<2, 1>, &ExecRow<2, 2>, &ExecRow<2, 3> },
        { &ExecRow<3, 0>, &ExecRow<3, 1>, &ExecRow<3, 2>, &ExecRow<3, 3> },
    };


    struct GameRows
    {
        std::uint8_t* commandSet = nullptr;
        void*         exec[kSlotCount]  = {};
        std::uint8_t  flags[kSlotCount] = {};
    };
    GameRows g_GameRows[CallMenuItems::ColumnCount];   // each column as the game last left it, before we wrote ours

    bool IsOurRow(std::uint8_t* commandSet, int column, int slot)
    {
        return SlotExec(commandSet, slot) == reinterpret_cast<void*>(kExecOfRow[column][slot]);
    }

    void PutBackGameRows(std::uint8_t* commandSet, int column)
    {
        const GameRows& game = g_GameRows[column];
        if (game.commandSet != commandSet)
            return;

        for (int slot = 0; slot < static_cast<int>(kSlotCount); ++slot)
        {
            if (!IsOurRow(commandSet, column, slot))
                continue;
            SlotExec(commandSet, slot) = game.exec[slot];
            std::uint8_t& flags = SlotFlags(commandSet, slot);
            flags = static_cast<std::uint8_t>((flags & ~kSlotShownAndSelectable) | (game.flags[slot] & kSlotShownAndSelectable));
        }
    }

    void RememberGameRows(std::uint8_t* commandSet, int column)
    {
        GameRows& game = g_GameRows[column];
        game.commandSet = commandSet;
        for (int slot = 0; slot < static_cast<int>(kSlotCount); ++slot)
        {
            if (IsOurRow(commandSet, column, slot))
                SlotFlags(commandSet, slot) &= static_cast<std::uint8_t>(~kSlotShownAndSelectable);   // ours from a menu we never saw the game's rows for
            game.exec[slot]  = SlotExec(commandSet, slot);
            game.flags[slot] = SlotFlags(commandSet, slot);
        }
    }

    void WriteColumn(std::uint8_t* setParam, std::uint8_t* commandSet, int column, const RowContent (&rows)[kSlotCount])
    {
        for (int slot = 0; slot < static_cast<int>(kSlotCount); ++slot)
        {
            SlotFlags(commandSet, slot) &= static_cast<std::uint8_t>(~kSlotShownAndSelectable);
            SlotExec(commandSet, slot)   = reinterpret_cast<void*>(kExecOfRow[column][slot]);

            const RowContent& row = rows[slot];
            if (!row.used)
                continue;

            const Entry& entry = row.entry;
            SlotFlags(commandSet, slot) |= static_cast<std::uint8_t>(kSlotShown | (entry.selectable ? kSlotSelectable : 0));

            if (IsAtHome(entry, column, slot))
                GiveRowItsOwnLabel(setParam, kHudItemOfSlot[column][slot]);
            else
                SetRowLabel(setParam, kHudItemOfSlot[column][slot], entry.name, entry.description);
        }
    }

    void SetColumnIcon(std::uint8_t* setParam, int column, std::uint8_t icon,
                       const CallMenuItems::IconPicture& picture = CallMenuItems::IconPicture{})
    {
        void* ui = At<void*>(setParam, kSetParamUi);
        VirtualAt<SetCategoryId_t>(ui, kSetCategoryIdVtblIndex)(ui, static_cast<std::uint8_t>(column), icon);
        std::lock_guard<std::mutex> lock(g_PictureMutex);
        g_ColumnPictures[column] = picture;
    }

    void ShowColumnIcon(std::uint8_t* setParam, int column, int page, const RowContent (&rows)[kSlotCount])
    {
        for (const RowContent& row : rows)
        {
            const Entry& entry = row.entry;
            if (!row.used || (entry.icon == CallMenuItems::NoIcon && entry.picture.texture == 0))
                continue;
            const std::uint8_t icon = entry.picture.texture != 0 ? kVanillaIcon[column] : entry.icon;   // a mod picture is drawn over a logo the HUD shows
            SetColumnIcon(setParam, column, icon, entry.picture);
            return;
        }
        SetColumnIcon(setParam, column, page == kGamePage ? kVanillaIcon[column] : CallMenuItems::kHiddenIcon);
    }

    bool IsHoldingSomeone(std::uint8_t* setParam, FillCommandSet_t interrogationFill)
    {
        if (g_FillInterrogation && interrogationFill != g_FillInterrogation)
            return false;   // let go: the column runs an empty fill that also leaves its status at 1
        const std::uint8_t status = setParam[kSetParamColumnStatus + CallMenuItems::InterrogationColumn];
        return status == kHoldingInCqc || status == kHoldingUp;
    }

    void BuildMenu(std::uint8_t* firstSet, std::uint8_t* setParam)
    {
        std::uint8_t*    sets[CallMenuItems::ColumnCount];
        FillCommandSet_t fills[CallMenuItems::ColumnCount];
        for (int column = 0; column < CallMenuItems::ColumnCount; ++column)
        {
            sets[column]  = firstSet + column * kCommandSetSize;
            fills[column] = At<FillCommandSet_t>(sets[column], kSetFillFunction);
            PutBackGameRows(sets[column], column);   // a column the game doesn't refill (status not 0, e.g. no sonar) keeps what it had
            fills[column](setParam, sets[column], static_cast<std::uint8_t>(column));
            RememberGameRows(sets[column], column);
        }

        g_Holding = IsHoldingSomeone(setParam, fills[CallMenuItems::InterrogationColumn]);

        std::vector<Entry> entries;
        {
            const auto lock = LockItems();
            bool interrogationOffered = false;
            CollectVanillaEntries(entries, setParam, sets, fills, interrogationOffered);
            const std::uint16_t held = At<std::uint16_t>(setParam, kSetParamHeldSoldierId);
            AddHoldingOrderEntry(entries, interrogationOffered, CallMenuItems::CallComrade, EntryKind::CallComrade,
                                 CallComradeCommand::Label(), CallComradeCommand::Help(), true);
            if (CpRadioResponse::IsAvailable())
            {
                bool sitrep = false;
                const bool waiting = CpRadioResponse::IsWaitingOn(held, sitrep);
                AddHoldingOrderEntry(entries, interrogationOffered, CallMenuItems::FalseRadioResponse,
                                     EntryKind::FalseRadioResponse, FalseRadioResponseCommand::Label(sitrep),
                                     FalseRadioResponseCommand::Help(), waiting);
            }
            AddCustomEntries(entries);
        }

        AssignPages(entries);
        RememberPlaces(entries);
        g_PageCount = ShowOnlyFilledPages(entries);
        const int currentPage = CallMenuPaging::CurrentPage();

        for (int column = 0; column < CallMenuItems::ColumnCount; ++column)
        {
            RowContent (&rows)[kSlotCount] = g_Rows[column];
            for (RowContent& row : rows)
                row = RowContent{};

            LayOutColumn(entries, column, currentPage, rows);
            WriteColumn(setParam, sets[column], column, rows);
            ShowColumnIcon(setParam, column, currentPage, rows);
        }
    }

    void __fastcall FillNothing(std::uint8_t*, std::uint8_t*, std::uint8_t)
    {
    }

    void PushToHudWithoutRefill(std::uint8_t* commandSet, std::uint8_t* setParam, std::uint8_t setIndex)
    {
        auto& fill = At<FillCommandSet_t>(commandSet, kSetFillFunction);
        const FillCommandSet_t vanillaFill = fill;
        fill = &FillNothing;
        g_Orig(commandSet, setParam, setIndex);
        fill = vanillaFill;
    }

    void __fastcall hk_UpdateState(std::uint8_t* commandSet, std::uint8_t* setParam, std::uint8_t setIndex)
    {
        if (MissionCodeGuard::ShouldBypassHooks() || setIndex >= CallMenuItems::ColumnCount)
        {
            if (setIndex < CallMenuItems::ColumnCount)
            {
                PutBackGameRows(commandSet, setIndex);
                SetColumnIcon(setParam, setIndex, kVanillaIcon[setIndex]);
            }
            g_Orig(commandSet, setParam, setIndex);
            return;
        }

        if (setIndex == CallMenuItems::KnockColumn)
            BuildMenu(commandSet, setParam);

        PushToHudWithoutRefill(commandSet, setParam, setIndex);
    }
}

bool CallMenuItems::IsKnownItem(int item)
{
    if (item >= 0 && item < BuiltInCount)
        return true;

    const auto lock = LockItems();
    return CustomAt(item) != nullptr;
}

bool CallMenuItems::HasFixedRow(int item)
{
    return item >= 0 && item < BuiltInCount && kBuiltInHome[item].slot >= 0;
}

int CallMenuItems::AddCustomItem(const CustomItem& item)
{
    const auto lock = LockItems();

    int index = IndexOfKey(item.key);
    if (index < 0)
    {
        index = static_cast<int>(g_CustomItems.size());
        g_CustomItems.emplace_back();
    }

    StoredCustomItem& stored = g_CustomItems[index];
    stored.desc   = item;
    stored.misses = 0;
    if (!stored.settings.inPlaceOfSet)
        stored.settings.inPlaceOf = item.inPlaceOf;   // Enable/Disable/Show/Hide/SetInPlaceOf made earlier stay
    SaveItem(stored);
    return kFirstCustomItem + index;
}

int CallMenuItems::FindItemByKey(const std::string& key)
{
    const auto lock = LockItems();
    const int index = key.empty() ? -1 : IndexOfKey(key);
    return index >= 0 ? kFirstCustomItem + index : kNoItem;
}

void CallMenuItems::SetEnabled(int item, bool enabled)
{
    const auto lock = LockItems();
    if (ItemSettings* settings = SettingsOf(item))
    {
        settings->enabled = enabled ? Override::On : Override::Off;
        SaveCustom(item);
    }
}

void CallMenuItems::SetVisible(int item, bool visible)
{
    const auto lock = LockItems();
    if (ItemSettings* settings = SettingsOf(item))
    {
        settings->visible = visible ? Override::On : Override::Off;
        SaveCustom(item);
    }
}

void CallMenuItems::SetInPlaceOf(int item, int target)
{
    const auto lock = LockItems();
    if (ItemSettings* settings = SettingsOf(item))
    {
        settings->inPlaceOf    = target;
        settings->inPlaceOfSet = true;
        SaveCustom(item);
    }
}

void CallMenuItems::Reset(int item)
{
    const auto lock = LockItems();
    if (item >= 0 && item < BuiltInCount)
    {
        g_BuiltInSettings[item] = GameSettingsOf(item);
        return;
    }

    if (StoredCustomItem* custom = CustomAt(item))
    {
        ItemSettings settings;
        settings.inPlaceOf = custom->desc.inPlaceOf;
        custom->settings   = settings;
        SaveItem(*custom);
    }
}

void CallMenuItems::ResetAll()
{
    const auto lock = LockItems();
    ResetBuiltIns();
    for (StoredCustomItem& custom : g_CustomItems)
    {
        ItemSettings settings;
        settings.inPlaceOf = custom.desc.inPlaceOf;
        custom.settings    = settings;
        SaveItem(custom);
    }
}

void CallMenuItems::ClearForNewMission()
{
    const auto lock = LockItems();
    ResetBuiltIns();   // custom items keep their Enable/Disable/Show/Hide every mission
}

int CallMenuItems::PageCount()
{
    return g_PageCount;
}

CallMenuItems::IconPicture CallMenuItems::ColumnIconPicture(int column)
{
    if (column < 0 || column >= ColumnCount)
        return IconPicture{};
    std::lock_guard<std::mutex> lock(g_PictureMutex);
    return g_ColumnPictures[column];
}

bool Install_CallMenuCommandSet_UpdateState_Hook()
{
    if (g_HookTarget)
        return true;

    if (!gAddr.CallMenuCommandSet_UpdateState)
    {
        LogError("CallMenu: UpdateState address missing for %s, call menu can't be changed and no call comrade option",
                 GetGameBuildName(gGameBuild));
        return true;
    }

    CallMenuItems::ClearForNewMission();
    CallComradeCommand::Initialize();
    if (gAddr.CallMenuManagerImpl_UpdateCommandSetInterrogation)
        g_FillInterrogation = reinterpret_cast<FillCommandSet_t>(
            ResolveGameAddress(gAddr.CallMenuManagerImpl_UpdateCommandSetInterrogation));
    if (gAddr.CallMenuManagerImpl_UpdateCommandSetRescueBoy)
        g_FillRescueBoy = reinterpret_cast<FillCommandSet_t>(
            ResolveGameAddress(gAddr.CallMenuManagerImpl_UpdateCommandSetRescueBoy));

    void* target = ResolveGameAddress(gAddr.CallMenuCommandSet_UpdateState);
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hk_UpdateState),
                             reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("CallMenu: UpdateState hook failed at %p, call menu can't be changed and no call comrade option", target);
        return false;
    }

    g_HookTarget = target;
    return true;
}

void Uninstall_CallMenuCommandSet_UpdateState_Hook()
{
    if (g_HookTarget)
    {
        DisableAndRemoveHook(g_HookTarget);
        g_HookTarget = nullptr;
        g_Orig       = nullptr;
    }
    {
        std::lock_guard<std::mutex> lock(g_PictureMutex);
        for (CallMenuItems::IconPicture& picture : g_ColumnPictures)
            picture = CallMenuItems::IconPicture{};
    }
    CallMenuItems::ClearForNewMission();
}
