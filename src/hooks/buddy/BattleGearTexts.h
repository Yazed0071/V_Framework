#pragma once

#include <cstdint>

// The Battle Gear's names, descriptions and icons on its Customize page and the HUD. The game names its guns and has no description
// or icon for them; the rest are V Framework's own lang entries and textures.
namespace battleGearTexts
{
    constexpr std::uint64_t kBodyName     = 0x2C7295E9E90B;   // V_name_bg_body
    constexpr std::uint64_t kBodyInfo     = 0xC0BDB447034C;   // V_info_bg_body
    constexpr std::uint64_t kRailGunName  = 0x5F4DA30DD7DF;   // name_bg_41000
    constexpr std::uint64_t kRailGunInfo  = 0xD7A494AB7C6E;   // V_info_bg_41000
    constexpr std::uint64_t kHMissileName = 0x4DDBD7A1AF38;   // name_bg_42000
    constexpr std::uint64_t kHMissileInfo = 0x9DFB0A41B90D;   // V_info_bg_42000
    constexpr std::uint64_t kRoofMgName   = 0x881ECAB848D6;   // V_name_bg_roof_mg
    constexpr std::uint64_t kRoofMgInfo   = 0x7F33AE189289;   // V_info_bg_roof_mg

    // icon textures: the path without its extension, as R&D records carry it
    constexpr std::uint64_t kBodyIcon     = 0x21325019B85B5;   // /Assets/tpp/ui/texture/EquipIcon/mecha/ui_bg_btg1_body0_alp
    constexpr std::uint64_t kRailGunIcon  = 0x340D678B65D77;   // /Assets/tpp/ui/texture/EquipIcon/mecha/ui_bg_btg1_rgun0_alp
    constexpr std::uint64_t kHMissileIcon = 0x11D1D0154DB00;   // /Assets/tpp/ui/texture/EquipIcon/mecha/ui_bg_btg1_hmsl0_alp
    constexpr std::uint64_t kRoofMgIcon   = 0x175CE705B6BEB;   // /Assets/tpp/ui/texture/EquipIcon/mecha/ui_bg_btg1_rfmg0_alp

    constexpr std::uint64_t kLangMask      = 0xFFFFFFFFFFFF;
    constexpr std::uint64_t kPathStemMask  = 0x3FFFFFFFFFFFF;   // a path code: the path without its extension,
    constexpr std::uint64_t kFtexExtension = 0x2ADull << 51;    // then the extension: "ftex", as the HUD's icon ids carry it
}
