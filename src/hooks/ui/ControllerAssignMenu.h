#pragma once

#include <cstdint>

// CONTROLLER ASSIGNMENTS on the pause menu's CONTROLS & MANUAL page: the game's Key Assignments screen, with rows that
// wait for a controller button instead of a key.
bool Install_ControllerAssignMenu_Hooks();
void Uninstall_ControllerAssignMenu_Hooks();

namespace controllerassign
{
    struct Entry   // one thing the player can give a controller button
    {
        std::uint64_t label = 0;     // lang key
        int           action = -1;   // padassign::Action, or -1
        int           name = -1;     // padassign::Name, or -1
    };

    int         ListEntries(int controlType, Entry* entries, int limit);   // CONTROLLER ASSIGNMENTS' rows for a Control Type, in its order; none while it is off
    int         ButtonOf(int controlType, const Entry& entry);             // padassign::kNoButton when it has none
    bool        GiveButton(int controlType, const Entry& entry, int button);
    const char* ButtonIconName(int button);                                // a pad button's picture, drawn as <I=G=name>
}
