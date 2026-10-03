#pragma once
#include <cstddef>
#include <cstdint>

namespace CallComradeCommand
{
    void          Initialize();
    bool          Exec(std::uint8_t* execParam, std::uint8_t* block);
    std::uint64_t Label();
    void          PlayRandomPlayerLine(std::uint8_t* execParam, const std::uint32_t* voices, std::size_t count);
    bool          IsPlayerLinePlaying(std::uint8_t* block);
}
