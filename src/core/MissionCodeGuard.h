#pragma once

#include <cstdint>

namespace MissionCodeGuard
{
    std::uint16_t GetCurrentMissionCode();
    bool IsMissionBlocked(std::uint16_t missionCode);
    bool ShouldBypassHooks();
    bool IsFob();   // also true when this build's mission flags can't be read
}


#define MISSION_GUARD_ORIGINAL_VOID(origFn, ...)         \
    do                                                   \
    {                                                    \
        if (MissionCodeGuard::ShouldBypassHooks())       \
        {                                                \
            if ((origFn) != nullptr)                     \
                (origFn)(__VA_ARGS__);                   \
            return;                                      \
        }                                                \
    } while (0)


#define MISSION_GUARD_RETURN_VOID()                      \
    do                                                   \
    {                                                    \
        if (MissionCodeGuard::ShouldBypassHooks())       \
            return;                                      \
    } while (0)


#define MISSION_GUARD_RETURN_FALSE()                     \
    do                                                   \
    {                                                    \
        if (MissionCodeGuard::ShouldBypassHooks())       \
            return false;                                \
    } while (0)


#define MISSION_GUARD_ORIGINAL_RET(origFn, ...)          \
    do                                                   \
    {                                                    \
        if (MissionCodeGuard::ShouldBypassHooks())       \
            return (origFn)(__VA_ARGS__);                \
    } while (0)