#pragma once

#include <cstdint>

namespace GameObjectLife
{
    enum class Kind : std::uint8_t
    {
        None,
        Soldier,
        Hostage,
        Animal,
        Vehicle,
        WalkerGear,
    };

    enum Status : std::uint8_t
    {
        Status_Alive = 0,
        Status_Down  = 1,   // asleep or fainted
        Status_Dead  = 2,
    };

    struct Values
    {
        Kind          kind       = Kind::None;
        std::uint32_t life       = 0;
        std::uint32_t maxLife    = 0;
        std::uint32_t stamina    = 0;
        std::uint32_t maxStamina = 0;   // 0 = this kind has no stamina
        std::uint8_t  status     = Status_Alive;
    };

    bool IsSupportedOnThisBuild();

    Kind KindOf(std::uint32_t gameObjectId);
    bool Read(std::uint32_t gameObjectId, Values& out);

    bool SetLife(std::uint32_t gameObjectId, std::uint32_t life, const char** reason);
    bool SetStamina(std::uint32_t gameObjectId, std::uint32_t stamina, bool sleep, const char** reason);
}

bool Install_GameObjectLife_Hooks();
bool Uninstall_GameObjectLife_Hooks();
