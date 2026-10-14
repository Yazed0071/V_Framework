#pragma once

struct lua_State;

// Lua functions that run once per game frame on the Lua thread, also while a mission loads (when Infinite Heaven's
// Update does not run). Lua: V_FrameWork.SetFrameCallback(name, fn).
namespace FrameCallbacks
{
    // stores fn (the value at fnIdx, a function or nil to remove) under name; runs on the Lua thread
    bool Set(lua_State* L, const char* name, int fnIdx);

    // called by the lua_pcall hook after the game's outermost Lua call returns (Lua thread, no Lua running)
    void Run();

    // the UI layout pass counts frames; a callback runs at most once per counted frame
    void NoteFrame();
}
