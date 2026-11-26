#pragma once

#include <cstdint>

namespace HeliKey
{
    constexpr std::uint32_t kFirstType = 0xE;    // TppHeli2, then TppEnemyHeli, TppOtherHeli, TppOtherHeli2
    constexpr std::uint32_t kLastType  = 0x11;
    constexpr std::uint32_t kCount     = (kLastType - kFirstType + 1) << 9;

    inline bool IsHeliType(std::uint32_t type)
    {
        return type >= kFirstType && type <= kLastType;
    }

    inline std::uint32_t Of(std::uint32_t type, std::uint32_t index)
    {
        return ((type - kFirstType) << 9) | (index & 0x1FFu);
    }

    inline std::uint32_t OfGameObjectId(std::uint32_t gameObjectId)
    {
        return Of(gameObjectId >> 9, gameObjectId & 0x1FFu);
    }

    inline std::uint32_t GameObjectIdOf(std::uint32_t key)
    {
        return (((key >> 9) + kFirstType) << 9) | (key & 0x1FFu);
    }
}
