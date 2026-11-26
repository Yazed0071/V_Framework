#include <pch.h>

#include "BasicAction2Impl_Execute.h"

#include <Windows.h>
#include <atomic>
#include <cstdint>

#include "AddressSet.h"
#include "BuddyOutfitRegistry.h"
#include "HookUtils.h"
#include "log.h"
#include "../player/Player2Impl_CallVoice.h"
#include "../player/RideHorseActionPluginImpl_State_Idle.h"
#include "../sound/AkSoundEngine_PostEvent.h"

namespace
{
    using Execute_t            = void(__fastcall*)(void* self, std::int32_t phase, std::uint32_t index);
    using SetMotionSpeedRate_t = void(__fastcall*)(void* controller, std::uint32_t index, std::uint32_t layer, float rate);
    using StateProc_t          = void(__fastcall*)(void* object, std::uint32_t index, std::uint32_t proc, void* param);

    const std::uint8_t kStateRideOnIdleEntry[] = { 0x40, 0x53, 0x56, 0x57, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x38, 0x48, 0x8B, 0xF9 };

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
    constexpr std::uint8_t kRideOnIdle               = 6;   // standing still with a rider; the spur slaps happen in the moving states
    constexpr std::uint8_t kEjectDungStart           = 49;  // the unridden idles start one too, on Snake's order from any distance
    constexpr std::uint8_t kEjectDungEnd             = 51;  // the dung has dropped
    constexpr std::size_t kIface_Object              = 0x20;   // Execute runs on the object's +0x20 interface, the state functions on the object
    constexpr std::size_t kWork_NextState            = 0x261;
    constexpr std::size_t kWork_AfterDung            = 0x431;
    constexpr std::uint32_t kProcEnter               = 2;
    constexpr std::uint32_t kProcLeave               = 3;
    constexpr std::uint32_t kNoHorse                 = 0xFFFFFFFFu;

    std::atomic<std::uint32_t> g_RiddenDungHorse{ kNoHorse };   // the horse whose dung started under Snake

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

    std::uint8_t ReadStateSEH(void* self, std::uint32_t index)
    {
        __try
        {
            auto* iface = static_cast<std::uint8_t*>(self);
            const std::uint32_t first = *reinterpret_cast<const std::uint32_t*>(iface + kIface_FirstIndex);
            auto* works = *reinterpret_cast<std::uint8_t**>(iface + kIface_Works);
            if (!works || index < first)
                return 0;
            return works[static_cast<std::size_t>(index - first) * kWorkStride + kWork_State];
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    std::uint8_t* WorkSEH(void* self, std::uint32_t index)
    {
        __try
        {
            auto* iface = static_cast<std::uint8_t*>(self);
            const std::uint32_t first = *reinterpret_cast<const std::uint32_t*>(iface + kIface_FirstIndex);
            auto* works = *reinterpret_cast<std::uint8_t**>(iface + kIface_Works);
            if (!works || index < first)
                return nullptr;
            return works + static_cast<std::size_t>(index - first) * kWorkStride;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    bool NothingQueuedSEH(const std::uint8_t* work)
    {
        __try
        {
            return work[kWork_NextState] == 0 && work[kWork_AfterDung] != 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void ClearAfterDungSEH(std::uint8_t* work)
    {
        __try
        {
            work[kWork_AfterDung] = 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    StateProc_t RideOnIdleState()
    {
        static StateProc_t s_State = nullptr;
        static bool        s_Checked = false;
        if (!s_Checked)
        {
            s_Checked = true;
            void* target = ResolveGameAddress(gAddr.HorseBasicAction2Impl_State_RideOnIdle);
            if (target && CheckHookEntry(target, kStateRideOnIdleEntry, sizeof(kStateRideOnIdleEntry)) != HookEntry::Unknown)
                s_State = reinterpret_cast<StateProc_t>(target);
            else
                LogError("HorsePat: D-Horse's State_RideOnIdle missing or changed on %s, Reload starts no pat", GetGameBuildName(gGameBuild));
        }
        return s_State;
    }

    // what Execute does when the state changes: leave, then enter; with +0x431 set the entry is the after-dung one, idle mode 2
    void ReenterRideOnIdle(void* self, std::uint32_t index, std::uint8_t* work)
    {
        const StateProc_t state = RideOnIdleState();
        if (!state || !NothingQueuedSEH(work))
        {
            ClearAfterDungSEH(work);
            return;
        }
        void* object = static_cast<std::uint8_t*>(self) - kIface_Object;
        state(object, index, kProcLeave, nullptr);
        state(object, index, kProcEnter, nullptr);
    }

    void __fastcall hkExecute(void* self, std::int32_t phase, std::uint32_t index)
    {
        const std::uint8_t before = phase == 2 ? ReadStateSEH(self, index) : 0;   // phase 2 runs the state switch
        g_Orig(self, phase, index);
        ApplyGallopRateSEH(self, index);
        if (phase == 2)
        {
            const std::uint8_t after = ReadStateSEH(self, index);
            if (after != before)
            {
                SayHorseGaitLine(after);
                if (after == kEjectDungStart)
                    g_RiddenDungHorse.store(before == kRideOnIdle ? index : kNoHorse, std::memory_order_relaxed);
                if (after == kEjectDungEnd)
                {
                    if (g_RiddenDungHorse.exchange(kNoHorse, std::memory_order_relaxed) == index)
                        SayHorseDungLine();
                }
            }
            if (after == kRideOnIdle && TakeHorseSlapSound())
                SayHorsePatLine();   // the slap of Snake's mounted pat idle
            if (std::uint8_t* work = WorkSEH(self, index); work && TakeHorsePatReentry(work))
            {
                if (after == kRideOnIdle)
                    ReenterRideOnIdle(self, index, work);
                else
                {
                    ClearAfterDungSEH(work);
                }
            }
        }
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
