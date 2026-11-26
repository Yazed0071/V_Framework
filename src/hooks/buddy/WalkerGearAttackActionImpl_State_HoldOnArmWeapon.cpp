#include <pch.h>

#include "WalkerGearAttackActionImpl_State_HoldOnArmWeapon.h"

#include <Windows.h>
#include <algorithm>
#include <cstdint>

#include "AddressSet.h"
#include "BattleGearEffects.h"
#include "BattleGearWeapons.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    using State_t = void(__fastcall*)(void* attack, std::uint32_t walker, std::uint32_t proc);

    constexpr std::uint32_t kProcStart      = 1;
    constexpr std::uint32_t kProcUpdate     = 4;
    constexpr std::size_t   kSystem         = 0x70;
    constexpr std::size_t   kWorks          = 0x90;
    constexpr std::size_t   kFirstWalker    = 0x98;
    constexpr std::size_t   kWorkSize       = 0x2D0;
    constexpr std::size_t   kWorkWalkerId   = 0x150;
    constexpr std::size_t   kWorkFireRate   = 0x164;   // shots a second
    constexpr std::size_t   kWorkNextShot   = 0x168;   // seconds until the gun may fire again; it fires only at 0
    constexpr std::size_t   kWorkShots      = 0x1C0;   // +1 each shot
    constexpr std::size_t   kSystemClock    = 0x18;
    constexpr std::size_t   kClockPerWalker = 0x38;    // frame time = this walker's scale * the frame delta
    constexpr std::size_t   kClockFrame     = 0x48;
    constexpr std::size_t   kSystemEquip    = 0x88;    // walkergear::EquipController
    constexpr std::size_t   kEquipArmKinds  = 0x30;    // u16 per walker: the weapon in hand, 2 = sidearm
    constexpr std::size_t   kEquipSidearms  = 0x40;
    constexpr std::size_t   kSidearmSize    = 0x50;
    constexpr std::size_t   kSidearmAmmo    = 0x38;    // what EquipController::SubCurrentSlotAmmoCount takes the shots from
    constexpr std::uint16_t kSidearm        = 2;

    constexpr float kFireRate  = 8.0f;    // the tank roof MG's: a shot every 0.125 s
    constexpr float kHeldShot  = 0.05f;   // next-shot time kept while overheated

    const std::uint8_t kStateEntry[] = { 0x48, 0x89, 0x5C, 0x24, 0x18, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57,
                                         0x48, 0x8D, 0x6C, 0x24, 0xC0, 0x48, 0x81, 0xEC };

    struct Gun
    {
        std::uint8_t* work;
        std::uint32_t* ammo;
        float         dt;
    };

    struct Shots
    {
        const std::uint8_t* work;   // the walker work the counts belong to
        std::uint32_t       lastShots;
        std::uint32_t       fullAmmo;
    };

    State_t g_Orig = nullptr;
    void*   g_Target = nullptr;
    SRWLOCK g_ShotsLock = SRWLOCK_INIT;
    Shots   g_Shots{};
#ifdef _DEBUG
    ULONGLONG     g_NextLog = 0;
    std::uint32_t g_LoggedShots = 0;
