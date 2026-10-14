#pragma once

#include <cstddef>
#include <cstdint>

namespace uipad
{
    void NoteOwnDisableRequest(std::uint32_t id, bool held);
    bool HoldsOwnDisableRequest();
    void ReleaseOwnDisableRequests();

    struct OwnQuery
    {
        OwnQuery();
        ~OwnQuery();
        OwnQuery(const OwnQuery&) = delete;
        OwnQuery& operator=(const OwnQuery&) = delete;
    };

    using GameButtonFilter = bool (*)(std::uint32_t name);
    bool SetGameButtonFilter(GameButtonFilter filter);

    bool SetGameMouseFilter(GameButtonFilter filter);

    bool Press(std::uint32_t name, bool& out);
    bool Pressing(std::uint32_t name, bool& out);
    bool Release(std::uint32_t name, bool& out);
    bool Repeat(std::uint32_t name, float first, float interval, bool& out);
    bool LongPressResult(std::uint32_t name, float seconds, int& out);   // 0 idle, 1 held long enough, 2 held, 3 let go early

    bool StickAxis(std::uint32_t stick, float& x, float& y);   // stick 0 is the left one, +x is right, +y is down

    void Uninstall();
}
