#pragma once

#include <cstdint>

namespace ItemCount
{
    void Set(std::uint16_t equipId, int count, bool consumeOnUse);
    bool Get(std::uint16_t equipId, int& count, int& max);
    bool SetCount(std::uint16_t equipId, int count);
    bool TakeUse(std::uint16_t equipId, bool& lastOne);   // a Use press; false = a counted item with none left
    void RefillAll();
    void SetCarried(const std::uint16_t* equipIds, int count);   // the counted items in the player's item slots this frame
    bool RefillCarried();                                        // true = one of them was below its count
    bool Any();
}
