#include "pch.h"
#include "GameObjectLife.h"

#include <Windows.h>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "log.h"

namespace
{
    using GameObjectLife::Kind;
    using GameObjectLife::Values;

    constexpr std::uint32_t kSoldierType = 2;
    constexpr std::uint32_t kIndexMask   = 0x1FFu;
    constexpr unsigned      kTypeShift   = 9;
    constexpr std::uint32_t kTypeCount   = 128;
    constexpr std::uint64_t kStaleMs     = 500;    // the life updates run every frame, an older capture may be a freed controller

    constexpr std::size_t kSoldier_Count      = 0x80;
    constexpr std::size_t kSoldier_Stride     = 0x40;
    constexpr std::size_t kSoldier_Flags      = 0x12;
    constexpr std::size_t kSoldier_Life       = 0x1A;
    constexpr std::size_t kSoldier_MaxLife    = 0x1C;
    constexpr std::size_t kSoldier_Stamina    = 0x20;
    constexpr std::size_t kSoldier_MaxStamina = 0x22;
    constexpr std::size_t kSoldier_SleepKind  = 0x3D;   // 1 sleep, 0 faint, read when stamina reaches 0
    constexpr std::size_t kSoldier_Status     = 0x3E;

    // the life controllers' interface (what their Update gets): +0x08 records, +0x40 owner
    constexpr std::size_t kLc_Records = 0x08;
    constexpr std::size_t kLc_Owner   = 0x40;

    constexpr std::size_t kHostage_Count      = 0x50;
    constexpr std::size_t kHostage_OwnerType  = 0x168;
    constexpr std::size_t kHostage_OwnerFlags = 0x170;   // bit 0 set and bit 7 clear = this machine runs his life
    constexpr std::size_t kHostage_Stride     = 0x38;
    constexpr std::size_t kHostage_Life       = 0x0C;
    constexpr std::size_t kHostage_MaxLife    = 0x0E;
    constexpr std::size_t kHostage_Stamina    = 0x14;
    constexpr std::size_t kHostage_MaxStamina = 0x16;
    constexpr std::size_t kHostage_Status     = 0x33;

    constexpr std::size_t kAnimal_Count      = 0x48;
    constexpr std::size_t kAnimal_OwnerType  = 0x40;
    constexpr std::size_t kAnimal_Stride     = 0x20;
    constexpr std::size_t kAnimal_Life       = 0x08;
    constexpr std::size_t kAnimal_MaxLife    = 0x0A;
    constexpr std::size_t kAnimal_Stamina    = 0x0E;
    constexpr std::size_t kAnimal_MaxStamina = 0x10;
    constexpr std::size_t kAnimal_Status     = 0x1B;

    constexpr std::size_t kWalker_Count      = 0x50;
    constexpr std::size_t kWalker_OwnerType  = 0x08;
    constexpr std::size_t kWalker_OwnerFlags = 0xFC;    // bit 0 set and bit 3 clear = this machine runs its life
    constexpr std::size_t kWalker_Stride    = 0x3C;
    constexpr std::size_t kWalker_Life      = 0x00;   // u32
    constexpr std::size_t kWalker_MaxLife   = 0x04;   // u32
    constexpr std::size_t kWalker_Dead      = 0x2E;

    constexpr std::uint32_t kVehicleType         = 0x32;
    constexpr std::size_t   kVehicle_Interface   = 0x20;
    constexpr std::size_t   kVehicle_GetLifeSlot = 0x98;   // vtable slot of the interface, short[12] per vehicle, [0] = body
    constexpr std::size_t   kVehicle_Comms       = 0x30;   // 0x80 per vehicle, u32 status at +0x70, bit 2 = destroyed
    constexpr std::size_t   kVehicle_CommStride  = 0x80;
    constexpr std::size_t   kVehicle_CommStatus  = 0x70;
    constexpr std::uint32_t kVehicleDestroyed    = 0x4;
    constexpr std::size_t   kVehicle_Records     = 0x148;  // 0x40 per vehicle, max body life short at +0x14
    constexpr std::size_t   kVehicle_RecStride   = 0x40;
    constexpr std::size_t   kVehicle_MaxLife     = 0x14;
    constexpr std::size_t   kVehicle_Count       = 0x1E0;  // u16
    constexpr std::size_t   kVehicle_InUse       = 0x1C8;  // -> +8 u32 bits, one per vehicle

