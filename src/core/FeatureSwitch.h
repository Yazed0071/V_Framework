#pragma once

#include <atomic>
#include <cstddef>
#include <cstring>

// Built-in features Lua can switch off with V_FrameWork.SetFeatureEnabled(name, false). Off only stops new ones from starting.
namespace FeatureSwitch
{
    enum class Feature
    {
        SurveyTeams,
        BackupRequests,
        UavDownChecks,
        OcelotBinocularComments,
        StaffSaluteNews,
        PequodExtraLines,
        DyingMarkerColour,
        DangerLandingZones,
        PequodLzMove,
        PainAndDeathLines,
        BuddyLines,
        PinUpPosterReactions,
        MillerChopperReports,
        SoldierRadioAnswers,
        SoldierConversations,
        StaffTalk,
        LongBarrelRadio,
        RestoredIdroidLines,
        ModerateLandingZones,
        QuietLines,
        LiquidDriving,
        Count
    };

    inline constexpr const char* kNames[] = {
        "SurveyTeams",
        "BackupRequests",
        "UavDownChecks",
        "OcelotBinocularComments",
        "StaffSaluteNews",
        "PequodExtraLines",
        "DyingMarkerColour",
        "DangerLandingZones",
        "PequodLzMove",
        "PainAndDeathLines",
        "BuddyLines",
        "PinUpPosterReactions",
        "MillerChopperReports",
        "SoldierRadioAnswers",
        "SoldierConversations",
        "StaffTalk",
        "LongBarrelRadio",
        "RestoredIdroidLines",
        "ModerateLandingZones",
        "QuietLines",
        "LiquidDriving",
    };
    static_assert(sizeof(kNames) / sizeof(kNames[0]) == static_cast<std::size_t>(Feature::Count), "one name per feature");

    inline std::atomic<bool> g_Off[static_cast<std::size_t>(Feature::Count)] = {};

    inline bool IsOn(Feature feature)
    {
        return !g_Off[static_cast<std::size_t>(feature)].load(std::memory_order_relaxed);
    }

    inline int IndexOf(const char* name)   // -1 when it names no feature
    {
        for (int i = 0; name && i < static_cast<int>(Feature::Count); ++i)
        {
            if (std::strcmp(name, kNames[i]) == 0)
                return i;
        }
        return -1;
    }
}
