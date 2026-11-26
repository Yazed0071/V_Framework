#pragma once

struct lua_State;

namespace FrameCallbacks
{
    bool Set(lua_State* L, const char* name, int fnIdx);

    void Run();

    void NoteFrame();
}
