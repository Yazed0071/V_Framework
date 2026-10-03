#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace CallMenuItems
{
    enum Item : int
    {
        Knock,
        ActiveSonar,
        Buddy1,
        Buddy2,
        Buddy3,
        Buddy4,
        RequestInfo,
        SellOutComrade,
        RequestCrawl,
        CallComrade,
        RescueBoy,
        FalseRadioResponse,
        BuiltInCount,
    };

    constexpr int kFirstCustomItem = 100;
    constexpr int kNoItem          = -1;

    enum Column : int
    {
        KnockColumn,
        BuddyColumn,
        SonarColumn,
        InterrogationColumn,
        ColumnCount,
    };

    enum Condition : int
    {
        Always,
        Holding,
        NotHolding,
        ConditionCount,
    };

    enum Icon : std::uint8_t   // the HUD's own column logo ids
    {
        NoIcon,
        KnockIcon,
        InterrogationIcon,
        BuddyIcon,
        SonarIcon,
        IconCount,
    };
    constexpr std::uint8_t kHiddenIcon = 0xFF;   // the HUD hides a logo it has no picture for; the rows stay selectable

    struct CustomItem
    {
        std::string                key;               // the item's name in V_FrameWork_State.lua, which keeps it every mission
        std::uint64_t              label       = 0;
        std::uint64_t              description = 0;
        Column                     column      = InterrogationColumn;
        Condition                  when        = Always;
        std::uint32_t              message     = 0;   // StrCode32 sent as ItemSelected's item; 0 sends the item id
        std::uint32_t              exclusive   = 0;   // StrCode32 of the name whose pages this item keeps to itself; 0 shares pages
        int                        inPlaceOf   = kNoItem;
        Icon                       icon        = NoIcon;   // logo of its column on the page it lands on
        std::uint64_t              iconTexture = 0;   // a mod .ftex drawn as that logo instead
        std::uint8_t               iconCell    = 0;   // which quarter of that 2x2 sheet, like the game's own logo sheet
        bool                       hasIconUvShift = false;
        float                      iconUvShift[2]  = {};             // where the shown part starts, as a fraction of the texture; overrides iconCell
        float                      iconUvRepeat[2] = { 0.5f, 0.5f };  // how much of the texture shows; 1 = all of it
        std::vector<std::uint32_t> voices;
    };

    bool IsKnownItem(int item);
    bool HasFixedRow(int item);

    int  AddCustomItem(const CustomItem& item);
    int  FindItemByKey(const std::string& key);
    void SetEnabled(int item, bool enabled);
    void SetVisible(int item, bool visible);
    void SetInPlaceOf(int item, int target);
    void Reset(int item);
    void ResetAll();
    void ClearForNewMission();

    int  PageCount();
    struct IconPicture
    {
        std::uint64_t texture   = 0;   // 0 = the game's own logo
        float         shift[2]  = {};
        float         repeat[2] = { 0.5f, 0.5f };
    };
    IconPicture ColumnIconPicture(int column);   // what this column's logo shows now
}

bool Install_CallMenuCommandSet_UpdateState_Hook();
void Uninstall_CallMenuCommandSet_UpdateState_Hook();
