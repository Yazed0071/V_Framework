#pragma once
#include <cstdint>

namespace CallComrade
{
    void TurnToCaller(std::uint32_t comradeIndex);
    bool TakeReplyCue();
    void StopTurn();

    void OnSetCommand(std::uint32_t soldierIndex, std::uint8_t* command);
    bool ShouldHoldMovePause(std::uint8_t* soldier, std::uint32_t soldierIndex);   // he stands and answers before walking over
}
