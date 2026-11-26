#include <pch.h>
#include "ThrowingImpl_CalcInitialVelocityByAngle.h"

#include <cmath>
#include <cstring>

#include "AddressSet.h"
#include "HookUtils.h"
#include "SupportWeaponRegistry.h"
#include "log.h"

namespace
{
    using CalcInitialVelocityByAngle_t = bool(__fastcall*)(void* impl, const std::uint8_t* desc, const float* target, float angle,
                                                           float* velocity);

    constexpr std::size_t   kDescTarget           = 0x20;   // w = launch angle (mode 1) or flight seconds (mode 2)
    constexpr std::size_t   kDescEquipId          = 0x3C;   // low 11 bits, bits 11-12 the launch mode
    constexpr std::uint32_t kLaunchModeShift      = 11;
    constexpr std::uint32_t kLaunchModeMask       = 3;
    constexpr std::uint32_t kLaunchByAngle        = 1;
    constexpr std::uint32_t kLaunchByTime         = 2;
    constexpr float         kLeastGravityForAngle = 0.01f;

    const std::uint8_t kEntry[] = {
        0x48, 0x8B, 0xC4, 0x53, 0x48, 0x81, 0xEC, 0xD0, 0x00, 0x00, 0x00, 0x0F, 0x29, 0x70, 0xE8, 0x44,
        0x0F, 0x29, 0x40, 0xC8, 0x44, 0x0F, 0x29, 0x68, 0x88, 0x44, 0x0F, 0x29, 0x74, 0x24, 0x50,
    };

    CalcInitialVelocityByAngle_t FindCalcByAngle()
    {
        void* target = ResolveGameAddress(gAddr.ThrowingImpl_CalcInitialVelocityByAngle);
        if (target && CheckHookEntry(target, kEntry, sizeof(kEntry)) != HookEntry::Unknown)
            return reinterpret_cast<CalcInitialVelocityByAngle_t>(target);
        LogError("SupportWeapon: ThrowingImpl::CalcInitialVelocityByAngle not found on this build, "
                 "angled throws with their own gravity use the game's flight-time arc");
        return nullptr;
    }

    void VelocityByTime(const float* position, const float* target, float seconds, float gravity, float* velocity)
    {
        for (int lane = 0; lane < 4; ++lane)
            velocity[lane] = (target[lane] - position[lane]) / seconds;
        velocity[1] += 0.5f * gravity * seconds;
    }
}

bool ThrowingImpl_AimedThrowVelocity(void* impl, const std::uint8_t* desc, float gravity, float fallbackSeconds, float* velocity)
{
    const auto* position = reinterpret_cast<const float*>(desc);
    const auto* target   = reinterpret_cast<const float*>(desc + kDescTarget);
    const std::uint32_t mode = (*reinterpret_cast<const std::uint16_t*>(desc + kDescEquipId) >> kLaunchModeShift) & kLaunchModeMask;
    if (mode == kLaunchByTime)
    {
        if (target[3] == 0.0f)
            return false;
        VelocityByTime(position, target, target[3], gravity, velocity);
        return true;
    }
    if (mode != kLaunchByAngle)
        return false;

    static const CalcInitialVelocityByAngle_t calcByAngle = FindCalcByAngle();
    alignas(16) float aim[4];
    alignas(16) float solved[4] = {};
    std::memcpy(aim, target, sizeof(aim));
    if (calcByAngle && gravity >= kLeastGravityForAngle && calcByAngle(impl, desc, aim, target[3], solved))
    {
        const float scale = std::sqrt(gravity / SupportWeapon::kGameGravity);   // same launch angle: speed grows with the root of gravity
        for (int lane = 0; lane < 4; ++lane)
            velocity[lane] = solved[lane] * scale;
        return true;
    }
    VelocityByTime(position, target, fallbackSeconds, gravity, velocity);
    return true;
}
