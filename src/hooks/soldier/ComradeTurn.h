#pragma once
#include <cstdint>

namespace CallComrade
{
    void TurnToCaller(std::uint32_t comradeIndex, bool calm);   // calm: called in Sneak, he never comes weapon ready
    void HurryToCaller(std::uint32_t comradeIndex);
    bool TakeReplyCue(std::uint32_t comradeIndex);
    bool HasTurned(std::uint32_t comradeIndex);
    void StopTurn(std::uint32_t comradeIndex);
    void ForgetComrade(std::uint32_t comradeIndex);
    void ForgetComrades();

    void OnSetCommand(std::uint32_t soldierIndex, std::uint8_t* command);
    bool ShouldHoldMovePause(std::uint8_t* soldier, std::uint32_t soldierIndex);   // he stands and answers before walking over
    bool IsWalkingOver(std::uint32_t soldierIndex);
    bool ShouldLeaveOnArrival(std::uint32_t soldierIndex);
    bool ComesCalm(std::uint32_t soldierIndex);
    void OnNoiseNode(std::uint32_t soldierIndex, bool awake);   // his NoticeNoiseAi node starts or ends: the call ends with it
}
