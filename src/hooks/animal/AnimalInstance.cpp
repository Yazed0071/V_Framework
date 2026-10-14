#include "pch.h"
#include "AnimalInstance.h"

#include <Windows.h>
#include <cstddef>
#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"
#include "../fox/gm/GameObjectPosition.h"

namespace
{
    using AnimalInstance::Presence;

    constexpr std::uint32_t kTypeShift          = 9;
    constexpr std::uint32_t kIndexMask          = 0x1FFu;
    constexpr std::uint32_t kFirstAnimal        = 28;   // goat
    constexpr std::uint32_t kLastAnimal         = 37;   // bear
    constexpr std::size_t   kManagerGetTypeSlot = 0x20;
    constexpr std::size_t   kTypeIdOffset       = 0x1C;
    constexpr std::size_t   kTypeWrapperOffset  = 0x08;
    constexpr std::size_t   kWrapperTypeOffset  = 0x20;

    constexpr std::size_t kQuark_Plugin      = 0x10;
    constexpr std::size_t kPlugin_Animal     = 0x20;   // the animal object, its first field after the vtable is the instance block
    constexpr std::size_t kAnimal_State      = 0x08;
    constexpr std::size_t kState_Active      = 0x00;   // byte bitset, one bit per animal
    constexpr std::size_t kState_Realized    = 0x08;   // byte bitset, one bit per animal
    constexpr std::size_t kState_Positions   = 0x20;   // x, y, z, rotY per animal
    constexpr std::size_t kState_Spawned     = 0x28;   // u16 animal index per spawned slot
    constexpr std::size_t kState_Count       = 0x40;
    constexpr std::size_t kState_SpawnedCount = 0x44;

    constexpr std::size_t kDamage_State          = 0x30;   // the damage request controller keeps the same instance block
    constexpr std::size_t kDamage_Count          = 0x78;
    constexpr std::size_t kDamage_RequestSlot    = 0x08;   // vtable slot of RequestDamage(index, info)
    constexpr std::uint16_t kLastAttackId        = 0x1BC;

    constexpr std::uint8_t kDamageHeadShot   = 0x1;
    constexpr std::uint8_t kDamageOneHitKill = 0x2;
    constexpr std::uint8_t kDamageByAttacker = 0x4;

    struct RequestDamageInfo
    {
        std::uint16_t attackId;
        std::uint16_t attackerId;
        std::uint16_t boneIndex;
        std::uint8_t  bodyHit;
        std::uint8_t  flags;
    };
    static_assert(sizeof(RequestDamageInfo) == 8, "the game copies 8 bytes");

    using GetSharedInstance_t = void*(__fastcall*)();
    using GetTypeObject_t     = void*(__fastcall*)(void* manager, std::uint16_t gameObjectId);
    using RequestDamage_t     = void(__fastcall*)(void* controller, std::uint32_t index, const RequestDamageInfo* info);

    struct Resolved
    {
        std::uint8_t* animal = nullptr;
        std::uint8_t* state  = nullptr;
    };

    std::size_t QuarkMemberFor(std::uint32_t type)
    {
        switch (type)
        {
        case 28:   // goat
        case 29:   // nubian
        case 34:   // zebra
            return 0x158;
        case 35:   // wolf
        case 36:   // jackal
            return 0xC0;
        case 30:   // critter bird
        case 31:   // stork
        case 32:   // eagle
        case 33:   // rat
        case 37:   // bear
            return 0x48;
        default:
            return 0;
        }
    }

    std::size_t DamageControllerFor(std::uint32_t type)
    {
        switch (type)
        {
        case 28:   // goat
        case 29:   // nubian
        case 34:   // zebra
            return 0x1E0;
        case 35:   // wolf
        case 36:   // jackal
            return 0x160;
        case 37:   // bear
            return 0x1C0;
        default:
            return 0;   // birds and rats have none
        }
    }

    template <typename T>
    T At(const std::uint8_t* base, std::size_t offset)
    {
        return *reinterpret_cast<const T*>(base + offset);
    }

    GetSharedInstance_t GameObjectManager()
    {
        static const GetSharedInstance_t getShared = gAddr.Fox_GameObjectManager_GetSharedInstance
            ? reinterpret_cast<GetSharedInstance_t>(ResolveGameAddress(gAddr.Fox_GameObjectManager_GetSharedInstance))
            : nullptr;
        return getShared;
    }

