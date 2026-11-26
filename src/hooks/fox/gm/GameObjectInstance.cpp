#include <pch.h>
#include "GameObjectInstance.h"

#include <Windows.h>
#include <cstddef>
#include <cstdint>

#include "../../../core/AddressSet.h"
#include "../../../core/HookUtils.h"

namespace
{
    using GetSharedInstance_t = void*(__fastcall*)();
    using GetTypeObject_t     = void*(__fastcall*)(void* manager, std::uint16_t gameObjectId);
    using GetInstanceInfo_t   = void*(__fastcall*)(void* typeObject, void* out);
    using QueryPart_t         = std::uint8_t*(__fastcall*)(void* parts, std::uint64_t nameHash);

    constexpr std::size_t   kManagerGetTypeSlot  = 0x20;
    constexpr std::size_t   kTypeGetInstanceInfo = 0x40;
    constexpr std::size_t   kTypeWrapperOffset   = 0x08;
    constexpr std::size_t   kWrapperObjectOffset = 0x28;
    constexpr std::uint16_t kSoldierType         = 2;
    constexpr std::size_t   kTypeIdOffset        = 0x1C;
    constexpr std::size_t   kGameObjectParts     = 0x40;   // a quark on Soldier2GameObjectImpl, the type object itself
    constexpr std::size_t   kPartsHolder         = 0x10;
    constexpr std::size_t   kPartsQuerySlot      = 0x18;
    constexpr std::uint64_t kTppSoldier2         = 0x3FEF1D0298AAull;   // "TppSoldier2"
    constexpr std::size_t   kQueryIntoPart       = 0x0C;   // the query answers 0xC bytes into the soldier's quark part
    constexpr std::size_t   kPartInSoldier2Impl  = 0x20;   // and that part sits 0x20 into Soldier2Impl
    constexpr std::size_t   kSoldier2ImplName    = 0x08;   // Soldier2Impl keeps its "TppSoldier2" hash here
    constexpr std::size_t   kSoldierCount        = 0x28;   // Soldier2Impl view, its Soldier2 part starts at +0x20
    constexpr std::size_t   kSoldierTransforms   = 0x38;
    constexpr std::size_t   kTransformPositions  = 0x20;   // one Vector4 per soldier

    GetSharedInstance_t GameObjectManager()
    {
        static const GetSharedInstance_t getShared = gAddr.Fox_GameObjectManager_GetSharedInstance
            ? reinterpret_cast<GetSharedInstance_t>(ResolveGameAddress(gAddr.Fox_GameObjectManager_GetSharedInstance))
            : nullptr;
        return getShared;
    }

    void InstanceSeh(GetSharedInstance_t getShared, std::uint32_t gameObjectId, GameObjectInstance::Found& found)
    {
        __try
        {
            void* manager = getShared();
            if (!manager)
            {
                found.step = GameObjectInstance::Step::NoManager;
                return;
            }
            void** vtbl = *reinterpret_cast<void***>(manager);
            auto* typeObject = static_cast<std::uint8_t*>(reinterpret_cast<GetTypeObject_t>(vtbl[kManagerGetTypeSlot / sizeof(void*)])(manager, static_cast<std::uint16_t>(gameObjectId)));
            found.typeObject = typeObject;
            if (!typeObject)
            {
                found.step = GameObjectInstance::Step::NoType;
                return;
            }
            alignas(16) std::uint8_t instances[16] = {};
            void** typeVtbl = *reinterpret_cast<void***>(typeObject);
            auto* info = static_cast<std::uint8_t*>(reinterpret_cast<GetInstanceInfo_t>(typeVtbl[kTypeGetInstanceInfo / sizeof(void*)])(typeObject, instances));
            if (!info)
            {
                found.step = GameObjectInstance::Step::NoInstanceInfo;
                return;
            }
            found.instanceCount = *reinterpret_cast<std::uint32_t*>(info + 4);   // instance count of the type
            if ((gameObjectId & 0x1FFu) >= found.instanceCount)
            {
                found.step = GameObjectInstance::Step::PastInstanceCount;
                return;
            }
            auto* wrapper = *reinterpret_cast<std::uint8_t**>(typeObject + kTypeWrapperOffset);
            found.object = wrapper ? *reinterpret_cast<std::uint8_t**>(wrapper + kWrapperObjectOffset) : nullptr;
            found.step = found.object ? GameObjectInstance::Step::Found : GameObjectInstance::Step::NoObject;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            found.step = GameObjectInstance::Step::Fault;
        }
    }

    std::uint8_t* Soldier2ImplSeh(std::uint8_t* gameObject, std::uint8_t& step)
    {
        __try
        {
            step = 1;
            if (*reinterpret_cast<std::uint16_t*>(gameObject + kTypeIdOffset) != kSoldierType)
                return nullptr;
            step = 2;
            auto* parts = *reinterpret_cast<std::uint8_t**>(gameObject + kGameObjectParts);
            if (!parts)
                return nullptr;
            step = 3;
            auto* holder = *reinterpret_cast<std::uint8_t**>(parts + kPartsHolder);
            if (!holder)
                return nullptr;
            step = 4;
            void** vtbl = *reinterpret_cast<void***>(holder);
            std::uint8_t* part = reinterpret_cast<QueryPart_t>(vtbl[kPartsQuerySlot / sizeof(void*)])(holder, kTppSoldier2);
            if (!part)
                return nullptr;
            step = 6;
            std::uint8_t* soldiers = part - kQueryIntoPart - kPartInSoldier2Impl;
            if (*reinterpret_cast<std::uint64_t*>(soldiers + kSoldier2ImplName) != kTppSoldier2)
                return nullptr;
            step = 0;
            return soldiers;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            step = 5;
            return nullptr;
        }
    }

    bool SoldierPositionSeh(std::uint8_t* soldiers, std::uint32_t index, float position[3])
    {
        __try
        {
            if (index >= *reinterpret_cast<std::uint32_t*>(soldiers + kSoldierCount))
                return false;
            auto* transforms = *reinterpret_cast<std::uint8_t**>(soldiers + kSoldierTransforms);
            auto* positions  = transforms ? *reinterpret_cast<const float**>(transforms + kTransformPositions) : nullptr;
            if (!positions)
                return false;
            for (int i = 0; i < 3; ++i)
                position[i] = positions[static_cast<std::size_t>(index) * 4 + i];
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }
}

namespace GameObjectInstance
{
    Found Find(std::uint32_t gameObjectId)
    {
        Found found;
        const GetSharedInstance_t getShared = GameObjectManager();
        if (getShared)
            InstanceSeh(getShared, gameObjectId, found);
        return found;
    }

    std::uint8_t* Of(std::uint32_t gameObjectId)
    {
        return Find(gameObjectId).object;
    }

    Found FindSoldier(std::uint32_t soldierId)
    {
        Found found = Find(soldierId);
        if (found.step != Step::Found)
            return found;
        found.object = Soldier2ImplSeh(found.typeObject, found.soldierStep);
        if (!found.object)
            found.step = Step::NoSoldierImpl;
        return found;
    }

    bool SoldierPosition(std::uint32_t soldierId, float position[3])
    {
        if ((soldierId >> 9) != kSoldierType)
            return false;
        std::uint8_t* soldiers = FindSoldier(soldierId).object;
        return soldiers && SoldierPositionSeh(soldiers, soldierId & 0x1FFu, position);
    }
}
