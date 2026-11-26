#pragma once

#include <cstdint>

namespace GameObjectInstance
{
    enum class Step : std::uint8_t
    {
        NoManager,
        NoType,
        NoInstanceInfo,
        PastInstanceCount,
        NoObject,
        NoSoldierImpl,
        Fault,
        Found,
    };

    struct Found
    {
        std::uint8_t* object        = nullptr;
        std::uint8_t* typeObject    = nullptr;   // the type's own game object, e.g. Soldier2GameObjectImpl
        Step          step          = Step::NoManager;
        std::uint32_t instanceCount = 0;
        std::uint8_t  soldierStep   = 0;   // NoSoldierImpl: 1 not a soldier type, 2 no quark, 3 no holder, 4 query empty, 5 fault, 6 not the TppSoldier2 object
    };

    Found         Find(std::uint32_t gameObjectId);
    std::uint8_t* Of(std::uint32_t gameObjectId);   // what fox::gm::GetGameObjectInstanceWithId hands out, null when the index is past the type's instances
    Found         FindSoldier(std::uint32_t soldierId);   // object = the Soldier2Impl that runs every soldier
    bool          SoldierPosition(std::uint32_t soldierId, float position[3]);
}
