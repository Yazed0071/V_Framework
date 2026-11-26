#pragma once

#include <cstdint>

bool Install_AnnounceLogHook();
bool Uninstall_AnnounceLogHook();

struct AnnounceLogSound
{
    std::uint32_t se            = 0;         // 0 silent, 1-134 UI sound, 135-251 iDroid voice
    const char*   name          = nullptr;   // iDroid voice or Wwise event name, instead of se
    std::uint32_t condition     = 0;         // iDroid dialogue line, instead of se and name
    std::uint32_t chara         = 0;
    std::uint32_t dialogueEvent = 0;
    bool          hasExtra      = false;     // a second sound at the same moment; it replaces a voice's partner sound
    std::uint32_t extraSe       = 0;
    const char*   extraName     = nullptr;
};

std::uint32_t Set_AnnounceLogSE(const char* announceLabel, const AnnounceLogSound& sound);
bool Register_AnnounceLogSfx(const char* eventName);
bool Unset_AnnounceLogSE(const char* announceLabel);
bool Unregister_AnnounceLogSfx(const char* eventName);
bool Set_AnnounceLogTypingSpeed(float speed);
bool Set_AnnounceLogHoldTime(float seconds);
void Reset_AnnounceLogTyping();

enum class AnnounceLogPriority { Default, High, Normal, Low };
bool Set_AnnounceLogPriority(const char* announceLabel, AnnounceLogPriority priority);
