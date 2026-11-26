#pragma once

#include <cstdint>
#include <string>

// Icons for <I=G=NAME> tags in UI text. The game draws a tag only when its font manager's icon table holds NAME,
// and it fills that table with its own fixed names only.
namespace TextIcons
{
    bool SetFixed(const std::string& name, int padButton, int keyCode);   // false and a warning when refused
    bool SetForAction(const std::string& name, const std::string& action);
    bool SetPicture(const std::string& name, std::uint64_t texturePath, float u, float v, float width, float height, float size);
    bool SetPadPicture(const std::string& name, int padButton);   // the controller's picture of that button, also while the keyboard is in use
    void AddForNewAction(std::uint32_t action);   // an action just added to the button map gets an icon under its own name
    bool FindButton(std::uint32_t action, int& padButton, int& keyCode);
    bool SetActionPadButton(std::uint32_t action, int padButton);   // moves an action of the button map to another button, its icon follows

    void WritePending();   // only from the game's own icon writer (UiDepend::ActHudChecking): the table has no lock
}