    bool ResolveSeh(std::uint32_t gameObjectId, Resolved& out)
    {
        const std::uint32_t type = gameObjectId >> kTypeShift;
        const std::size_t quarkMember = QuarkMemberFor(type);
        const GetSharedInstance_t getShared = GameObjectManager();
        if (!quarkMember || !getShared)
            return false;
        __try
        {
            void* manager = getShared();
            if (!manager)
                return false;
            void** managerVtbl = *reinterpret_cast<void***>(manager);
            const auto getType = reinterpret_cast<GetTypeObject_t>(managerVtbl[kManagerGetTypeSlot / sizeof(void*)]);
            const std::uint8_t* typeObject = static_cast<const std::uint8_t*>(getType(manager, static_cast<std::uint16_t>(gameObjectId)));
            if (!typeObject || At<std::uint16_t>(typeObject, kTypeIdOffset) != type)
                return false;
            const std::uint8_t* wrapper = At<const std::uint8_t*>(typeObject, kTypeWrapperOffset);
            if (!wrapper || At<const std::uint8_t*>(wrapper, kWrapperTypeOffset) != typeObject)
                return false;

            const std::uint8_t* quark = At<const std::uint8_t*>(typeObject, quarkMember);
            std::uint8_t* plugin = quark ? At<std::uint8_t*>(quark, kQuark_Plugin) : nullptr;
            if (!plugin)
                return false;
            out.animal = plugin + kPlugin_Animal;
            out.state = At<std::uint8_t*>(out.animal, kAnimal_State);
            if (!out.state)
                return false;
            const std::uint32_t count = At<std::uint32_t>(out.state, kState_Count);
            return (gameObjectId & kIndexMask) < count && count <= kIndexMask + 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    // the stored position (not spawned) or the spawned list must agree with what the game just answered
    bool StateAgreesSeh(const std::uint8_t* state, std::uint32_t index, const float* reported)
    {
        __try
        {
            const std::uint8_t bit = static_cast<std::uint8_t>(1u << (index & 7));
            const std::uint8_t* realized = At<const std::uint8_t*>(state, kState_Realized);
            if ((realized[index >> 3] & bit) == 0)
            {
                const float* stored = At<const float*>(state, kState_Positions) + index * 4;
                return stored[0] == reported[0] && stored[1] == reported[1] && stored[2] == reported[2];
            }
            const std::uint16_t* spawned = At<const std::uint16_t*>(state, kState_Spawned);
            const std::uint32_t spawnedCount = At<std::uint32_t>(state, kState_SpawnedCount);
            for (std::uint32_t i = 0; i < spawnedCount; ++i)
            {
                if (spawned[i] == index)
                    return true;
            }
            return false;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool IsActiveSeh(const std::uint8_t* state, std::uint32_t index, bool& active)
    {
        __try
        {
            const std::uint8_t* bits = At<const std::uint8_t*>(state, kState_Active);
            active = (bits[index >> 3] & (1u << (index & 7))) != 0;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    Presence CheckResolved(std::uint32_t gameObjectId, const float* reported, Resolved& resolved)
    {
        const std::uint32_t index = gameObjectId & kIndexMask;
        bool active = false;
        if (!ResolveSeh(gameObjectId, resolved) || !StateAgreesSeh(resolved.state, index, reported) || !IsActiveSeh(resolved.state, index, active))
            return Presence::Unknown;
        return active ? Presence::InWorld : Presence::Gone;
    }

    bool RequestDamageSeh(const Resolved& resolved, std::size_t controllerMember, std::uint32_t index, const RequestDamageInfo& info)
    {
        __try
        {
            std::uint8_t* controller = At<std::uint8_t*>(resolved.animal, controllerMember);
            if (!controller || At<std::uint8_t*>(controller, kDamage_State) != resolved.state)
                return false;
            if (At<std::uint32_t>(controller, kDamage_Count) != At<std::uint32_t>(resolved.state, kState_Count))
                return false;
            void** vtbl = *reinterpret_cast<void***>(controller);
            const auto requestDamage = reinterpret_cast<RequestDamage_t>(vtbl[kDamage_RequestSlot / sizeof(void*)]);
            requestDamage(controller, index, &info);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }
}

namespace AnimalInstance
{
    bool IsAnimal(std::uint32_t gameObjectId)
    {
        const std::uint32_t type = gameObjectId >> kTypeShift;
        return gameObjectId <= 0xFFFFu && type >= kFirstAnimal && type <= kLastAnimal;
    }

    Presence Check(std::uint32_t gameObjectId, const float reportedPosition[3])
    {
        if (!IsAnimal(gameObjectId))
            return Presence::Unknown;

        Resolved resolved;
        const Presence presence = CheckResolved(gameObjectId, reportedPosition, resolved);
        if (presence == Presence::Unknown)
        {
            static bool s_logged = false;
            if (!s_logged)
            {
                s_logged = true;
                LogWarn("GetPosition: can't tell if animal 0x%04X is in the world, its position is returned anyway", gameObjectId);
            }
        }
        return presence;
    }

    bool AddDamage(std::uint32_t gameObjectId, const DamageRequest& request, const char** reason)
    {
        static const char* unused = nullptr;
        if (!reason)
            reason = &unused;

        const std::size_t controllerMember = DamageControllerFor(gameObjectId >> kTypeShift);
        if (!IsAnimal(gameObjectId) || !controllerMember)
        {
            *reason = "only goats, sheep, wolves, jackals, zebras and bears take it";
            return false;
        }
        if (request.attackId == 0 || request.attackId > kLastAttackId)
        {
            *reason = "attackId must be a TppDamage.ATK_ value";
            return false;
        }

        float position[3] = {};
        if (!GameObjectPosition::Read(gameObjectId, position))
        {
            *reason = "no animal has this id";
            return false;
        }
        Resolved resolved;
        switch (CheckResolved(gameObjectId, position, resolved))
        {
        case Presence::Gone:
            *reason = "it isn't in the world";
            return false;
        case Presence::Unknown:
            *reason = "its data can't be read on this game version";
            return false;
        default:
            break;
        }

        RequestDamageInfo info{};
        info.attackId = request.attackId;
        info.attackerId = request.attackerId;
        info.boneIndex = 0;
        info.bodyHit = request.headShot ? 0 : 1;
        info.flags = static_cast<std::uint8_t>((request.headShot ? kDamageHeadShot : 0)
                                             | (request.oneHitKill ? kDamageOneHitKill : 0)
                                             | (request.attackerId != 0xFFFF ? kDamageByAttacker : 0));
        if (!RequestDamageSeh(resolved, controllerMember, gameObjectId & kIndexMask, info))
        {
            *reason = "its damage controller can't be read on this game version";
            return false;
        }
        return true;
    }
}
