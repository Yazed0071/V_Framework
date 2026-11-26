#include <pch.h>
#include "PermanentGimmickDefenseCallback_Exec.h"

#include <Windows.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <utility>

#include "AddressSet.h"
#include "HookUtils.h"
#include "LuaBroadcaster.h"
#include "MissionCodeGuard.h"
#include "log.h"

namespace
{
    using Exec_t                = std::uint32_t(__fastcall*)(void* callback, void* offense, void* defense, const void* result,
                                                             std::uint8_t* unit);
    using GetQuarkSystemTable_t = std::uint8_t* (__fastcall*)();
    using GetVehicle_t          = const std::int32_t* (__fastcall*)(void* vehicles, std::int32_t* result, std::uint8_t** info,
                                                                    std::uint32_t index);
    using GetWalkerGear_t       = std::uint8_t* (__fastcall*)(void* walkerGears, std::uint16_t gameObjectId);

    using AS = AddressSetRuntime::AddressSet;

    enum class Damage { LifeU16, LifeS16, LifeS32, BreakRequest, HitRecord };

    struct GimmickType
    {
        const char*          name;
        std::uintptr_t AS::* address;
        Damage               damage;
        std::size_t          field;
        std::uint8_t         entry[24];
        std::size_t          skipFrom;
        std::size_t          skipCount;
    };

    constexpr std::size_t kUnitGameObjectId   = 0x08;
    constexpr std::size_t kUnitName           = 0x60;   // locator StrCode32 | low 32 bits of the data set's path code << 32
    constexpr std::size_t kUnitBreakRequest   = 0x6C;
    constexpr std::size_t kRadioHitRecord     = 0xD0;   // attack id, hit transform and attacker the radio keeps for its update
    constexpr std::size_t kRadioHitRecordSize = 0x52;
    constexpr std::size_t kOffenseData        = 0x40;
    constexpr std::size_t kOffenseAttacker    = 0x06;

    constexpr std::uint16_t kTypeShift      = 9;
    constexpr std::uint16_t kIndexMask      = 0x1FF;
    constexpr std::uint16_t kNoGameObject   = 0xFFFF;
    constexpr std::uint16_t kWalkerGearType = 0x2C;
    constexpr std::uint16_t kVehicleType    = 0x32;

    constexpr std::size_t kQuarkTableApplication = 0x98;
    constexpr std::size_t kApplicationVehicles   = 0x2A8;
    constexpr std::size_t kGetVehicleSlot        = 3;
    constexpr std::size_t kVehicleDriver         = 0x30;
    constexpr std::size_t kApplicationWalkers    = 0x2C0;
    constexpr std::size_t kGetWalkerGearSlot     = 5;
    constexpr std::size_t kWalkerGearPilot       = 0x2C8;