#endif

    bool SidearmInHandSEH(const std::uint8_t* attack, std::uint32_t walker, Gun& gun)
    {
        __try
        {
            auto* works = *reinterpret_cast<std::uint8_t* const*>(attack + kWorks);
            const auto first = *reinterpret_cast<const std::uint32_t*>(attack + kFirstWalker);
            const auto* system = *reinterpret_cast<const std::uint8_t* const*>(attack + kSystem);
            if (!works || !system)
                return false;
            gun.work = works + static_cast<std::uint64_t>(walker - first) * kWorkSize;
            const auto walkerId = *reinterpret_cast<const std::uint32_t*>(gun.work + kWorkWalkerId);
            const auto* equip = *reinterpret_cast<const std::uint8_t* const*>(system + kSystemEquip);
            if ((*reinterpret_cast<const std::uint16_t* const*>(equip + kEquipArmKinds))[walkerId] != kSidearm)
                return false;
            gun.ammo = reinterpret_cast<std::uint32_t*>(*reinterpret_cast<std::uint8_t* const*>(equip + kEquipSidearms) + walkerId * kSidearmSize
                                                        + kSidearmAmmo);
            const auto* clock = *reinterpret_cast<const std::uint8_t* const*>(system + kSystemClock);
            gun.dt = (*reinterpret_cast<const float* const*>(clock + kClockPerWalker))[walkerId] * *reinterpret_cast<const float*>(clock + kClockFrame);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void SetFireRateSEH(std::uint8_t* work)
    {
        __try
        {
            *reinterpret_cast<float*>(work + kWorkFireRate) = kFireRate;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    bool ReadShotsSEH(const Gun& gun, std::uint32_t& shots, std::uint32_t& ammo)
    {
        __try
        {
            shots = *reinterpret_cast<const std::uint32_t*>(gun.work + kWorkShots);
            ammo = *gun.ammo;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void WriteGunSEH(const Gun& gun, bool holdFire, std::uint32_t ammo)
    {
        __try
        {
            auto* nextShot = reinterpret_cast<float*>(gun.work + kWorkNextShot);
            if (holdFire && *nextShot < kHeldShot)
                *nextShot = kHeldShot;
            if (*gun.ammo < ammo)
                *gun.ammo = ammo;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    void TrackShots(const Gun& gun)
    {
        std::uint32_t shots = 0;
        std::uint32_t ammo = 0;
        if (!ReadShotsSEH(gun, shots, ammo))
            return;
        AcquireSRWLockExclusive(&g_ShotsLock);
        Shots& s = g_Shots;
        if (s.work != gun.work)
            s = { gun.work, shots, ammo };
        const std::uint32_t fired = shots >= s.lastShots ? shots - s.lastShots : shots;
        s.lastShots = shots;
        s.fullAmmo = (std::max)(s.fullAmmo, ammo);
        const std::uint32_t fullAmmo = s.fullAmmo;
        ReleaseSRWLockExclusive(&g_ShotsLock);
        const bool locked = battleGearWeapons::HeatMg(fired);
#ifdef _DEBUG
        g_LoggedShots += fired;
        const ULONGLONG now = GetTickCount64();
        if (now >= g_NextLog)
        {
            g_NextLog = now + 1000;
            LogDebug("BattleGear heavy MG: %u shots since the last line, heat %.2f of 1%s, ammo %u (kept at %u)", g_LoggedShots,
                     battleGearWeapons::MgHeat(), locked ? ", overheated" : "", ammo, fullAmmo);
            g_LoggedShots = 0;
        }
#endif
        WriteGunSEH(gun, locked, fullAmmo);
        if (fired > 0)
        {
            battleGearWeapons::NoteMgShots(fired);
            battleGearEffects::EjectBeltLinks(fired);
        }
    }

    void __fastcall hkState(void* attack, std::uint32_t walker, std::uint32_t proc)
    {
        g_Orig(attack, walker, proc);
        if ((proc != kProcStart && proc != kProcUpdate) || !attack || !battleGearWeapons::IsBattleGearAttack(attack))
            return;
        Gun gun{};
        if (!SidearmInHandSEH(static_cast<const std::uint8_t*>(attack), walker, gun))
            return;
        if (proc == kProcStart)
            SetFireRateSEH(gun.work);
        else
            TrackShots(gun);
    }
}

bool Install_WalkerGearAttackActionImpl_State_HoldOnArmWeapon_Hook()
{
    if (g_Target)
        return true;
    void* target = gAddr.WalkerGearAttackActionImpl_State_HoldOnArmWeapon
                       ? ResolveGameAddress(gAddr.WalkerGearAttackActionImpl_State_HoldOnArmWeapon) : nullptr;
    if (!target)
    {
        LogError("BattleGear: walker arm weapon state address missing on this build, the heavy MG keeps the ZE'EV's fire");
        return false;
    }
    const HookEntry state = CheckHookEntry(target, kStateEntry, sizeof(kStateEntry));
    if (state == HookEntry::Unknown)
    {
        char desc[160] = {};
        DescribeFunctionEntry(target, desc, sizeof(desc));
        LogError("BattleGear: walker arm weapon state code differs on this build (%s), the heavy MG keeps the ZE'EV's fire", desc);
        return false;
    }
    if (state == HookEntry::ForeignHook)
        LogHookedOnTop("BattleGear: walker State_HoldOnArmWeapon", target);
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hkState), reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("BattleGear: walker arm weapon state hook failed, the heavy MG keeps the ZE'EV's fire");
        return false;
    }
    g_Target = target;
    return true;
}

void Uninstall_WalkerGearAttackActionImpl_State_HoldOnArmWeapon_Hook()
{
    if (!g_Target)
        return;
    DisableAndRemoveHook(g_Target);
    g_Target = nullptr;
    g_Orig = nullptr;
    battleGearWeapons::ResetMgHeat();
}
