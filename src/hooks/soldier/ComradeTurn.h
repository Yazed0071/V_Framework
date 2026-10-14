#pragma once
#include <cstdint>

namespace CallComrade
{
    void TurnToCaller(std::uint32_t comradeIndex);
    void HurryToCaller(std::uint32_t comradeIndex);   // after TurnToCaller: he jogs over instead of walking
    bool TakeReplyCue();
    void StopTurn();
    void ForgetComrade();   // new mission: no turn or jog left over

    void OnSetCommand(std::uint32_t soldierIndex, std::uint8_t* command);
    bool ShouldHoldMovePause(std::uint8_t* soldier, std::uint32_t soldierIndex);   // he stands and answers before walking over
    bool IsWalkingOver(std::uint32_t soldierIndex);          // he heads for where the shout came from, not after the caller
    bool ShouldLeaveOnArrival(std::uint32_t soldierIndex);   // true once: he has stood there long enough, back to his route
}
