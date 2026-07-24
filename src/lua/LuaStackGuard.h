#pragma once

#include <Windows.h>
#include <cstdint>

namespace luaguard
{
    inline bool HasStackRoom(void* L, int need)
    {
        if (!L)
            return false;
        if (need < 1)
            return true;

        __try
        {
            const auto* base = reinterpret_cast<const std::uint8_t*>(L);
            const std::uintptr_t top =
                *reinterpret_cast<const std::uintptr_t*>(base + 0x10);
            const std::uintptr_t ci =
                *reinterpret_cast<const std::uintptr_t*>(base + 0x28);
            const std::uintptr_t stackLast =
                *reinterpret_cast<const std::uintptr_t*>(base + 0x38);

            if (!ci || stackLast < top)
                return true;

            const std::uintptr_t ciTop =
                *reinterpret_cast<const std::uintptr_t*>(ci + 0x10);
            if (ciTop < top)
                return true;

            const std::uintptr_t want         = static_cast<std::uintptr_t>(need);
            const std::uintptr_t frameFree     = (ciTop - top) / 0x10u;
            const std::uintptr_t physicalFree  = (stackLast - top) / 0x10u;
            return frameFree >= want && physicalFree >= want;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return true;
        }
    }
}
