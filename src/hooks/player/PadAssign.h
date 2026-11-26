#pragma once

#include <cstdint>

// The player's own choice of controller buttons. The game keeps one table per Control Type that says which pad button
// does each player action; ours is the game's table with buttons trading places, so actions the game keeps on one
// button stay together.
namespace padassign
{
    constexpr int kButtonCount      = 16;   // fox.PAD_ numbers 0 to 15
    constexpr int kControlTypeCount = 4;
    constexpr int kNoButton         = -1;

    enum Action   // PlayerPad bit numbers, the rows of the game's tables
    {
        kActionStance   = 1,
        kActionDash     = 2,
        kActionHold     = 3,
        kActionFire     = 4,
        kActionAction   = 5,
        kActionReload   = 6,
        kActionStock    = 7,
        kActionMbDevice = 8,
        kActionCall     = 9,
        kActionSubject  = 10,
        kActionCqc      = 15,
        kActionEvade    = 18,
    };

    enum Name   // actions of the UI button map that get a button of their own
    {
        kNameLaserWhileAiming,     // UI_LASER_SWITCH
        kNameLaserInWeaponMenu,    // UI_EQUIP_OP_3
        kNameCount,
    };

    struct Layout
    {
        std::uint8_t button[kControlTypeCount][kButtonCount];   // [Control Type][the game's button] = the button that does its work
        std::int8_t  nameButton[kNameCount];
    };

    bool IsActive();                                     // the game reads our tables
    int  ControlType();                                  // the Control Type in use, 0 when it cannot be read
    bool IsOfferedButton(int button);                    // the D-pad and SELECT stay with the game for now
    int  ButtonOfAction(int controlType, int action);    // kNoButton when the game gives the action none
    bool GiveActionButton(int controlType, int action, int button);   // the two buttons trade places
    int  ButtonOfName(int name);                         // kNoButton until its feature has put the action in the button map
    bool GiveNameButton(int name, int button);
    void UseGameLayout(int controlType);
    void Read(Layout& layout);
    void Write(const Layout& layout);
    bool Differs(const Layout& layout);
    void ApplySavedNameButtons();                        // Lua owner thread, cheap to call often
}

bool Install_PadAssign_Hooks();
void Uninstall_PadAssign_Hooks();
