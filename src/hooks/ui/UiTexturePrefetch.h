#pragma once

#include <cstdint>

namespace UiTexturePrefetch
{
    void Prefetch(std::uint64_t textureHash);
    void Acquire(std::uint64_t textureHash);
    void Release(std::uint64_t textureHash);
}
