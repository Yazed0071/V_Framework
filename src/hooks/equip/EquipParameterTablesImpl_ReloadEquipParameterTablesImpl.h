#pragma once

#include <cstdint>

bool Install_EquipParameterTablesImpl_ReloadEquipParameterTablesImpl_Hook();
void Uninstall_EquipParameterTablesImpl_ReloadEquipParameterTablesImpl_Hook();

namespace EquipParameterTables
{
    constexpr std::uint32_t kSupportWeaponRowCount = 1024;
    constexpr std::uint32_t kBlastRowCount         = 256;
    constexpr std::uint32_t kFirstCustomSupportWeaponId = 91;
    constexpr std::uint32_t kFirstCustomBlastId         = 49;

    void SetSupportWeaponRow(std::uint32_t swpId, int ammo, float p1, float p2, int grade);
    void SetBlastRow(std::uint32_t blastId, int blastFlag, float effectRange, float optimalRange);

    const void* GetBlastRow(std::uint32_t blastId);

    bool SupportWeaponRowsAvailable();
    bool BlastRowsAvailable();

    constexpr int kAutomaticDamageType = -1;
    void SetSupportWeaponDamageType(std::uint32_t swpId, int damageType);
    int  SupportWeaponDamageType(std::uint32_t swpId);

    constexpr float kBarAutomatic = -1.0f;
    struct SupportWeaponBars
    {
        float damage        = kBarAutomatic;   // 0 hides, 0-1 fill
        float durability    = kBarAutomatic;
        float effectTime    = kBarAutomatic;
        float durabilityMax = 0.0f;            // > 0: durability = p1 / durabilityMax
        float effectTimeMax = 0.0f;            // > 0: effect time = p2 / effectTimeMax
    };
    void SetSupportWeaponBars(std::uint32_t swpId, const SupportWeaponBars& bars);
    bool GetSupportWeaponBars(std::uint32_t swpId, SupportWeaponBars& bars);
    bool SupportWeaponRowValues(std::uint32_t swpId, float& p1, float& p2);
}
