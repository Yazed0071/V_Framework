#include "pch.h"
#include "GameObjectPosition.h"

#include <Windows.h>
#include <cstddef>
#include <cstdint>

#include "../../../core/AddressSet.h"
#include "../../../core/HookUtils.h"

namespace
{
    constexpr std::uint64_t kPositionSignal       = 0xD0A5D0C7E373ull;   // hash of the position query signal (real name unknown)
    constexpr std::size_t   kQuarkTable_GameObjects = 0x60;
    constexpr std::size_t   kGameObjects_SendSignal = 0x38;              // vtable slot, answers right away

    struct alignas(16) PositionSignal
    {
        std::uint64_t id;
        std::uint64_t unused;
        float         rotation[4];   // quaternion
        float         position[4];
        std::uint8_t  answered;      // the game object sets it to 1
        std::uint8_t  pad[15];
    };

    using GetQuarkSystemTable_t = std::uint8_t*(__fastcall*)();
    using SendSignal_t = std::int32_t*(__fastcall*)(void* gameObjects, std::int32_t* result, std::uint32_t gameObjectId, PositionSignal* signal);

    bool SendSeh(GetQuarkSystemTable_t getQuarkSystemTable, std::uint32_t gameObjectId, PositionSignal& signal)
    {
        __try
        {
            std::uint8_t* quarkTable = getQuarkSystemTable();
            void* gameObjects = quarkTable ? *reinterpret_cast<void**>(quarkTable + kQuarkTable_GameObjects) : nullptr;
            if (!gameObjects)
                return false;
            void** vtbl = *reinterpret_cast<void***>(gameObjects);
            const auto sendSignal = reinterpret_cast<SendSignal_t>(vtbl[kGameObjects_SendSignal / sizeof(void*)]);
            std::int32_t result = -1;
            sendSignal(gameObjects, &result, gameObjectId & 0xFFFFu, &signal);
            return result >= 0 && signal.answered == 1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }
}

namespace GameObjectPosition
{
    bool Read(std::uint32_t gameObjectId, float position[3])
    {
        static const GetQuarkSystemTable_t getQuarkSystemTable = gAddr.GetQuarkSystemTable
            ? reinterpret_cast<GetQuarkSystemTable_t>(ResolveGameAddress(gAddr.GetQuarkSystemTable))
            : nullptr;
        if (!getQuarkSystemTable)
            return false;

        PositionSignal signal{};
        signal.id = kPositionSignal;
        signal.rotation[3] = 1.0f;
        if (!SendSeh(getQuarkSystemTable, gameObjectId, signal))
            return false;

        position[0] = signal.position[0];
        position[1] = signal.position[1];
        position[2] = signal.position[2];
        return true;
    }
}