    const GimmickType kTypes[] = {
        { "DoorDefenceCallback", &AS::DoorDefenceCallback_Exec, Damage::BreakRequest, kUnitBreakRequest,
          { 0x48, 0x8B, 0xC4, 0x57, 0x48, 0x83, 0xEC, 0x60, 0x48, 0xC7, 0x40, 0xC0, 0xFE, 0xFF, 0xFF, 0xFF, 0x48, 0x89, 0x58, 0x08, 0x48, 0x89, 0x68, 0x18 }, 0, 0 },
        { "GatlingGunDefenseCallback", &AS::GatlingGunDefenseCallback_Exec, Damage::LifeS32, 0xF4,
          { 0x48, 0x89, 0x7C, 0x24, 0x18, 0x55, 0x48, 0x8B, 0xEC, 0x48, 0x81, 0xEC, 0x80, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x7A, 0x40, 0x48, 0x85, 0xFF, 0x0F }, 0, 0 },
        { "ImportantBreakableDefenseCallback", &AS::ImportantBreakableDefenseCallback_Exec, Damage::LifeU16, 0xD0,
          { 0x48, 0x8B, 0xC4, 0x56, 0x57, 0x41, 0x54, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x40, 0x48, 0xC7, 0x40, 0xB8, 0xFE, 0xFF, 0xFF, 0xFF, 0x48 }, 0, 0 },
        { "MachineGunDefenseCallback", &AS::MachineGunDefenseCallback_Exec, Damage::LifeU16, 0x10C,
          { 0x48, 0x8B, 0xC4, 0x55, 0x56, 0x57, 0x41, 0x56, 0x41, 0x57, 0x48, 0x81, 0xEC, 0x90, 0x00, 0x00, 0x00, 0x48, 0xC7, 0x44, 0x24, 0x20, 0xFE, 0xFF }, 0, 0 },
        { "MortarDefenseCallback", &AS::MortarDefenseCallback_Exec, Damage::LifeU16, 0xFC,
          { 0x48, 0x8B, 0xC4, 0x55, 0x56, 0x57, 0x41, 0x56, 0x41, 0x57, 0x48, 0x81, 0xEC, 0x90, 0x00, 0x00, 0x00, 0x48, 0xC7, 0x44, 0x24, 0x20, 0xFE, 0xFF }, 0, 0 },
        { "SearchLightDefenseCallback", &AS::SearchLightDefenseCallback_Exec, Damage::BreakRequest, kUnitBreakRequest,
          { 0x4C, 0x8B, 0xDC, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x49, 0xC7, 0x43, 0xE8, 0xFE, 0xFF, 0xFF, 0xFF, 0x49, 0x89, 0x5B, 0x08, 0x49, 0x89, 0x73, 0x18 }, 0, 0 },
        { "WatchTowerDefenseCallback", &AS::WatchTowerDefenseCallback_Exec, Damage::LifeS16, 0x168,
          { 0x48, 0x8B, 0xC4, 0x55, 0x56, 0x57, 0x48, 0x81, 0xEC, 0x90, 0x00, 0x00, 0x00, 0x48, 0xC7, 0x44, 0x24, 0x20, 0xFE, 0xFF, 0xFF, 0xFF, 0x48, 0x89 }, 0, 0 },
        { "IrSensorDefCallback", &AS::IrSensorDefCallback_Exec, Damage::BreakRequest, kUnitBreakRequest,
          { 0x40, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x48, 0xC7, 0x44, 0x24, 0x20, 0xFE, 0xFF, 0xFF, 0xFF, 0x48, 0x89, 0x5C, 0x24, 0x40, 0x48, 0x89, 0x6C, 0x24 }, 0, 0 },
        { "EventAnimationDefenseCallback", &AS::EventAnimationDefenseCallback_Exec, Damage::BreakRequest, kUnitBreakRequest,
          { 0x40, 0x53, 0x55, 0x48, 0x83, 0xEC, 0x48, 0x48, 0x8B, 0x05, 0x00, 0x00, 0x00, 0x00, 0x48, 0x33, 0xC4, 0x48, 0x89, 0x44, 0x24, 0x30, 0x48, 0x8B }, 10, 4 },   // stack cookie load at +7
        { "BridgeDefenseCallback", &AS::BridgeDefenseCallback_Exec, Damage::LifeS16, 0xE8,
          { 0x48, 0x8B, 0xC4, 0x56, 0x57, 0x41, 0x54, 0x41, 0x56, 0x41, 0x57, 0x48, 0x81, 0xEC, 0x80, 0x00, 0x00, 0x00, 0x48, 0xC7, 0x40, 0x80, 0xFE, 0xFF }, 0, 0 },
        { "WaterTowerDefenseCallback", &AS::WaterTowerDefenseCallback_Exec, Damage::LifeS32, 0x124,
          { 0x48, 0x8B, 0xC4, 0x57, 0x48, 0x81, 0xEC, 0xA0, 0x00, 0x00, 0x00, 0x48, 0xC7, 0x40, 0x80, 0xFE, 0xFF, 0xFF, 0xFF, 0x48, 0x89, 0x58, 0x08, 0x48 }, 0, 0 },
        { "ToiletDefenceCallback", &AS::ToiletDefenceCallback_Exec, Damage::LifeU16, 0x128,
          { 0x40, 0x56, 0x57, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x30, 0x48, 0xC7, 0x44, 0x24, 0x20, 0xFE, 0xFF, 0xFF, 0xFF, 0x48, 0x89, 0x5C, 0x24, 0x50, 0x48 }, 0, 0 },
        { "RadioCasseteDefenseCallback", &AS::RadioCasseteDefenseCallback_Exec, Damage::HitRecord, kRadioHitRecord,
          { 0x48, 0x8B, 0xC4, 0x55, 0x57, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8D, 0x68, 0xA9, 0x48, 0x81, 0xEC, 0x00, 0x01, 0x00, 0x00, 0x48, 0xC7 }, 0, 0 },
    };
    constexpr std::size_t kTypeCount = sizeof(kTypes) / sizeof(kTypes[0]);

