#pragma once
#include <cstdint>

namespace FalseRadioResponseCommand
{
    bool          Exec(std::uint8_t* execParam, std::uint8_t* block);
    std::uint64_t Label(bool sitrep);
    std::uint64_t Help(bool sitrep);
}
