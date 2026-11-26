#include <pch.h>

#include "WalkerGearAttackActionImpl_RegisterStunGunOffenseTarget.h"

#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>

#include "AddressSet.h"
#include "BattleGearArmory.h"
#include "BattleGearLegs.h"
#include "BattleGearSound.h"
#include "BattleGearWeapons.h"
#include "BulletSystem3Impl_CreateBullet.h"
#include "EquipPartParams.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    using RegisterStunGunOffenseTarget_t = void(__fastcall*)(void* attack, std::uint8_t* work, std::uint32_t walker, float* start,
                                                             float* end, float radius, float charge);

    struct Shake
    {
        float power;       // camera offset = sin * power * 0.1 m
        float frequency;
        float decay;       // power kept each frame
    };

    constexpr std::size_t   kAttackIdAt   = 0x208;   // the shot's attack info: walker work +0x200, attack id at +0x08
    constexpr std::size_t   kAttackerAt   = 0x206;   // the walker's game object id, written by the register call
    constexpr std::size_t   kRateAt       = 0x20B;   // the shot's damage rate, 200 = all of the row's damage
    constexpr std::size_t   kGradeAt      = 0x20D;   // the gun's grade: under the target's grade the damage drops a quarter a grade
    constexpr std::size_t   kCommRider    = 0x2C8;   // the rider's game object id; the walker's guns credit him while he rides
    constexpr std::size_t   kChargeLevel  = 0x1AC;   // 1-3; the muzzle flash right after this call picks its variation by it
    constexpr std::uint32_t kPlainFlash   = 3;       // level 3 = empty variation, the rail-gun blast plays as authored
    constexpr std::uint16_t kStunGun      = 0x97;    // ATK_WalkerGear_StunGun
    constexpr std::uint16_t kRailGun      = 0xA2;    // ATK_BattleGear_RailGun
    constexpr std::uint32_t kShotRayFlags = 0x4020700;   // what the stun gun's own impact line check hits
    constexpr float         kRadius       = 0.8f;
    constexpr std::size_t   kShakePower     = 0x10;
    constexpr std::size_t   kShakeFrequency = 0x14;
    constexpr std::size_t   kShakeDecay     = 0x18;

    constexpr float       kRanges[3]          = { 80.0f, 160.0f, 250.0f };
    // a weaker charge carries less of the full charge's impact force (the share armor lets through), vehicle damage and blow;
    // the slug itself kills a soldier at every level. The stun gun's own damage rate per charge level (0.3 / 0.6 / 1) is not
    // taken: on top of the level's row it cut a level 1 and 2 shot to 450 and 1800 of the row's 4500, under a soldier's 2600 life.
    constexpr float       kLevelForce[3]      = { 1.0f / 3.0f, 2.0f / 3.0f, 1.0f };
    constexpr float       kFullRate           = 1.0f;
    // soldiers skip every hit whose damage row carries no type flag (soldier DamageHandlerImpl::ProcessDamages), and the game's
    // rail-gun row, like Sahelanthropus' it was copied from, carries none: no level did anything to them. The slug is a
    // bullet armor does not stop.
    constexpr int         kSlugFlags          = 0x1 | 0x8000;   // isBullet, isPenetrating
    constexpr const char* kLevelDamageName[3] = { "ATK_BattleGear_RailGun_Level1", "ATK_BattleGear_RailGun_Level2",
                                                  "ATK_BattleGear_RailGun_Level3" };
    constexpr Shake       kShakes[3]          = { { 0.5f, 4.0f, 0.9f }, { 0.9f, 4.0f, 0.9f }, { 1.5f, 4.0f, 0.9f } };

    const std::uint8_t kRegisterEntry[] = { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC,
                                            0x20, 0x44, 0x2B, 0x81, 0x98, 0x00, 0x00, 0x00, 0x49, 0x8B, 0xF1, 0x48, 0x8B, 0xFA };

    RegisterStunGunOffenseTarget_t g_Orig = nullptr;
    void*                          g_Target = nullptr;
    void* const*                   g_CameraShake = nullptr;
    std::atomic<int>               g_LevelDamageIds[3] = {};
    std::atomic<bool>              g_WarnedLevelDamage{ false };

    bool IsRailGunId(std::uint16_t id)
    {
        if (id == kRailGun || battleGearArmory::IsMainAttackId(id))
            return true;
        for (const std::atomic<int>& level : g_LevelDamageIds)
        {
            const int levelId = level.load(std::memory_order_relaxed);
            if (levelId != 0 && id == levelId)
                return true;
        }
        return false;
    }

    void SetAttackIdSEH(std::uint8_t* work, std::uint16_t to)
    {
        __try
        {
            *reinterpret_cast<std::uint16_t*>(work + kAttackIdAt) = to;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    void PutBackStunGunSEH(std::uint8_t* work)   // a D-Walker later called into the same walker work
    {
        __try
        {
            auto* id = reinterpret_cast<std::uint16_t*>(work + kAttackIdAt);
            if (IsRailGunId(*id))
                *id = kStunGun;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    int ChargeLevelSEH(const std::uint8_t* work)
    {
        __try
        {
            return std::clamp(*reinterpret_cast<const std::int32_t*>(work + kChargeLevel), 1, 3);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 3;
        }
    }

    std::uint16_t AttackIdFor(int level)
    {
        std::atomic<int>& slot = g_LevelDamageIds[level - 1];
        int id = slot.load(std::memory_order_acquire);
        if (!id)
        {
            id = EquipParam_DeclareScaledDamage(kLevelDamageName[level - 1], kRailGun, 1.0f, kLevelForce[level - 1], kSlugFlags);
            if (id)
                slot.store(id, std::memory_order_release);
            else if (!g_WarnedLevelDamage.exchange(true))
                LogWarn("BattleGear: custom damage ids are not served (custom weapon damage off), the rail-gun's hits do nothing to soldiers");
        }
        return id ? static_cast<std::uint16_t>(id) : kRailGun;
    }

    void ShakeCameraSEH(const Shake& shake)   // what the EmitCameraShake binding writes
    {
        __try
        {
            auto* state = g_CameraShake ? static_cast<std::uint8_t*>(*g_CameraShake) : nullptr;
            if (!state)
                return;
            auto* power = reinterpret_cast<float*>(state + kShakePower);
            if (*power < shake.power)
                *power = shake.power;
            *reinterpret_cast<float*>(state + kShakeFrequency) = shake.frequency;
            *reinterpret_cast<float*>(state + kShakeDecay) = shake.decay;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    void SetChargeLevelSEH(std::uint8_t* work, std::uint32_t level)
    {
        __try
        {
            *reinterpret_cast<std::uint32_t*>(work + kChargeLevel) = level;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    bool RailShot(float* start, float* end, int level, bool& blocked)
    {
        float muzzle[3];
        float target[3];
        if (!battleGearWeapons::RailGunMuzzle(muzzle) || !battleGearWeapons::AimPoint(target))
            return false;
        battleGearArmory::OffsetMainMuzzle(muzzle);   // a registered gun's own muzzle point
        if (!battleGearWeapons::Ridden())
            battleGearWeapons::NoteAiAttack();
        float dir[3] = { target[0] - muzzle[0], target[1] - muzzle[1], target[2] - muzzle[2] };
        float length = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
        if (length < 1.0f)
        {
            dir[0] = end[0] - start[0];
            dir[1] = end[1] - start[1];
            dir[2] = end[2] - start[2];
            length = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
            if (length < 1e-3f)
                return false;
        }
        float reach[3];
        for (int i = 0; i < 3; ++i)
            reach[i] = muzzle[i] + dir[i] / length * kRanges[level - 1];
        float hit[3];
        blocked = battleGearLegs::FirstHit(muzzle, reach, kShotRayFlags, hit);
        for (int i = 0; i < 3; ++i)
        {
            start[i] = muzzle[i];
            end[i] = blocked ? hit[i] : reach[i];
        }
        LogDebug("BattleGear: rail-gun level %d shot from (%.1f %.1f %.1f) to (%.1f %.1f %.1f), %s", level, start[0], start[1], start[2],
                 end[0], end[1], end[2], blocked ? "stopped by a wall" : "full range");
        return true;
    }

#ifdef _DEBUG
    void LogShotDamageSEH(const std::uint8_t* work, int level)   // TEMP probe: what the registered shot carries
    {
        __try
        {
            LogDebug("BattleGear rail-gun damage: level %d, attack id 0x%X, rate %u of 200, gun grade %u", level,
                     *reinterpret_cast<const std::uint16_t*>(work + kAttackIdAt), work[kRateAt], work[kGradeAt]);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }
#endif

    bool ShotIdsSEH(const std::uint8_t* work, std::uint16_t& shooter, std::uint16_t& walker)
    {
        __try
        {
            walker = *reinterpret_cast<const std::uint16_t*>(work + kAttackerAt);
            const auto* comm = *static_cast<const std::uint8_t* const*>(battleGearWeapons::BattleGearCommSlot());
            shooter = comm && battleGearWeapons::Ridden() ? *reinterpret_cast<const std::uint16_t*>(comm + kCommRider) : walker;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    // the round Sahelanthropus fires with its rail gun, from the muzzle to where the shot stops; the capsule still pierces
    void FireRound(const float* start, const float* end, std::uint16_t attackId, const std::uint8_t* work)
    {
        float direction[3] = { end[0] - start[0], end[1] - start[1], end[2] - start[2] };
        const float length = std::sqrt(direction[0] * direction[0] + direction[1] * direction[1] + direction[2] * direction[2]);
        if (length < 1e-3f)
            return;
        for (float& d : direction)
            d /= length;
        std::uint16_t shooter = 0;
        std::uint16_t walker = 0;
        const std::uint8_t ownRound = battleGearArmory::MainBulletId();   // a registered gun's own TppEquip.BL_* row
        if (!ShotIdsSEH(work, shooter, walker)
            || !(ownRound ? FireBattleGearRound(start, direction, attackId, ownRound, true, shooter, walker)
                          : FireBattleGearRailRound(start, direction, attackId, shooter, walker)))
            LogWarnOnce("BattleGear: the rail-gun round could not be fired (bullet system unreachable), the shot still hits");
    }

    void __fastcall hkRegisterStunGunOffenseTarget(void* attack, std::uint8_t* work, std::uint32_t walker, float* start, float* end,
                                                   float radius, float charge)
    {
        const bool battleGear = work && battleGearWeapons::IsBattleGearAttack(attack);
        bool railShot = false;
        bool blocked = false;
        std::uint16_t attackId = kRailGun;
        if (battleGear)
        {
            const int level = ChargeLevelSEH(work);
            attackId = AttackIdFor(level);
            if (const std::uint16_t registered = battleGearArmory::MainAttackIdForLevel(level))   // a registered gun's own damage
                attackId = registered;
            SetAttackIdSEH(work, attackId);
            railShot = start && end && RailShot(start, end, level, blocked);
            if (railShot)
                radius = kRadius;
            charge = kFullRate;
            battleGearWeapons::NoteRailShot(level);
            if (battleGearWeapons::Ridden())
                ShakeCameraSEH(kShakes[level - 1]);
        }
        else if (work)
        {
            PutBackStunGunSEH(work);
        }
        g_Orig(attack, work, walker, start, end, radius, charge);
        if (battleGear)
        {
#ifdef _DEBUG
            LogShotDamageSEH(work, ChargeLevelSEH(work));
#endif
            SetChargeLevelSEH(work, kPlainFlash);
            if (railShot)
                FireRound(start, end, attackId, work);
            if (railShot && blocked)
                battleGearSound::RailImpact(end);
        }
    }
}

bool Install_WalkerGearAttackActionImpl_RegisterStunGunOffenseTarget_Hook()
{
    if (g_Target)
        return true;
    void* target = ResolveGameAddress(gAddr.WalkerGearAttackActionImpl_RegisterStunGunOffenseTarget);
    if (!target)
    {
        LogError("BattleGear: walker stun gun shot address missing on this build, the rail-gun fires as the stun gun");
        return false;
    }
    const HookEntry state = CheckHookEntry(target, kRegisterEntry, sizeof(kRegisterEntry));
    if (state == HookEntry::Unknown)
    {
        char desc[160] = {};
        DescribeFunctionEntry(target, desc, sizeof(desc));
        LogError("BattleGear: walker stun gun shot code differs on this build (%s), the rail-gun fires as the stun gun", desc);
        return false;
    }
    if (state == HookEntry::ForeignHook)
        LogHookedOnTop("BattleGear: walker RegisterStunGunOffenseTarget", target);
    g_CameraShake = static_cast<void* const*>(gAddr.CameraShake_Instance ? ResolveGameAddress(gAddr.CameraShake_Instance) : nullptr);
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hkRegisterStunGunOffenseTarget), reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("BattleGear: walker stun gun shot hook failed, the rail-gun fires as the stun gun");
        return false;
    }
    g_Target = target;
    return true;
}

void Uninstall_WalkerGearAttackActionImpl_RegisterStunGunOffenseTarget_Hook()
{
    if (!g_Target)
        return;
    DisableAndRemoveHook(g_Target);
    g_Target = nullptr;
    g_Orig = nullptr;
}
