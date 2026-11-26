#pragma once

#include <Windows.h>
#include <cstdint>

namespace mbDvcCustomPage
{
    bool Install(HMODULE hGame);
    void Uninstall();

    bool OpenPage(std::uint64_t windowName);    // Lua thread: puts the window inside the iDroid and shows it
    bool ClosePage(std::uint64_t windowName);   // 0 gives the menu back without touching a page
    bool IsPageOpen(std::uint64_t windowName);
    void Tick();   // UI frame: gives the pad back when the iDroid closed with a page open
}
