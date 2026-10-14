#pragma once

#include <Windows.h>
#include <cstdint>

namespace SoldierRealizedSlot
{
    constexpr std::uint32_t kNotRealized         = 0xFFFFFFFFu;
    constexpr std::size_t   kSoldierTransforms   = 0x18;
    constexpr std::size_t   kSoldierInSlot       = 0x28;  // u16 soldier index for each realized slot
    constexpr std::size_t   kRealizedSlotCount   = 0x44;

    inline bool Table(const std::uint8_t* soldiers, const std::uint16_t*& soldierInSlot, std::uint32_t& slotCount)
    {
        __try
        {
            const std::uint8_t* transforms = *reinterpret_cast<const std::uint8_t* const*>(soldiers + kSoldierTransforms);
            soldierInSlot = *reinterpret_cast<const std::uint16_t* const*>(transforms + kSoldierInSlot);
            slotCount = *reinterpret_cast<const std::uint32_t*>(transforms + kRealizedSlotCount);
            return soldierInSlot != nullptr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    inline std::uint32_t Of(const std::uint8_t* soldiers, std::uint32_t soldierIndex)
    {
        const std::uint16_t* soldierInSlot = nullptr;
        std::uint32_t slotCount = 0;
        if (!soldiers || !Table(soldiers, soldierInSlot, slotCount))
            return kNotRealized;
        __try
        {
            for (std::uint32_t slot = 0; slot < slotCount; ++slot)
            {
                if (soldierInSlot[slot] == soldierIndex)
                    return slot;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
        return kNotRealized;
    }

    inline std::uint32_t SoldierIn(const std::uint8_t* soldiers, std::uint32_t slot)
    {
        const std::uint16_t* soldierInSlot = nullptr;
        std::uint32_t slotCount = 0;
        if (!soldiers || !Table(soldiers, soldierInSlot, slotCount) || slot >= slotCount)
            return kNotRealized;
        __try
        {
            return soldierInSlot[slot];
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return kNotRealized;
        }
    }
}
