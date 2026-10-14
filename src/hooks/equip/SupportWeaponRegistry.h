#pragma once

#include <cstdint>

namespace SupportWeapon
{
    constexpr std::uint32_t kEquipIdCount = 0x10000;   // real ids, wide included
    constexpr int kNoBlastId = -1;

    bool IsVanillaSupportWeaponEquipId(std::uint32_t equipId);

    void Set(std::uint32_t equipId, std::uint32_t throwAs, int attackId, int blastId);

    std::uint32_t FamilyFrom(std::uint32_t equipId);
    int  AttackId(std::uint32_t equipId);
    int  BlastId(std::uint32_t equipId);
    bool NativeAction(std::uint32_t equipId);

    std::uint32_t RunningItem(std::uint32_t equipId);
    void SetRunningItem(std::uint32_t equipId);

    void NoteDevelopRow(std::uint32_t equipId, std::uint32_t developId, std::uint32_t baseDevelopId);
    std::uint32_t PickUpLine(std::uint32_t equipId);
}
