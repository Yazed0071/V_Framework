#pragma once

#include <cstdint>

bool Install_PlacedImpl_ExecuteAfter_Hook();
bool Uninstall_PlacedImpl_ExecuteAfter_Hook();

namespace CustomPlaced
{
    enum class Request : std::uint8_t
    {
        Activate,
        Remove,
    };

    bool IsCustom(std::uint32_t handle);
    bool GetPosition(std::uint32_t handle, float out[3]);
    void Queue(Request kind, std::uint32_t handle);
}
