#pragma once

#include <cstdint>

enum class DestroyedEquipment : std::uint8_t
{
    Other,
    Comms,         // an antenna or comms gear: the guard who hears it radios CPR0346 (user)
    PowerSupply,   // a generator: CPR0345 (user)
};

DestroyedEquipment TppPermanentGimmickUnit_DestroyedAt(const float spot[3]);   // what the player just destroyed where the noise a guard reports was
void               TppPermanentGimmickUnit_ClearForReload();
bool Install_TppPermanentGimmickUnit_CallNoise_Hook();
bool Uninstall_TppPermanentGimmickUnit_CallNoise_Hook();
