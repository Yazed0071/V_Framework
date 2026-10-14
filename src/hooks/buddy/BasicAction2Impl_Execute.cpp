#include "pch.h"

#include "BasicAction2Impl_Execute.h"

#include <Windows.h>
#include <cstdint>

#include "AddressSet.h"
#include "BuddyOutfitRegistry.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    using Execute_t            = void(__fastcall*)(void* self, std::int32_t phase, std::uint32_t index);
    using SetMotionSpeedRate_t = void(__fastcall*)(void* controller, std::uint32_t index, std::uint32_t layer, float rate);

    Execute_t g_Orig   = nullptr;
    void*     g_Target = nullptr;

    constexpr std::size_t kIface_AnimationController = 0x60;
    constexpr std::size_t kIface_FirstIndex          = 0x78;
    constexpr std::size_t kIface_Works               = 0xA0;
    constexpr std::size_t kWorkStride                = 0x440;
    constexpr std::size_t kWork_GallopAccel          = 0x1A0; // grows with the dash taps
    constexpr std::size_t kWork_State                = 0x260;
    constexpr std::size_t kWork_Horse                = 0x268;
    constexpr std::size_t kHorse_Tack                = 0x2D4;
    constexpr std::size_t kSlot_SetMotionSpeedRate   = 0x280 / sizeof(void*);

    bool IsGallopState(std::uint8_t state) // BasicAction2Impl::s_stateFuncs ids
    {
        switch (state)
        {
        case 15:            // RideOnIdleToGallop
        case 19:            // Gallop
        case 20: case 21:   // GallopChangeLegToReverse, GallopChangeLegToNormal
        case 25:            // GallopToIdle
        case 29: case 30:   // TrotToGallop, GallopToTrot
        case 31: case 32:   // CanterToGallop, GallopToCanter
        case 33:            // GallopCanter
        case 35: case 36: case 37: // GallopJump, GallopJumpLanding, GallopJumpEnd
        case 39:            // GallopAvoid
        case 47: case 48:   // GallopAccelSuccess, GallopAccelFailure
            return true;
        default:
            return false;
        }
    }

    bool ScalesWithDashTaps(std::uint8_t state)
    {
        return state == 19 || state == 20 || state == 21 || state == 35 || state == 36 || state == 37;
    }

    void ApplyGallopRateSEH(void* self, std::uint32_t index)
    {
        __try
        {
            auto* iface = static_cast<std::uint8_t*>(self);
            const std::uint32_t first = *reinterpret_cast<const std::uint32_t*>(iface + kIface_FirstIndex);
            auto* works = *reinterpret_cast<std::uint8_t**>(iface + kIface_Works);
            if (!works || index < first)
                return;
            const std::uint8_t* work = works + static_cast<std::size_t>(index - first) * kWorkStride;

            const std::uint8_t state = work[kWork_State];
            if (!IsGallopState(state))
                return;
            const auto* horse = *reinterpret_cast<const std::uint8_t* const*>(work + kWork_Horse);
            if (!horse)
                return;
            const std::uint32_t tack = *reinterpret_cast<const std::uint32_t*>(horse + kHorse_Tack);
            if (tack < buddyoutfit::kHorseVanillaTackCount || tack > 0xFF)
                return;
            const float rate = buddyoutfit::HorseGallopRates()[tack];
            if (rate == 1.0f)
                return;

            float speed = 1.0f;
            if (ScalesWithDashTaps(state))
            {
                const float accel = *reinterpret_cast<const float*>(work + kWork_GallopAccel);
                if (accel > 4.0f)
                    speed = (accel - 4.0f) * 0.25f * 0.2f + 1.0f; // UpdateGallopMotionSpeed's own curve
            }

            void* controller = *reinterpret_cast<void**>(iface + kIface_AnimationController);
            if (!controller)
                return;
            const auto setRate = reinterpret_cast<SetMotionSpeedRate_t>(
                (*reinterpret_cast<void***>(controller))[kSlot_SetMotionSpeedRate]);
            setRate(controller, index, 0, speed * rate);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    void __fastcall hkExecute(void* self, std::int32_t phase, std::uint32_t index)
    {
        g_Orig(self, phase, index);
        ApplyGallopRateSEH(self, index); // the dash and stop states never set a speed, they ride on the last one
    }
}

bool Install_BasicAction2Impl_Execute_Hook()
{
    if (g_Target)
        return true;
    void* target = ResolveGameAddress(gAddr.HorseBasicAction2Impl_Execute);
    if (!target || !CreateAndEnableHook(target, reinterpret_cast<void*>(&hkExecute),
                                        reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("BuddyOutfit: D-Horse action hook failed or address missing, custom tack gallop speed only holds in a plain gallop");
        return false;
    }
    g_Target = target;
    return true;
}

bool Uninstall_BasicAction2Impl_Execute_Hook()
{
    if (!g_Target)
        return true;
    DisableAndRemoveHook(g_Target);
    g_Target = nullptr;
    g_Orig = nullptr;
    return true;
}