    struct Addrs
    {
        std::uintptr_t soldierUpdate;
        std::uintptr_t hostageUpdate;
        std::uintptr_t animalUpdate;
        std::uintptr_t walkerUpdate;
        std::uintptr_t vehicleReflectLife;
    };

    Addrs AddrsForBuild()
    {
        switch (gGameBuild)
        {
        case ::AddressSetRuntime::GameBuild::En_1_0_15_4:
        case ::AddressSetRuntime::GameBuild::En_1_0_15_4a:
            return {
                0x1414AA180ull, // tpp::gm::soldier::impl::LifeControllerImpl::Update
                0x140EC2990ull, // tpp::gm::hostage::impl::LifeControllerImpl::Update
                0x141348F10ull, // the animals' LifeControllerImpl::Update
                0x141762410ull, // the walker gears' LifeControllerImpl::Update
                0x140B33670ull, // tpp::gm::vehicle::impl::GameObjectImpl::ReflectLife
            };
        case ::AddressSetRuntime::GameBuild::Jp_1_0_15_4:
        case ::AddressSetRuntime::GameBuild::Jp_1_0_15_4a:
            return {
                0x1414AA190ull, // tpp::gm::soldier::impl::LifeControllerImpl::Update
                0x140EC29C0ull, // tpp::gm::hostage::impl::LifeControllerImpl::Update
                0x141348F20ull, // the animals' LifeControllerImpl::Update
                0x1417623D0ull, // the walker gears' LifeControllerImpl::Update
                0x140B33620ull, // tpp::gm::vehicle::impl::GameObjectImpl::ReflectLife
            };
        case ::AddressSetRuntime::GameBuild::En_1_0_15_3:
            return {
                0x1414AB390ull, // tpp::gm::soldier::impl::LifeControllerImpl::Update
                0x140EC2BB0ull, // tpp::gm::hostage::impl::LifeControllerImpl::Update
                0x149F14EA0ull, // the animals' LifeControllerImpl::Update
                0x14A7686D0ull, // the walker gears' LifeControllerImpl::Update
                0x140B33340ull, // tpp::gm::vehicle::impl::GameObjectImpl::ReflectLife
            };
        case ::AddressSetRuntime::GameBuild::Jp_1_0_15_3:
            return {
                0x1414AB360ull, // tpp::gm::soldier::impl::LifeControllerImpl::Update
                0x140EC2D30ull, // tpp::gm::hostage::impl::LifeControllerImpl::Update
                0x14A90E220ull, // the animals' LifeControllerImpl::Update
                0x14B1BBAA0ull, // the walker gears' LifeControllerImpl::Update
                0x140B32EC0ull, // tpp::gm::vehicle::impl::GameObjectImpl::ReflectLife
            };
        default:
            return {};
        }
    }

    using Update_t         = void(__fastcall*)(void* self);
    using GetLifeValues_t  = std::int16_t*(__fastcall*)(void* iface, std::uint32_t index);

    struct Capture
    {
        std::atomic<void*>         object{ nullptr };
        std::atomic<Kind>          kind{ Kind::None };
        std::atomic<std::uint64_t> tick{ 0 };
    };

    Addrs   g_Addrs{};
    Capture g_ByType[kTypeCount];

    Update_t g_OrigSoldierUpdate = nullptr;
    Update_t g_OrigHostageUpdate = nullptr;
    Update_t g_OrigAnimalUpdate  = nullptr;
    Update_t g_OrigWalkerUpdate  = nullptr;
    Update_t g_OrigReflectLife   = nullptr;

    template <typename T>
    T& At(std::uint8_t* base, std::size_t offset)
    {
        return *reinterpret_cast<T*>(base + offset);
    }

    void Store(std::uint32_t type, void* object, Kind kind)
    {
        if (type >= kTypeCount)
            return;
        Capture& c = g_ByType[type];
        c.object.store(object, std::memory_order_relaxed);
        c.kind.store(kind, std::memory_order_relaxed);
        c.tick.store(GetTickCount64(), std::memory_order_release);
    }

