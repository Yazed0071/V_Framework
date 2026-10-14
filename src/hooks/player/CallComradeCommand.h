#pragma once
#include <cstddef>
#include <cstdint>

namespace CallComradeCommand
{
    void          Initialize();
    bool          Exec(std::uint8_t* execParam, std::uint8_t* block);
    std::uint64_t Label();
    std::uint64_t Help();
    void          PlayRandomPlayerLine(std::uint8_t* execParam, const std::uint32_t* voices, std::size_t count);
    bool          IsPlayerLinePlaying(std::uint8_t* block);
    void          SendHeldSoldierRequest(std::uint16_t soldierId, std::uint32_t playerIndex, std::uint32_t request);   // CallComrade requests
    std::uint32_t ExcuseOf(std::uint16_t soldierId, std::uint32_t playerIndex);   // the request that has him say why he won't obey, 0 when he will
}
