#include <pch.h>

#include "BattleGearSound.h"

#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <initializer_list>

#include "AddressSet.h"
#include "BattleGearWeapons.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    using CallSound_t  = bool(__fastcall*)(void* controller, std::uint32_t index, std::uint32_t* handle, std::uint32_t event,
                                           const float* position, std::uint32_t flags);
    using StopSound_t  = void(__fastcall*)(void* sound, std::uint32_t index, std::uint32_t handle);
    using GetGunInfo_t = std::uint8_t*(__fastcall*)(void* source, std::uint32_t gunInfoIndex);
    using StartLoop_t  = void(__fastcall*)(void* sound, std::uint32_t index, std::uint32_t* handle, std::uint32_t event,
                                           const float* position);
    using MoveLoop_t   = void(__fastcall*)(void* sound, std::uint32_t index, std::uint32_t handle, const float* position);
    using OneShot_t    = void(__fastcall*)(void* sound, std::uint32_t index, std::uint32_t event, const float* position);

    // what the walker's stun gun state posts (State_FireStunGun)
    constexpr std::uint32_t kStunChargeStart     = 0xA7E07F9E;
    constexpr std::uint32_t kStunChargeStages[3] = { 0xA7E07F9D, 0xA7E07F9C, 0xA7E07F9B };
    constexpr std::uint32_t kStunChargeFull      = 0x539F6BDD;
    constexpr std::uint32_t kStunFire            = 0x97FEC96F;
    // the Battle Gear's own bank, se_e_btlg: its rail-gun charge whine (3.7 s), shot and hit, and three loop events, each
    // starting two loops and stopping the others': charging (stops nothing: the first one posted), full charge held (stops the
    // charging loops), and the charging tones winding down after the shot (stops the held loop)
    constexpr std::uint32_t kRailCharge          = 0x4E9523C3;
    constexpr std::uint32_t kRailFire            = 0xEAA89C62;
    constexpr std::uint32_t kRailImpact          = 0x5DA0B062;
    constexpr std::uint32_t kRailHum             = 0xA438C8EA;
    constexpr std::uint32_t kRailHeld            = 0x45B5A695;
    constexpr std::uint32_t kRailDown            = 0xF799CBCD;
    constexpr float         kRailDownTime        = 3.0f;         // seconds the wind-down plays before its loops are stopped
    constexpr std::uint32_t kCupolaGunEmpty      = 0x43865F5E;   // what the cupola gun posts for a trigger pulled empty
    constexpr std::uint32_t kCupolaGunFire       = 0xA21EC67A;   // sfx_w_e_hw01_active, what the tanks' cupola gun posts for each round
    constexpr int           kWalkerZeev          = 13;           // EQP_WP_West_sm_010_WG, the walker sidearm that carries the roof MG

    constexpr std::size_t   kAttackSystem        = 0x70;
    constexpr std::size_t   kSystemSound         = 0x68;    // the walkers' sound interface
    constexpr std::size_t   kStopSound           = 0x60 / sizeof(void*);
    constexpr std::size_t   kStartLoop           = 0x28 / sizeof(void*);
    constexpr std::size_t   kOneShot             = 0x30 / sizeof(void*);
    constexpr std::size_t   kMoveLoop            = 0x78 / sizeof(void*);
    constexpr std::uint32_t kMountTurn           = 0x26483886;   // sfx_w_hw01_move_start, the cupola gun's traverse
    constexpr std::uint32_t kMountStop           = 0xF27B4B0D;   // sfx_w_hw01_move_end
    constexpr float         kMountQuiet          = 0.15f;        // seconds still before the traverse sound ends
    constexpr std::size_t   kEquipRecordOwner    = 0x58;    // RealizedEquipObjectImpl
    constexpr std::size_t   kOwnerRecords        = 0x28;
    constexpr std::size_t   kRecordSize          = 0xE;
    constexpr std::size_t   kRecordGunInfoIndex  = 0x8;
    constexpr std::size_t   kEquipGunInfoSource  = 0x68;

    const std::uint8_t kCallSoundEntry[] = { 0x40, 0x53, 0x56, 0x57, 0x48, 0x81, 0xEC, 0x80, 0x00, 0x00, 0x00, 0x48, 0xC7, 0x44, 0x24, 0x48,
                                             0xFE, 0xFF, 0xFF, 0xFF };

    CallSound_t   g_Orig = nullptr;
    void*         g_Target = nullptr;
    std::atomic<DWORD> g_RailThread{ 0 }; // the thread running the Battle Gear's stun gun state right now
    // what the stun gun state asked for; the walker frame plays it through the walker sound interface, at the rail gun's muzzle
    constexpr std::uint32_t kAskCharge = 1;
    constexpr std::uint32_t kAskHeld   = 2;
    constexpr std::uint32_t kAskFire   = 4;
    constexpr std::uint32_t kAskStop   = 8;
    constexpr std::uint32_t kAskImpact = 16;
    std::atomic<std::uint32_t> g_Asked{ 0 };
    SRWLOCK       g_ImpactLock = SRWLOCK_INIT;
    float         g_ImpactAt[3] = {};
    // only the walker frame touches these
    bool          g_Charging = false;
    std::uint32_t g_ChargeHandle = 0;
    std::uint32_t g_HumHandle = 0;
    std::uint32_t g_HeldHandle = 0;
    std::uint32_t g_DownHandle = 0;
    float         g_DownLeft = 0.0f;

    struct Loops   // only the Battle Gear's walker frame touches it, and Drop once that frame no longer runs
    {
        void*         system = nullptr;
        void*         sound = nullptr;
        std::uint32_t index = 0;
        bool          mountTurning = false;
        std::uint32_t mountHandle = 0;
        float         mountStill = 0.0f;
    };

    SRWLOCK g_LoopsLock = SRWLOCK_INIT;
    Loops   g_Loops{};

