#pragma once

#include <cstdint>

namespace buddyoutfit
{
    constexpr int kQuietMotionSlots = 8;

    struct QuietWeaponMotion
    {
        std::uint16_t equipId = 0;
        std::uint64_t mtar = 0;
        std::uint64_t clips[kQuietMotionSlots] = {};
        std::uint64_t partClips[2] = {};
    };

    void RegisterQuietWeaponMotion(const QuietWeaponMotion& motion);
}

void* QuietWeaponMotion_GetAnimFile(void* mtar, unsigned long long clip, void* origGetAnimFile);

bool Install_EquipControllerImpl_InitCurrentSlot_Hooks();
bool Uninstall_EquipControllerImpl_InitCurrentSlot_Hooks();
