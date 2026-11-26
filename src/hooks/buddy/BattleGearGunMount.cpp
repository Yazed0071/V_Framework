#include <pch.h>

#include "BattleGearGunMount.h"

#include <cstdint>

#include "BattleGearArmory.h"
#include "BattleGearWeapons.h"
#include "WalkerGear2Impl_SetUpMainWeapon.h"

namespace
{
    constexpr std::size_t   kActionWalkers   = 0xC0;   // the walkers' object interface
    constexpr std::size_t   kInterfaceType   = 0x08;
    constexpr std::uint16_t kBuddyWalkerType = 0x2C;

    void* BuddyWalkerInterfaceSEH(void* action)
    {
        __try
        {
            auto* walkerInterface = *reinterpret_cast<std::uint8_t**>(static_cast<std::uint8_t*>(action) + kActionWalkers);
            return walkerInterface && *reinterpret_cast<const std::uint16_t*>(walkerInterface + kInterfaceType) == kBuddyWalkerType
                ? walkerInterface : nullptr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }
}

namespace battleGearGunMount
{
    void AfterPartsPlaced(void* action)
    {
        alignas(16) float matrix[16];
        if (!action || !battleGearWeapons::LoadoutApplies() || !battleGearArmory::MainGunMountMatrix(matrix))
            return;
        if (void* walkerInterface = BuddyWalkerInterfaceSEH(action))
            PlaceBattleGearMainGun(walkerInterface, matrix);
    }
}
