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

    enum class KeyRead { Press, Pressing, Release, Repeat, LongPress };
    bool HasPadButton(std::uint32_t name);   // false also for a name the button map lacks: the game's reads jump into its protected code for both
    int KeyboardHalf(KeyRead read, std::uint32_t name, float seconds, bool force);   // the game's keyboard half of a button read, without its pad lookup

    bool StickAxis(std::uint32_t stick, float& x, float& y);   // stick 0 is the left one, +x is right, +y is down
    bool ButtonWords(std::uint32_t& held, std::uint32_t& pressed);   // the UI's pad, bit n = fox.PAD_ number n; pressed: only the frame it went down

    void Uninstall();
}