    void CaptureSeh(void* iface, std::size_t ownerTypeOffset, Kind kind)
    {
        __try
        {
            std::uint8_t* owner = At<std::uint8_t*>(static_cast<std::uint8_t*>(iface), kLc_Owner);
            if (owner)
                Store(At<std::uint16_t>(owner, ownerTypeOffset), iface, kind);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    void __fastcall hkSoldierUpdate(void* iface)
    {
        g_OrigSoldierUpdate(iface);
        if (!MissionCodeGuard::ShouldBypassHooks())
            Store(kSoldierType, iface, Kind::Soldier);
    }

    void __fastcall hkHostageUpdate(void* iface)
    {
        g_OrigHostageUpdate(iface);
        if (!MissionCodeGuard::ShouldBypassHooks())
            CaptureSeh(iface, kHostage_OwnerType, Kind::Hostage);
    }

    void __fastcall hkAnimalUpdate(void* iface)
    {
        g_OrigAnimalUpdate(iface);
        if (!MissionCodeGuard::ShouldBypassHooks())
            CaptureSeh(iface, kAnimal_OwnerType, Kind::Animal);
    }

    void __fastcall hkWalkerUpdate(void* iface)
    {
        g_OrigWalkerUpdate(iface);
        if (!MissionCodeGuard::ShouldBypassHooks())
            CaptureSeh(iface, kWalker_OwnerType, Kind::WalkerGear);
    }

    void __fastcall hkReflectLife(void* vehicles)
    {
        g_OrigReflectLife(vehicles);
        if (!MissionCodeGuard::ShouldBypassHooks())
            Store(kVehicleType, vehicles, Kind::Vehicle);
    }

    void* Captured(std::uint32_t type, Kind& kind)
    {
        kind = Kind::None;
        if (type >= kTypeCount)
            return nullptr;
        const Capture& c = g_ByType[type];
        if (GetTickCount64() - c.tick.load(std::memory_order_acquire) > kStaleMs)
            return nullptr;
        kind = c.kind.load(std::memory_order_relaxed);
        return c.object.load(std::memory_order_relaxed);
    }

    bool ReadSoldierSeh(std::uint8_t* rec, Values& out)
    {
        __try
        {
            out.life       = At<std::uint16_t>(rec, kSoldier_Life);
            out.maxLife    = At<std::uint16_t>(rec, kSoldier_MaxLife);
            out.stamina    = At<std::uint16_t>(rec, kSoldier_Stamina);
            out.maxStamina = At<std::uint16_t>(rec, kSoldier_MaxStamina);
            out.status     = At<std::uint8_t>(rec, kSoldier_Status);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    std::uint8_t* RecordSeh(std::uint8_t* iface, std::size_t countOffset, std::size_t stride, std::uint32_t index)
    {
        __try
        {
            if (index >= At<std::uint32_t>(iface, countOffset))
                return nullptr;
            std::uint8_t* records = At<std::uint8_t*>(iface, kLc_Records);
            return records ? records + index * stride : nullptr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    std::uint8_t* SoldierRecord(std::uint32_t gameObjectId)
    {
        Kind kind = Kind::None;
        std::uint8_t* iface = static_cast<std::uint8_t*>(Captured(kSoldierType, kind));
        return iface ? RecordSeh(iface, kSoldier_Count, kSoldier_Stride, gameObjectId & kIndexMask) : nullptr;
    }

    bool VehicleInUse(std::uint8_t* vehicles, std::uint32_t index)
    {
        std::uint8_t* inUse = At<std::uint8_t*>(vehicles, kVehicle_InUse);
        const std::uint32_t* bits = inUse ? At<const std::uint32_t*>(inUse, 8) : nullptr;
        return bits && ((bits[index >> 5] >> (index & 31)) & 1u) != 0;
    }

    bool ReadCapturedSeh(std::uint8_t* object, Kind kind, std::uint32_t index, Values& out)
    {
        __try
        {
            std::uint8_t* rec = nullptr;
            switch (kind)
            {
            case Kind::Hostage:
                rec = RecordSeh(object, kHostage_Count, kHostage_Stride, index);
                if (!rec)
                    return false;
                out.life       = At<std::uint16_t>(rec, kHostage_Life);
                out.maxLife    = At<std::uint16_t>(rec, kHostage_MaxLife);
                out.stamina    = At<std::uint16_t>(rec, kHostage_Stamina);
                out.maxStamina = At<std::uint16_t>(rec, kHostage_MaxStamina);
                out.status     = At<std::uint8_t>(rec, kHostage_Status);
                return true;

            case Kind::Animal:
                rec = RecordSeh(object, kAnimal_Count, kAnimal_Stride, index);
                if (!rec)
                    return false;
                out.life       = At<std::uint16_t>(rec, kAnimal_Life);
                out.maxLife    = At<std::uint16_t>(rec, kAnimal_MaxLife);
                out.stamina    = At<std::uint16_t>(rec, kAnimal_Stamina);
                out.maxStamina = At<std::uint16_t>(rec, kAnimal_MaxStamina);
                out.status     = At<std::uint8_t>(rec, kAnimal_Status);
                return true;

            case Kind::WalkerGear:
                rec = RecordSeh(object, kWalker_Count, kWalker_Stride, index);
                if (!rec)
                    return false;
                out.life    = At<std::uint32_t>(rec, kWalker_Life);
                out.maxLife = At<std::uint32_t>(rec, kWalker_MaxLife);
                out.status  = At<std::uint8_t>(rec, kWalker_Dead) ? GameObjectLife::Status_Dead : GameObjectLife::Status_Alive;
                return true;

            case Kind::Vehicle:
            {
                if (index >= At<std::uint16_t>(object, kVehicle_Count) || !VehicleInUse(object, index))
                    return false;
                std::uint8_t* iface = object + kVehicle_Interface;
                void** vtbl = At<void**>(iface, 0);
                const auto getLife = reinterpret_cast<GetLifeValues_t>(vtbl[kVehicle_GetLifeSlot / sizeof(void*)]);
                const std::int16_t* lives = getLife ? getLife(iface, index) : nullptr;
                std::uint8_t* records = At<std::uint8_t*>(object, kVehicle_Records);
                std::uint8_t* comms = At<std::uint8_t*>(object, kVehicle_Comms);
                if (!lives || !records || !comms)
                    return false;
                const std::int16_t life = lives[0];
                const std::int16_t max = At<std::int16_t>(records + index * kVehicle_RecStride, kVehicle_MaxLife);
                const std::uint32_t status = At<std::uint32_t>(comms + index * kVehicle_CommStride, kVehicle_CommStatus);
                out.life    = life > 0 ? static_cast<std::uint32_t>(life) : 0u;
                out.maxLife = max > 0 ? static_cast<std::uint32_t>(max) : 0u;
                out.status  = (status & kVehicleDestroyed) ? GameObjectLife::Status_Dead : GameObjectLife::Status_Alive;
                return true;
            }

            default:
                return false;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    struct Field16
    {
        std::size_t value;
        std::size_t max;
    };

    // writes the current value the next life update reads, clamped to the max; never the status, so the game reacts as usual
    bool WriteU16Seh(std::uint8_t* rec, Field16 field, std::size_t statusOffset, std::uint32_t value, const char** reason)
    {
        __try
        {
            if (At<std::uint8_t>(rec, statusOffset) == GameObjectLife::Status_Dead)
            {
                *reason = "it is dead";
                return false;
            }
            const std::uint16_t max = At<std::uint16_t>(rec, field.max);
            At<std::uint16_t>(rec, field.value) = static_cast<std::uint16_t>(value > max ? max : value);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            *reason = "its life data is unreadable";
            return false;
        }
    }

    bool RunsLocallySeh(std::uint8_t* iface, std::size_t flagsOffset, std::uint32_t remoteBit)
    {
        __try
        {
            std::uint8_t* owner = At<std::uint8_t*>(iface, kLc_Owner);
            if (!owner)
                return false;
            const std::uint32_t flags = At<std::uint32_t>(owner, flagsOffset);
            return (flags & 1u) != 0 && (flags & remoteBit) == 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool WriteWalkerLifeSeh(std::uint8_t* rec, std::uint32_t value, const char** reason)
    {
        __try
        {
            if (At<std::uint8_t>(rec, kWalker_Dead))
            {
                *reason = "it is destroyed";
                return false;
            }
            const std::uint32_t max = At<std::uint32_t>(rec, kWalker_MaxLife);
            At<std::uint32_t>(rec, kWalker_Life) = value > max ? max : value;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            *reason = "its life data is unreadable";
            return false;
        }
    }

    // the same write as the vanilla vehicle command SetBodyLife
    bool WriteVehicleLifeSeh(std::uint8_t* vehicles, std::uint32_t index, std::uint32_t value, const char** reason)
    {
        __try
        {
            if (index >= At<std::uint16_t>(vehicles, kVehicle_Count) || !VehicleInUse(vehicles, index))
            {
                *reason = "no vehicle has this id";
                return false;
            }
            std::uint8_t* comms = At<std::uint8_t*>(vehicles, kVehicle_Comms);
            std::uint8_t* records = At<std::uint8_t*>(vehicles, kVehicle_Records);
            if (!comms || !records)
            {
                *reason = "its life data is unreadable";
                return false;
            }
            if (At<std::uint32_t>(comms + index * kVehicle_CommStride, kVehicle_CommStatus) & kVehicleDestroyed)
            {
                *reason = "it is destroyed";
                return false;
            }
            std::uint8_t* iface = vehicles + kVehicle_Interface;
            void** vtbl = At<void**>(iface, 0);
            const auto getLife = reinterpret_cast<GetLifeValues_t>(vtbl[kVehicle_GetLifeSlot / sizeof(void*)]);
            std::int16_t* lives = getLife ? getLife(iface, index) : nullptr;
            if (!lives)
            {
                *reason = "its life data is unreadable";
                return false;
            }
            const std::int16_t max = At<std::int16_t>(records + index * kVehicle_RecStride, kVehicle_MaxLife);
            const std::uint32_t limit = max > 0 ? static_cast<std::uint32_t>(max) : 0u;
            lives[0] = static_cast<std::int16_t>(value > limit ? limit : value);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            *reason = "its life data is unreadable";
            return false;
        }
    }

    bool SetSoldierSleepKindSeh(std::uint8_t* rec, bool sleep)
    {
        __try
        {
            At<std::uint8_t>(rec, kSoldier_SleepKind) = sleep ? 1 : 0;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    enum class What { Life, Stamina };

    bool Write(std::uint32_t gameObjectId, What what, std::uint32_t value, bool sleep, const char** reason)
    {
        static const char* unused = nullptr;
        if (!reason)
            reason = &unused;
        if (!g_Addrs.soldierUpdate)
        {
            *reason = "not ported to this game version";
            return false;
        }

        const std::uint32_t type = (gameObjectId >> kTypeShift) & (kTypeCount - 1);
        const std::uint32_t index = gameObjectId & kIndexMask;
        const bool life = what == What::Life;

        if (type == kSoldierType)
        {
            std::uint8_t* rec = SoldierRecord(gameObjectId);
            if (!rec)
            {
                *reason = "no soldier has this id";
                return false;
            }
            if (!life && value == 0 && !SetSoldierSleepKindSeh(rec, sleep))
            {
                *reason = "his life data is unreadable";
                return false;
            }
            return WriteU16Seh(rec, life ? Field16{ kSoldier_Life, kSoldier_MaxLife } : Field16{ kSoldier_Stamina, kSoldier_MaxStamina },
                               kSoldier_Status, value, reason);
        }

        Kind kind = Kind::None;
        std::uint8_t* object = static_cast<std::uint8_t*>(Captured(type, kind));
        if (!object)
        {
            *reason = "it has no life the game updates now";
            return false;
        }

        switch (kind)
        {
        case Kind::Hostage:
        {
            if (!RunsLocallySeh(object, kHostage_OwnerFlags, 0x80u))
            {
                *reason = "another machine runs its life";
                return false;
            }
            std::uint8_t* rec = RecordSeh(object, kHostage_Count, kHostage_Stride, index);
            if (!rec)
            {
                *reason = "no hostage has this id";
                return false;
            }
            return WriteU16Seh(rec, life ? Field16{ kHostage_Life, kHostage_MaxLife } : Field16{ kHostage_Stamina, kHostage_MaxStamina },
                               kHostage_Status, value, reason);
        }
        case Kind::Animal:
        {
            std::uint8_t* rec = RecordSeh(object, kAnimal_Count, kAnimal_Stride, index);
            if (!rec)
            {
                *reason = "no animal has this id";
                return false;
            }
            return WriteU16Seh(rec, life ? Field16{ kAnimal_Life, kAnimal_MaxLife } : Field16{ kAnimal_Stamina, kAnimal_MaxStamina },
                               kAnimal_Status, value, reason);
        }
        case Kind::WalkerGear:
        {
            if (!life)
            {
                *reason = "walker gears have no stamina";
                return false;
            }
            if (!RunsLocallySeh(object, kWalker_OwnerFlags, 0x8u))
            {
                *reason = "another machine runs its life";
                return false;
            }
            std::uint8_t* rec = RecordSeh(object, kWalker_Count, kWalker_Stride, index);
            if (!rec)
            {
                *reason = "no walker gear has this id";
                return false;
            }
            return WriteWalkerLifeSeh(rec, value, reason);
        }
        case Kind::Vehicle:
            if (!life)
            {
                *reason = "vehicles have no stamina";
                return false;
            }
            return WriteVehicleLifeSeh(object, index, value, reason);
        default:
            *reason = "it has no life the game updates now";
            return false;
        }
    }

    bool Hook(std::uintptr_t address, void* detour, Update_t* original, const char* what)
    {
        if (!address)
            return false;
        if (CreateAndEnableHook(ResolveGameAddress(address), detour, reinterpret_cast<void**>(original)))
            return true;
        LogError("GameObjectLife: %s hook failed, its bars and life reads are off", what);
        return false;
    }
}

namespace GameObjectLife
{
    bool IsSupportedOnThisBuild()
    {
        return g_Addrs.soldierUpdate != 0;
    }

    Kind KindOf(std::uint32_t gameObjectId)
    {
        const std::uint32_t type = (gameObjectId >> kTypeShift) & (kTypeCount - 1);
        if (type == kSoldierType)
            return g_Addrs.soldierUpdate ? Kind::Soldier : Kind::None;
        Kind kind = Kind::None;
        return Captured(type, kind) ? kind : Kind::None;
    }

    bool Read(std::uint32_t gameObjectId, Values& out)
    {
        out = Values{};
        const std::uint32_t type = (gameObjectId >> kTypeShift) & (kTypeCount - 1);
        const std::uint32_t index = gameObjectId & kIndexMask;

        if (type == kSoldierType)
        {
            std::uint8_t* rec = SoldierRecord(gameObjectId);
            out.kind = Kind::Soldier;
            return rec && ReadSoldierSeh(rec, out) && out.maxLife != 0;
        }

        Kind kind = Kind::None;
        void* object = Captured(type, kind);
        if (!object)
            return false;
        out.kind = kind;
        return ReadCapturedSeh(static_cast<std::uint8_t*>(object), kind, index, out) && out.maxLife != 0;
    }

    bool SetLife(std::uint32_t gameObjectId, std::uint32_t life, const char** reason)
    {
        return Write(gameObjectId, What::Life, life, false, reason);
    }

    bool SetStamina(std::uint32_t gameObjectId, std::uint32_t stamina, bool sleep, const char** reason)
    {
        return Write(gameObjectId, What::Stamina, stamina, sleep, reason);
    }
}

bool Install_GameObjectLife_Hooks()
{
    g_Addrs = AddrsForBuild();
    if (!g_Addrs.soldierUpdate)
    {
        LogError("GameObjectLife: not ported to %s yet, life commands and head gauges do nothing", GetGameBuildName(gGameBuild));
        return false;
    }

    Hook(g_Addrs.soldierUpdate, reinterpret_cast<void*>(&hkSoldierUpdate), &g_OrigSoldierUpdate, "soldier life");
    Hook(g_Addrs.hostageUpdate, reinterpret_cast<void*>(&hkHostageUpdate), &g_OrigHostageUpdate, "hostage life");
    Hook(g_Addrs.animalUpdate, reinterpret_cast<void*>(&hkAnimalUpdate), &g_OrigAnimalUpdate, "animal life");
    Hook(g_Addrs.walkerUpdate, reinterpret_cast<void*>(&hkWalkerUpdate), &g_OrigWalkerUpdate, "walker gear life");
    Hook(g_Addrs.vehicleReflectLife, reinterpret_cast<void*>(&hkReflectLife), &g_OrigReflectLife, "vehicle life");
    return true;
}

bool Uninstall_GameObjectLife_Hooks()
{
    const std::uintptr_t targets[] = { g_Addrs.soldierUpdate, g_Addrs.hostageUpdate, g_Addrs.animalUpdate, g_Addrs.walkerUpdate, g_Addrs.vehicleReflectLife };
    for (const std::uintptr_t address : targets)
        if (address)
            DisableAndRemoveHook(ResolveGameAddress(address));
    for (Capture& c : g_ByType)
        c.tick.store(0, std::memory_order_relaxed);
    return true;
}
