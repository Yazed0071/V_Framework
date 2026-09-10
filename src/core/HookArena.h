#pragma once

#include <cstddef>
#include <cstdint>

namespace HookArena
{
    void        ReserveEarly();
    void        LogSummary();
    void        NoteExhausted();
    void*       AllocateSlot(const void* origin, std::size_t maxDistance);
    void        FreeSlot(void* slot);
    void*       AllocateNear(std::uintptr_t nearAddr, std::size_t bytes);
    std::size_t Remaining();
}
