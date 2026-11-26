#pragma once

#include <cstdint>

// ASSIGN BUTTON on the options menu's Control Type page: a row that picks one of CONTROLLER ASSIGNMENTS' actions and
// waits for the controller button it should use, and the controller picture drawn with the player's own buttons.
// The row itself is built by the options menu hooks in KeySettingEvCall_SetKeySettingMenu.cpp, which call in here.
namespace controltypepage
{
    constexpr std::int32_t kItemAssignButton = 0x40;   // an options row number past the game's own and V_FrameWork's other rows

    bool        RowIsOffered();
    const char* OpenRow(std::uint8_t* page, std::uint8_t* row);      // the page builds the row; its label
    const char* RowHelp(std::uint8_t* page);
    const char* RowValue(std::uint8_t* page, std::int32_t shown);
    void        RowChanged(std::uint8_t* page, std::uint8_t* row);   // a row of the page got another value
    void        Kept(std::uint8_t* page);                            // OK
    void        Restored(std::uint8_t* page);                        // CANCEL, or the page closed without OK
}

bool Install_ControlTypePageAssign_Hooks();
void Uninstall_ControlTypePageAssign_Hooks();
