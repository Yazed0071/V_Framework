#include <pch.h>

#include "BulletSystem3Impl_CreateBullet.h"

#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "BattleGearWeapons.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    using CreateBullet_t        = void(__fastcall*)(void* bulletSystem, const std::uint8_t* desc);
    using GetQuarkSystemTable_t = std::uint8_t*(__fastcall*)();
    using GroupByName_t         = std::uint16_t(__fastcall*)(void* geometry, const std::uint64_t* name, std::uint32_t one);

    constexpr std::size_t   kDescSize       = 0x40;    // Bullet3Desc
    constexpr std::size_t   kDescStart      = 0x00;
    constexpr std::size_t   kDescDirection  = 0x10;
    constexpr std::size_t   kDescGroup      = 0x28;    // the geometry group the round hits through
    constexpr std::size_t   kDescShooter    = 0x2A;
    constexpr std::size_t   kDescSecond     = 0x2C;    // the gun's own game object: the walker, as its sidearm rounds carry it
    constexpr std::size_t   kDescAttack     = 0x2E;
    constexpr std::size_t   kDescRow        = 0x30;    // TppEquip.BL_*
    constexpr std::size_t   kDescFlags      = 0x32;
    constexpr std::uint8_t  kTracer         = 0x20;
    constexpr std::uint8_t  kVehicleRound   = 0x02;    // set on every round the vehicles' guns fire
    constexpr int           kTracerEvery    = 6;       // the tank's cupola gun lights every sixth round
    constexpr std::uint16_t kTankHmgAttack  = 0x133;   // ATK_Tankgun_12_7mmHeavyMachineGun_West
    constexpr std::uint8_t  kTankHmgRow     = 0x3E;    // BL_Turret: the tank gun's 625 m/s round, its tracers and hits
    constexpr std::uint8_t  kRailRoundRow   = 0x56;    // BL_HeliChainGun, the round Sahelanthropus' rail gun fires
    constexpr std::uint64_t kWalkerShotName = 0x30D490F9F531;   // the geometry group the walker's guns look up
    constexpr std::size_t   kQstGeometry    = 0x80;    // -> +0x10 geometry system, vt+0x58 = group by name
    constexpr std::size_t   kGeometry       = 0x10;
    constexpr std::size_t   kGroupByName    = 0x58 / sizeof(void*);
    constexpr std::size_t   kQstSystems     = 0x98;    // -> +0x218 bullet system, vt[0] = CreateBullet
    constexpr std::size_t   kBulletSystem   = 0x218;

    const std::uint8_t kCreateBulletEntry[] = { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xFA, 0x48, 0x8B,
                                                0xD9 };

    CreateBullet_t        g_Orig = nullptr;
    void*                 g_Target = nullptr;
    GetQuarkSystemTable_t g_GetQuarkSystemTable = nullptr;
    std::atomic<int>      g_HmgRounds{ 0 };

    // the walker sidearm carries the tank gun's attack while the Battle Gear loadout applies (GetAttackIdByEquipId); the tank's
    // own rounds already fly as BL_Turret
    bool IsBattleGearHmgRoundSEH(const std::uint8_t* desc, std::uint8_t* round)
    {
        __try
        {
            if (*reinterpret_cast<const std::uint16_t*>(desc + kDescAttack) != kTankHmgAttack || desc[kDescRow] == kTankHmgRow)
                return false;
            std::memcpy(round, desc, kDescSize);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void __fastcall hkCreateBullet(void* bulletSystem, const std::uint8_t* desc)
    {
        alignas(16) std::uint8_t round[kDescSize];
        if (desc && IsBattleGearHmgRoundSEH(desc, round) && battleGearWeapons::LoadoutApplies())
        {
#ifdef _DEBUG
            if (g_HmgRounds.load(std::memory_order_relaxed) == 0)
                LogDebug("BattleGear roof MG: first round turned into BL_Turret (was row %u, flags 0x%02X, shooter 0x%04X)",
                         round[kDescRow], round[kDescFlags], *reinterpret_cast<const std::uint16_t*>(round + kDescShooter));   // TEMP probe
#endif
            round[kDescRow] = kTankHmgRow;
            if (g_HmgRounds.fetch_add(1, std::memory_order_relaxed) % kTracerEvery == 0)
                round[kDescFlags] |= kTracer;
            g_Orig(bulletSystem, round);
            return;
        }
        g_Orig(bulletSystem, desc);
    }

    bool FireRoundSEH(std::uint8_t* round)
    {
        __try
        {
            std::uint8_t* qst = g_GetQuarkSystemTable();
            void* geometry = *reinterpret_cast<void**>(*reinterpret_cast<std::uint8_t**>(qst + kQstGeometry) + kGeometry);
            void* bullets = *reinterpret_cast<void**>(*reinterpret_cast<std::uint8_t**>(qst + kQstSystems) + kBulletSystem);
            if (!geometry || !bullets)
                return false;
            const auto groupByName = reinterpret_cast<GroupByName_t>((*static_cast<void***>(geometry))[kGroupByName]);
            *reinterpret_cast<std::uint16_t*>(round + kDescGroup) = groupByName(geometry, &kWalkerShotName, 1);
            g_Orig(bullets, round);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }
}

bool FireBattleGearRailRound(const float* start, const float* direction, std::uint16_t attackId, std::uint16_t shooterId,
                             std::uint16_t walkerId)
{
    if (!g_Orig || !g_GetQuarkSystemTable || !start || !direction)
        return false;
    alignas(16) std::uint8_t round[kDescSize] = {};
    auto* at = reinterpret_cast<float*>(round + kDescStart);
    auto* toward = reinterpret_cast<float*>(round + kDescDirection);
    for (int i = 0; i < 3; ++i)
    {
        at[i] = start[i];
        toward[i] = direction[i];
    }
    at[3] = 1.0f;
    *reinterpret_cast<std::uint16_t*>(round + kDescShooter) = shooterId;
    *reinterpret_cast<std::uint16_t*>(round + kDescSecond) = walkerId;
    *reinterpret_cast<std::uint16_t*>(round + kDescAttack) = attackId;
    round[kDescRow] = kRailRoundRow;
    round[kDescFlags] = kVehicleRound | kTracer;
    return FireRoundSEH(round);
}

bool FireBattleGearMgRound(const float* start, const float* direction, std::uint16_t shooterId, std::uint16_t walkerId)
{
    if (!g_Orig || !g_GetQuarkSystemTable || !start || !direction)
        return false;
    alignas(16) std::uint8_t round[kDescSize] = {};
    auto* at = reinterpret_cast<float*>(round + kDescStart);
    auto* toward = reinterpret_cast<float*>(round + kDescDirection);
    for (int i = 0; i < 3; ++i)
    {
        at[i] = start[i];
        toward[i] = direction[i];
    }
    at[3] = 1.0f;
    *reinterpret_cast<std::uint16_t*>(round + kDescShooter) = shooterId;
    *reinterpret_cast<std::uint16_t*>(round + kDescSecond) = walkerId;
    *reinterpret_cast<std::uint16_t*>(round + kDescAttack) = kTankHmgAttack;
    round[kDescRow] = kTankHmgRow;
    round[kDescFlags] = kVehicleRound;
    if (g_HmgRounds.fetch_add(1, std::memory_order_relaxed) % kTracerEvery == 0)
        round[kDescFlags] |= kTracer;
    return FireRoundSEH(round);
}

bool FireBattleGearRound(const float* start, const float* direction, std::uint16_t attackId, std::uint8_t bulletRow, bool tracer,
                         std::uint16_t shooterId, std::uint16_t walkerId)
{
    if (!g_Orig || !g_GetQuarkSystemTable || !start || !direction)
        return false;
    alignas(16) std::uint8_t round[kDescSize] = {};
    auto* at = reinterpret_cast<float*>(round + kDescStart);
    auto* toward = reinterpret_cast<float*>(round + kDescDirection);
    for (int i = 0; i < 3; ++i)
    {
        at[i] = start[i];
        toward[i] = direction[i];
    }
    at[3] = 1.0f;
    *reinterpret_cast<std::uint16_t*>(round + kDescShooter) = shooterId;
    *reinterpret_cast<std::uint16_t*>(round + kDescSecond) = walkerId;
    *reinterpret_cast<std::uint16_t*>(round + kDescAttack) = attackId;
    round[kDescRow] = bulletRow;
    round[kDescFlags] = tracer ? kVehicleRound | kTracer : kVehicleRound;
    return FireRoundSEH(round);
}

bool Install_BulletSystem3Impl_CreateBullet_Hook()
{
    if (g_Target)
        return true;
    void* target = gAddr.BulletSystem3Impl_CreateBullet ? ResolveGameAddress(gAddr.BulletSystem3Impl_CreateBullet) : nullptr;
    void* quarkSystemTable = gAddr.GetQuarkSystemTable ? ResolveGameAddress(gAddr.GetQuarkSystemTable) : nullptr;
    if (!target || !quarkSystemTable)
    {
        LogError("BattleGear: bullet system address missing on this build, the roof MG fires the D-Walker's rounds and the rail gun no round");
        return false;
    }
    const HookEntry state = CheckHookEntry(target, kCreateBulletEntry, sizeof(kCreateBulletEntry));
    if (state == HookEntry::Unknown)
    {
        char desc[160] = {};
        DescribeFunctionEntry(target, desc, sizeof(desc));
        LogError("BattleGear: bullet system code differs on this build (%s), the roof MG fires the D-Walker's rounds and the rail gun no round",
                 desc);
        return false;
    }
    if (state == HookEntry::ForeignHook)
        LogHookedOnTop("BattleGear: BulletSystem3Impl CreateBullet", target);
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hkCreateBullet), reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("BattleGear: bullet system hook failed, the roof MG fires the D-Walker's rounds and the rail gun no round");
        return false;
    }
    g_GetQuarkSystemTable = reinterpret_cast<GetQuarkSystemTable_t>(quarkSystemTable);
    g_Target = target;
    return true;
}

void Uninstall_BulletSystem3Impl_CreateBullet_Hook()
{
    if (!g_Target)
        return;
    DisableAndRemoveHook(g_Target);
    g_Target = nullptr;
    g_Orig = nullptr;
    g_GetQuarkSystemTable = nullptr;
}
