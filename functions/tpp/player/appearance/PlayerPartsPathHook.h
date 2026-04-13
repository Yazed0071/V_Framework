#pragma once

#include <cstdint>

bool Install_PlayerPartsPath_Hook();
bool Uninstall_PlayerPartsPath_Hook();

#pragma pack(push, 1)
struct LoadPartsPlayerInfo
{
    std::uint8_t  playerType;         // 0x00
    std::uint8_t  playerPartsType;    // 0x01
    std::uint8_t  playerCamoType;     // 0x02
    std::uint8_t  playerArmType;      // 0x03
    std::uint16_t playerFaceId;       // 0x04
    std::uint8_t  playerFaceEquipId;  // 0x06
    std::uint8_t  playerFaceEquipUnk; // 0x07
    std::uint8_t  rest[0x47];         // 0x08..0x4E
};
#pragma pack(pop)

static_assert(sizeof(LoadPartsPlayerInfo) == 0x4F, "LoadPartsPlayerInfo size mismatch");