#ifdef _DEBUG
    void LogRailSound(const char* what, std::uint32_t event, std::uint32_t handle, const float* place)   // TEMP probe
    {
        LogDebug("BattleGear sound: rail %s 0x%08X, handle 0x%X, at (%.1f %.1f %.1f), from the walker frame", what, event, handle,
                 place[0], place[1], place[2]);
    }
#endif

    bool IsStunChargeStage(std::uint32_t event)
    {
        for (const std::uint32_t stage : kStunChargeStages)
            if (event == stage)
                return true;
        return false;
    }

    bool __fastcall hkCallSound(void* controller, std::uint32_t index, std::uint32_t* handle, std::uint32_t event, const float* position,
                                std::uint32_t flags)
    {
        if (g_RailThread.load(std::memory_order_relaxed) != GetCurrentThreadId())
            return g_Orig(controller, index, handle, event, position, flags);
        // the stun gun's own sounds stay silent; the walker frame plays the Battle Gear's (posted from inside this state the
        // events started and were not heard; posted from the walker frame through its sound interface they were)
        if (handle)
            *handle = 0;
        if (event == kStunChargeStart)
        {
            g_Asked.fetch_and(~(kAskStop | kAskHeld | kAskFire), std::memory_order_relaxed);
            g_Asked.fetch_or(kAskCharge, std::memory_order_relaxed);
            return true;
        }
        if (event == kStunChargeFull)
        {
            g_Asked.fetch_or(kAskHeld, std::memory_order_relaxed);
            return true;
        }
        if (event == kStunFire)
        {
            g_Asked.fetch_or(kAskFire, std::memory_order_relaxed);
            return true;
        }
        if (IsStunChargeStage(event))
            return true;
        return g_Orig(controller, index, handle, event, position, flags);
    }

    void* SoundOfAttackSEH(const void* attackAction)
    {
        __try
        {
            const auto* system = *reinterpret_cast<const std::uint8_t* const*>(static_cast<const std::uint8_t*>(attackAction) + kAttackSystem);
            return system ? *reinterpret_cast<void* const*>(system + kSystemSound) : nullptr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    std::uint32_t* ShotEventsSEH(void* realizedEquipObject, std::uint32_t recordIndex)
    {
        __try
        {
            const auto* object = static_cast<const std::uint8_t*>(realizedEquipObject);
            const auto* owner = *reinterpret_cast<const std::uint8_t* const*>(object + kEquipRecordOwner);
            const auto* records = owner ? *reinterpret_cast<const std::uint8_t* const*>(owner + kOwnerRecords) : nullptr;
            void* source = *reinterpret_cast<void* const*>(object + kEquipGunInfoSource);
            if (!records || !source)
                return nullptr;
            const std::uint8_t gunInfoIndex = records[static_cast<std::size_t>(recordIndex) * kRecordSize + kRecordGunInfoIndex];
            return reinterpret_cast<std::uint32_t*>(reinterpret_cast<GetGunInfo_t>((*static_cast<void***>(source))[1])(source, gunInfoIndex));
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    bool SwapShotEventsSEH(battleGearSound::SidearmShot& shot)
    {
        __try
        {
            for (int i = 0; i < 4; ++i)
            {
                shot.vanilla[i] = shot.events[i];
                shot.events[i] = kCupolaGunFire;
            }
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void PutBackShotEventsSEH(const battleGearSound::SidearmShot& shot)
    {
        __try
        {
            for (int i = 0; i < 4; ++i)
                shot.events[i] = shot.vanilla[i];
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    bool StartLoopSEH(const Loops& l, std::uint32_t event, const float* position, std::uint32_t& handle)
    {
        __try
        {
            handle = 0;
            reinterpret_cast<StartLoop_t>((*static_cast<void***>(l.sound))[kStartLoop])(l.sound, l.index, &handle, event, position);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void MoveLoopSEH(const Loops& l, std::uint32_t handle, const float* position)
    {
        __try
        {
            if (handle)
                reinterpret_cast<MoveLoop_t>((*static_cast<void***>(l.sound))[kMoveLoop])(l.sound, l.index, handle, position);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    void StopLoopSEH(const Loops& l, std::uint32_t handle)
    {
        __try
        {
            if (handle)
                reinterpret_cast<StopSound_t>((*static_cast<void***>(l.sound))[kStopSound])(l.sound, l.index, handle);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    void OneShotSEH(const Loops& l, std::uint32_t event, const float* position)
    {
        __try
        {
            reinterpret_cast<OneShot_t>((*static_cast<void***>(l.sound))[kOneShot])(l.sound, l.index, event, position);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    void* WalkerSoundSEH(void* walkerSystem)
    {
        __try
        {
            return *reinterpret_cast<void* const*>(static_cast<const std::uint8_t*>(walkerSystem) + kSystemSound);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    std::uint32_t StartRail(const Loops& l, std::uint32_t event, const float* place)
    {
        std::uint32_t handle = 0;
        StartLoopSEH(l, event, place, handle);
#ifdef _DEBUG
        LogRailSound("loop", event, handle, place);
#endif
        return handle;
    }

    void StopRail(const Loops& l, std::uint32_t& handle)
    {
        StopLoopSEH(l, handle);
        handle = 0;
    }

    // what the stun gun state asked for since the last walker frame, played at the rail gun's muzzle
    void PlayRailAsks(const Loops& l, const float* muzzle, float dt)
    {
        const std::uint32_t asked = g_Asked.exchange(0, std::memory_order_relaxed);
        if (asked & (kAskStop | kAskCharge | kAskFire))
        {
            const bool charged = g_Charging;
            g_Charging = false;
            StopRail(l, g_ChargeHandle);
            StopRail(l, g_HumHandle);
            StopRail(l, g_HeldHandle);
            if ((asked & kAskFire) && charged)
            {
                StopRail(l, g_DownHandle);
                g_DownHandle = StartRail(l, kRailDown, muzzle);
                g_DownLeft = kRailDownTime;
            }
        }
        if (asked & kAskCharge)
        {
            StopRail(l, g_DownHandle);
            g_DownLeft = 0.0f;
            g_ChargeHandle = StartRail(l, kRailCharge, muzzle);
            g_HumHandle = StartRail(l, kRailHum, muzzle);
            g_Charging = true;
        }
        if ((asked & kAskHeld) && g_Charging && !g_HeldHandle)
            g_HeldHandle = StartRail(l, kRailHeld, muzzle);
        if (asked & kAskFire)
        {
            OneShotSEH(l, kRailFire, muzzle);
#ifdef _DEBUG
            LogRailSound("shot", kRailFire, 0, muzzle);
#endif
        }
        if (asked & kAskImpact)
        {
            alignas(16) float at[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
            AcquireSRWLockShared(&g_ImpactLock);
            at[0] = g_ImpactAt[0];
            at[1] = g_ImpactAt[1];
            at[2] = g_ImpactAt[2];
            ReleaseSRWLockShared(&g_ImpactLock);
            OneShotSEH(l, kRailImpact, at);
#ifdef _DEBUG
            LogRailSound("hit", kRailImpact, 0, at);
#endif
        }
        for (const std::uint32_t handle : { g_ChargeHandle, g_HumHandle, g_HeldHandle, g_DownHandle })
            MoveLoopSEH(l, handle, muzzle);
        if (g_DownLeft > 0.0f)
        {
            g_DownLeft -= dt;
            if (g_DownLeft <= 0.0f)
                StopRail(l, g_DownHandle);
        }
    }
}

namespace battleGearSound
{
    void BeforeSidearmShot(void* realizedEquipObject, std::uint32_t recordIndex, int equipId, SidearmShot& shot)
    {
        if (equipId != kWalkerZeev || !realizedEquipObject || !battleGearWeapons::LoadoutApplies())
            return;
        shot.events = ShotEventsSEH(realizedEquipObject, recordIndex);
        if (shot.events && !SwapShotEventsSEH(shot))
            shot.events = nullptr;
    }

    void AfterSidearmShot(const SidearmShot& shot)
    {
        if (shot.events)
            PutBackShotEventsSEH(shot);
    }

    void EnterRailState(const void* attackAction)
    {
        if (!g_Orig)
            return;
        (void)attackAction;
        g_RailThread.store(GetCurrentThreadId(), std::memory_order_relaxed);
    }

    void LeaveRailState(bool stateEnded)
    {
        g_RailThread.store(0, std::memory_order_relaxed);
        if (stateEnded)
            g_Asked.fetch_or(kAskStop, std::memory_order_relaxed);   // let go before the first level: no shot ends the charge
    }

    void OnWalkerFrame(void* walkerSystem, std::uint32_t index, float dt)
    {
        alignas(16) float gun[16];
        alignas(16) float at[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
        const bool gunKnown = battleGearWeapons::RoofGun(gun, nullptr);
        if (gunKnown)
        {
            at[0] = gun[12];
            at[1] = gun[13];
            at[2] = gun[14];
        }
        else if (!battleGearWeapons::RailGunMuzzle(at))
        {
            return;   // not posed on the field yet: nowhere to play from
        }
        AcquireSRWLockExclusive(&g_LoopsLock);
        Loops& l = g_Loops;
        void* sound = WalkerSoundSEH(walkerSystem);
        if (l.system != walkerSystem || l.sound != sound || l.index != index)
            l = { walkerSystem, sound, index };
        if (!l.sound)
        {
            ReleaseSRWLockExclusive(&g_LoopsLock);
            return;
        }
        alignas(16) float muzzle[4] = { at[0], at[1], at[2], 1.0f };
        battleGearWeapons::RailGunMuzzle(muzzle);
        PlayRailAsks(l, muzzle, dt);

        const bool turning = gunKnown && battleGearWeapons::RoofGunTurning();
        l.mountStill = turning ? 0.0f : l.mountStill + dt;
        if (turning && !l.mountTurning)
        {
            l.mountTurning = StartLoopSEH(l, kMountTurn, at, l.mountHandle);
        }
        else if (l.mountTurning && l.mountStill >= kMountQuiet)
        {
            StopLoopSEH(l, l.mountHandle);
            OneShotSEH(l, kMountStop, at);
            l.mountTurning = false;
            l.mountHandle = 0;
        }
        if (l.mountTurning)
            MoveLoopSEH(l, l.mountHandle, at);
        ReleaseSRWLockExclusive(&g_LoopsLock);
    }

    void Drop()
    {
        AcquireSRWLockExclusive(&g_LoopsLock);
        Loops& l = g_Loops;
        if (!l.system)
        {
            ReleaseSRWLockExclusive(&g_LoopsLock);
            return;
        }
        if (l.sound && l.sound == WalkerSoundSEH(l.system))   // the walker system still stands: its sound interface does too
        {
            StopLoopSEH(l, l.mountHandle);
            for (const std::uint32_t handle : { g_ChargeHandle, g_HumHandle, g_HeldHandle, g_DownHandle })
                StopLoopSEH(l, handle);
        }
        l = {};
        g_ChargeHandle = g_HumHandle = g_HeldHandle = g_DownHandle = 0;
        g_Charging = false;
        g_DownLeft = 0.0f;
        g_Asked.store(0, std::memory_order_relaxed);
        ReleaseSRWLockExclusive(&g_LoopsLock);
    }

    void RoofGunShot(void* walkerSystem, std::uint32_t index, const float* position)
    {
        alignas(16) const float at[4] = { position[0], position[1], position[2], 1.0f };
        const Loops walker{ walkerSystem, WalkerSoundSEH(walkerSystem), index };
        if (walker.sound)
            OneShotSEH(walker, kCupolaGunFire, at);
    }

    void RoofGunEmpty(void* walkerSystem, std::uint32_t index, const float* position)
    {
        alignas(16) const float at[4] = { position[0], position[1], position[2], 1.0f };
        const Loops walker{ walkerSystem, WalkerSoundSEH(walkerSystem), index };
        if (walker.sound)
            OneShotSEH(walker, kCupolaGunEmpty, at);
    }

    void RailImpact(const float* position)
    {
        if (!g_Orig || !position)
            return;
        AcquireSRWLockExclusive(&g_ImpactLock);
        g_ImpactAt[0] = position[0];
        g_ImpactAt[1] = position[1];
        g_ImpactAt[2] = position[2];
        ReleaseSRWLockExclusive(&g_ImpactLock);
        g_Asked.fetch_or(kAskImpact, std::memory_order_relaxed);
    }

    bool Install()
    {
        if (g_Target)
            return true;
        void* target = gAddr.GmSoundControllerImpl_CallSound ? ResolveGameAddress(gAddr.GmSoundControllerImpl_CallSound) : nullptr;
        if (!target)
        {
            LogError("BattleGear: sound controller address missing on this build, the rail gun sounds like the D-Walker's stun gun");
            return false;
        }
        const HookEntry state = CheckHookEntry(target, kCallSoundEntry, sizeof(kCallSoundEntry));
        if (state == HookEntry::Unknown)
        {
            char desc[160] = {};
            DescribeFunctionEntry(target, desc, sizeof(desc));
            LogError("BattleGear: sound controller code differs on this build (%s), the rail gun sounds like the D-Walker's stun gun", desc);
            return false;
        }
        if (state == HookEntry::ForeignHook)
            LogHookedOnTop("BattleGear: SoundControllerImpl CallSound", target);
        if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hkCallSound), reinterpret_cast<void**>(&g_Orig)))
        {
            LogError("BattleGear: sound controller hook failed, the rail gun sounds like the D-Walker's stun gun");
            return false;
        }
        g_Target = target;
        return true;
    }

    void Uninstall()
    {
        g_RailThread.store(0, std::memory_order_relaxed);
        Drop();
        if (!g_Target)
            return;
        DisableAndRemoveHook(g_Target);
        g_Target = nullptr;
        g_Orig = nullptr;
    }
}
