#pragma once

#include <cstdint>
#include <vector>

namespace equip
{
    constexpr std::uint32_t kChargeShotDefaultSound  = 0xFFFFFFFFu;
    constexpr std::uint64_t kChargeShotDefaultEffect = ~0ull;
    constexpr std::uint32_t kChargeShotNoVariation   = 0xBF169F98u; // StrCode32 of "": the effect's own variation

    struct ChargeShotLevel
    {
        float time = 0.0f;
        int ammo = 1;
        int bulletId = 0;
        int shots = 1;
        std::uint32_t sound = kChargeShotDefaultSound;
        std::uint32_t fireSound = kChargeShotDefaultSound;
        std::uint64_t effect = kChargeShotDefaultEffect;
        std::uint64_t impactEffect = 0;
        std::uint32_t effectVariation = kChargeShotNoVariation;
        std::uint32_t impactVariation = kChargeShotNoVariation;
    };

    struct ChargeShotStyle
    {
        std::uint32_t chargeSound = kChargeShotDefaultSound;
        std::uint32_t fullSound = kChargeShotDefaultSound;
        std::uint64_t chargeEffect = 0;
        bool hud = true;
    };

    struct ChargeShotFire
    {
        int level = 0;
        int ammo = 1;
        int magazineBefore = 0;
        bool infiniteAmmo = false;
    };

    struct ChargeShotDoFire
    {
        int slot = -1;
        int runs = 1;
        bool swapped = false;
        bool secondary = false;
        std::uint8_t bulletId = 0;
        std::uint8_t bulletType = 0;
    };

    void ChargeShot_Register(int equipId, int receiverId, float cooldown, const ChargeShotStyle& style,
                             const std::vector<ChargeShotLevel>& levels);

    bool ChargeShot_BeginFire(std::uint8_t* work, std::uint32_t slot, ChargeShotFire* out);
    void ChargeShot_EndFire(void* attackAction, std::uint8_t* work, std::uint32_t slot,
                            const ChargeShotFire& shot);

    void ChargeShot_BeforeDoFire(std::uint8_t* gunRecord, bool secondary, void* request, ChargeShotDoFire* out);
    void ChargeShot_AfterDoFire(std::uint8_t* gunRecord, const ChargeShotDoFire& state);

    int ChargeShot_RealEquipId(int engineEquipId);
    bool ChargeShot_HudCharge(int* equipId, int* value, int* max);
    int ChargeShot_HudLevels(int equipId); // 0 when the weapon has no SetChargeShot or hud = false

    void ChargeShot_ClearForNewMission();
}

bool Install_AttackActionImpl_UpdateShootingByPad_Hook();
void Uninstall_AttackActionImpl_UpdateShootingByPad_Hook();
