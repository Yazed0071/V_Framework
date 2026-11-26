#include <pch.h>

#include "BattleGearRoofGun.h"

#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <cstdint>

#include "BattleGearArmory.h"
#include "BattleGearEffects.h"
#include "BattleGearSound.h"
#include "BattleGearWeapons.h"
#include "BulletSystem3Impl_CreateBullet.h"
#include "log.h"

namespace
{
    constexpr float         kShotEvery    = 0.125f;   // the tanks' cupola gun's fireInterval
    constexpr std::uint32_t kMostRounds   = 2;        // a frame
    constexpr float         kOnAim        = 0.966f;   // cos 15 degrees: past that off the barrel the round leaves along the barrel
    constexpr std::int32_t  kRoofGunArm   = 1;
    constexpr std::size_t   kSystemType   = 0x08;     // game object type of this system's walkers
    constexpr std::size_t   kSystemEquip  = 0x88;     // walkergear::EquipController
    constexpr std::size_t   kEquipSlots   = 0x30;     // u16 per walker id: the weapon its trigger works, 1 main gun, 2 sidearm, 3 arm
    constexpr std::uint16_t kMainGunSlot  = 1;
    constexpr std::uint16_t kSidearmSlot  = 2;
    constexpr std::uint16_t kArmSlot      = 3;
    constexpr std::size_t   kCommRider    = 0x2C8;

    // only the Battle Gear's walker frame touches these
    float g_UntilShot = 0.0f;
    bool  g_EmptySounded = false;
#ifdef _DEBUG
    ULONGLONG     g_NextLog = 0;     // TEMP probe: the roof gun's own trigger, rounds and heat
    std::uint32_t g_LoggedRounds = 0;
#endif

    // the weapon wheel's pick reaches the walker as its weapon slot (RideWalkerGearActionImpl::ChangeWeapon asks through the comm
    // block, the walker's ActionCoreImpl::Execute changes the slot): the sidearm slot is the roof gun. The Battle Gear has no arm
    // weapon: that pick is the main gun.
    bool RoofGunPickedSEH(void* walkerSystem, std::uint16_t walkerId)
    {
        __try
        {
            const auto* equip = *reinterpret_cast<const std::uint8_t* const*>(static_cast<const std::uint8_t*>(walkerSystem) + kSystemEquip);
            auto* slots = equip ? *reinterpret_cast<std::uint16_t* const*>(equip + kEquipSlots) : nullptr;
            if (!slots)
                return false;
            if (slots[walkerId] == kArmSlot)
                slots[walkerId] = kMainGunSlot;   // the hold-on-arm states see the change and put the arm away themselves
            return slots[walkerId] == kSidearmSlot;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    std::uint32_t g_RegisteredRounds = 0;

    // a registered roof gun fires its own round, every sixth a tracer like the built-in one's
    bool FireRoofRound(const float* muzzle, const float* direction, std::uint16_t shooter, std::uint16_t walker)
    {
        const std::uint16_t attackId = battleGearArmory::RoofGunAttackId();
        const std::uint8_t bulletId = battleGearArmory::RoofGunBulletId();
        if (attackId == battleGearArmory::kRoofMgAttackId && bulletId == battleGearArmory::kRoofMgBulletId)
            return FireBattleGearMgRound(muzzle, direction, shooter, walker);
        return FireBattleGearRound(muzzle, direction, attackId, bulletId, g_RegisteredRounds++ % 6 == 0, shooter, walker);
    }

    bool ShooterIdsSEH(void* walkerSystem, const std::uint8_t* comm, std::uint16_t commId, std::uint16_t& shooter, std::uint16_t& walker)
    {
        __try
        {
            const std::uint16_t type = *reinterpret_cast<const std::uint16_t*>(static_cast<const std::uint8_t*>(walkerSystem) + kSystemType);
            walker = static_cast<std::uint16_t>((type << 9) | (commId & 0x1FF));
            shooter = *reinterpret_cast<const std::uint16_t*>(comm + kCommRider);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }
}

namespace battleGearRoofGun
{
    void OnWalkerFrame(void* walkerSystem, std::uint32_t index, const std::uint8_t* comm, std::uint16_t commId, float dt)
    {
        if (!battleGearWeapons::Ridden())
        {
            g_UntilShot = 0.0f;
            g_EmptySounded = false;
            return;
        }
        battleGearWeapons::SelectMg(RoofGunPickedSEH(walkerSystem, commId));
        g_UntilShot = (std::max)(0.0f, g_UntilShot - dt);
        const bool trigger = battleGearWeapons::MgTrigger();
        if (!trigger)
            g_EmptySounded = false;
        float muzzle[3];
        float barrel[3];
        float aim[3];
        std::uint32_t fired = 0;
        if (trigger && !battleGearWeapons::MgOverheated() && battleGearWeapons::MuzzleFor(kRoofGunArm, 0, muzzle)
            && battleGearWeapons::BarrelFor(kRoofGunArm, barrel))
        {
            float direction[3] = { barrel[0], barrel[1], barrel[2] };
            if (battleGearWeapons::AimPoint(aim))
            {
                const float to[3] = { aim[0] - muzzle[0], aim[1] - muzzle[1], aim[2] - muzzle[2] };
                const float length = std::sqrt(to[0] * to[0] + to[1] * to[1] + to[2] * to[2]);
                if (length > 1.0f && (to[0] * barrel[0] + to[1] * barrel[1] + to[2] * barrel[2]) / length >= kOnAim)
                    for (int i = 0; i < 3; ++i)
                        direction[i] = to[i] / length;
            }
            std::uint16_t shooter = 0;
            std::uint16_t walker = 0;
            if (ShooterIdsSEH(walkerSystem, comm, commId, shooter, walker))
            {
                while (g_UntilShot <= 0.0f && fired < kMostRounds)
                {
                    if (!battleGearWeapons::TakeMgRound())
                    {
                        if (!g_EmptySounded)
                            battleGearSound::RoofGunEmpty(walkerSystem, index, muzzle);
                        g_EmptySounded = true;
                        break;
                    }
                    if (!FireRoofRound(muzzle, direction, shooter, walker))
                    {
                        LogWarnOnce("BattleGear: the roof gun's round could not be fired (bullet system unreachable)");
                        break;
                    }
                    battleGearSound::RoofGunShot(walkerSystem, index, muzzle);
                    g_UntilShot += battleGearArmory::RoofGunFireInterval();   // kShotEvery for the built-in gun
                    ++fired;
                }
            }
        }
        if (fired)
        {
            battleGearWeapons::HeatMg(fired);
            battleGearWeapons::NoteMgShots(fired);
            battleGearEffects::EjectBeltLinks(fired);
        }
#ifdef _DEBUG
        g_LoggedRounds += fired;
        const ULONGLONG now = GetTickCount64();
        if (now >= g_NextLog && (battleGearWeapons::MgSelected() || g_LoggedRounds))
        {
            g_NextLog = now + 1000;
            LogDebug("BattleGear roof gun: selected %d, trigger %d, %u rounds since the last line, heat %.2f of 1%s, %u rounds left",
                     battleGearWeapons::MgSelected() ? 1 : 0, trigger ? 1 : 0, g_LoggedRounds, battleGearWeapons::MgHeat(),
                     battleGearWeapons::MgOverheated() ? " (overheated)" : "", battleGearWeapons::MgAmmo());
            g_LoggedRounds = 0;
        }
#endif
    }
}