    struct Snapshot
    {
        std::int64_t  life;
        std::uint32_t request;
        std::uint8_t  hitRecord[kRadioHitRecordSize];
    };

    struct Damaged
    {
        std::uint16_t gimmickId;
        std::uint32_t locator;
        std::uint32_t dataSet;
        std::uint16_t attacker;
    };

    Exec_t g_Orig[kTypeCount]   = {};
    void*  g_Target[kTypeCount] = {};

    bool TakeSnapshotSEH(const GimmickType& type, const std::uint8_t* unit, Snapshot& out)
    {
        __try
        {
            const std::uint8_t* field = unit + type.field;
            switch (type.damage)
            {
            case Damage::LifeU16:      out.life = *reinterpret_cast<const std::uint16_t*>(field); break;
            case Damage::LifeS16:      out.life = *reinterpret_cast<const std::int16_t*>(field); break;
            case Damage::LifeS32:      out.life = *reinterpret_cast<const std::int32_t*>(field); break;
            case Damage::BreakRequest: out.request = *reinterpret_cast<const std::uint32_t*>(field); break;
            case Damage::HitRecord:    std::memcpy(out.hitRecord, field, kRadioHitRecordSize); break;
            }
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool TookDamage(const GimmickType& type, const Snapshot& before, const Snapshot& after)
    {
        switch (type.damage)
        {
        case Damage::BreakRequest:
            return after.request != before.request && after.request != 0;
        case Damage::HitRecord:
            return std::memcmp(before.hitRecord, after.hitRecord, kRadioHitRecordSize) != 0;
        default:
            return after.life < before.life;
        }
    }

    std::uint8_t* Application()
    {
        const auto quarkTable = reinterpret_cast<GetQuarkSystemTable_t>(ResolveGameAddress(gAddr.Fox_GetQuarkSystemTable));
        std::uint8_t* table = quarkTable ? quarkTable() : nullptr;
        return table ? *reinterpret_cast<std::uint8_t* const*>(table + kQuarkTableApplication) : nullptr;
    }

    std::uint16_t PersonBehindSEH(std::uint16_t attacker)
    {
        __try
        {
            const std::uint16_t type = attacker >> kTypeShift;
            std::uint8_t* application = (type == kWalkerGearType || type == kVehicleType) ? Application() : nullptr;
            if (!application)
                return attacker;
            if (type == kWalkerGearType)
            {
                void* walkerGears = *reinterpret_cast<void* const*>(application + kApplicationWalkers);
                if (!walkerGears)
                    return attacker;
                const auto getWalkerGear = (*reinterpret_cast<GetWalkerGear_t* const*>(walkerGears))[kGetWalkerGearSlot];
                const std::uint8_t* walker = getWalkerGear(walkerGears, attacker);
                return walker ? *reinterpret_cast<const std::uint16_t*>(walker + kWalkerGearPilot) : attacker;
            }
            void* vehicles = *reinterpret_cast<void* const*>(application + kApplicationVehicles);
            if (!vehicles)
                return attacker;
            const auto getVehicle = (*reinterpret_cast<GetVehicle_t* const*>(vehicles))[kGetVehicleSlot];
            std::int32_t result = -1;
            std::uint8_t* info = nullptr;
            const std::int32_t* found = getVehicle(vehicles, &result, &info, attacker & kIndexMask);
            if (!found || *found < 0 || !info)
                return attacker;
            return *reinterpret_cast<const std::uint16_t*>(info + kVehicleDriver);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return attacker;
        }
    }

    bool ReadDamagedSEH(const std::uint8_t* unit, const std::uint8_t* offense, Damaged& out)
    {
        __try
        {
            const std::uint64_t name = *reinterpret_cast<const std::uint64_t*>(unit + kUnitName);
            out.gimmickId = *reinterpret_cast<const std::uint16_t*>(unit + kUnitGameObjectId);
            out.locator   = static_cast<std::uint32_t>(name);
            out.dataSet   = static_cast<std::uint32_t>(name >> 32);
            out.attacker  = kNoGameObject;
            const std::uint8_t* data = offense ? *reinterpret_cast<const std::uint8_t* const*>(offense + kOffenseData) : nullptr;
            if (data)
                out.attacker = *reinterpret_cast<const std::uint16_t*>(data + kOffenseAttacker);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void AnnounceDamage(const std::uint8_t* unit, const void* offense)
    {
        Damaged damaged{};
        if (!ReadDamagedSEH(unit, static_cast<const std::uint8_t*>(offense), damaged))
            return;
        if (damaged.attacker != kNoGameObject)
            damaged.attacker = PersonBehindSEH(damaged.attacker);
        V_FrameWork::QueueMessage("GameObject", "DamageGimmick", damaged.gimmickId, damaged.locator, damaged.dataSet,
                                  damaged.attacker);
    }

    std::uint32_t RunExec(std::size_t index, void* callback, void* offense, void* defense, const void* result, std::uint8_t* unit)
    {
        if (!unit || MissionCodeGuard::ShouldBypassHooks())
            return g_Orig[index](callback, offense, defense, result, unit);
        const GimmickType& type = kTypes[index];
        Snapshot before{};
        const bool known = TakeSnapshotSEH(type, unit, before);
        const std::uint32_t verdict = g_Orig[index](callback, offense, defense, result, unit);
        Snapshot after{};
        if (known && TakeSnapshotSEH(type, unit, after) && TookDamage(type, before, after))
            AnnounceDamage(unit, offense);
        return verdict;
    }

    template <std::size_t Index>
    std::uint32_t __fastcall hkExec(void* callback, void* offense, void* defense, const void* result, std::uint8_t* unit)
    {
        return RunExec(Index, callback, offense, defense, result, unit);
    }

    template <std::size_t... Indices>
    constexpr auto MakeDetours(std::index_sequence<Indices...>)
    {
        return std::array<Exec_t, sizeof...(Indices)>{ &hkExec<Indices>... };
    }

    const auto kDetours = MakeDetours(std::make_index_sequence<kTypeCount>{});

    bool InstallOne(std::size_t index)
    {
        const GimmickType& type = kTypes[index];
        void* target = ResolveGameAddress(gAddr.*(type.address));
        if (!target)
        {
            LogError("DamageGimmick: %s::Exec address missing for this build, no DamageGimmick for it", type.name);
            return true;
        }
        std::uint8_t mask[sizeof(type.entry)];
        std::memset(mask, 1, sizeof(mask));
        std::memset(mask + type.skipFrom, 0, type.skipCount);
        const HookEntry entry = CheckHookEntryMasked(target, type.entry, mask, sizeof(type.entry));
        if (entry == HookEntry::Unknown)
        {
            char desc[160] = {};
            DescribeFunctionEntry(target, desc, sizeof(desc));
            LogError("DamageGimmick: %s::Exec differs on this build (%s), no DamageGimmick for it", type.name, desc);
            return false;
        }
        if (entry == HookEntry::ForeignHook)
        {
            char what[96] = {};
            snprintf(what, sizeof(what), "DamageGimmick: %s::Exec", type.name);
            LogHookedOnTop(what, target);
        }
        if (!CreateAndEnableHook(target, reinterpret_cast<void*>(kDetours[index]), reinterpret_cast<void**>(&g_Orig[index])))
        {
            LogError("DamageGimmick: %s::Exec hook failed, no DamageGimmick for it", type.name);
            return false;
        }
        g_Target[index] = target;
        return true;
    }
}

bool Install_PermanentGimmickDefenseCallback_Exec_Hooks()
{
    bool ok = true;
    for (std::size_t index = 0; index < kTypeCount; ++index)
        ok = InstallOne(index) && ok;
    return ok;
}

void Uninstall_PermanentGimmickDefenseCallback_Exec_Hooks()
{
    for (std::size_t index = 0; index < kTypeCount; ++index)
    {
        if (g_Target[index])
            DisableAndRemoveHook(g_Target[index]);
        g_Target[index] = nullptr;
        g_Orig[index]   = nullptr;
    }
}
