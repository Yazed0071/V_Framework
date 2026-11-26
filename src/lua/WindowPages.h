#pragma once

#include <cstdint>

struct lua_State;

namespace WindowPages
{
    enum class Event : std::uint8_t { Setup, Init, Start, Update, Stop, Finalize, Back };   // Back: an iDroid page saw the Back button

    bool Set(lua_State* L, std::uint64_t windowName, int tableIdx);   // a table of On<Event> functions, or nil to remove
    bool HasPage(std::uint64_t windowName);

    void Note(std::uint64_t windowName, void* window, Event e);       // any thread: queued for Run

    void Run();   // the game's Lua thread, at the frame-callback safe point
}
