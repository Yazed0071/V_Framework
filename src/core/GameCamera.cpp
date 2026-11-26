#include <pch.h>
#include "GameCamera.h"

#include <Windows.h>
#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"

namespace
{
    using GetQuarkSystemTable_t = std::uint8_t*(__fastcall*)();
    using IsInView_t            = bool(__fastcall*)(void* gameUtility, const float* position, float radius);
    using CameraDistanceSq_t    = float(__fastcall*)(void* gameUtility, const float* position);

    void* GameUtility()
    {
        static GetQuarkSystemTable_t getQuarkSystemTable = nullptr;
        if (!getQuarkSystemTable && gAddr.GetQuarkSystemTable)
            getQuarkSystemTable = reinterpret_cast<GetQuarkSystemTable_t>(ResolveGameAddress(gAddr.GetQuarkSystemTable));
        std::uint8_t* quark = getQuarkSystemTable ? getQuarkSystemTable() : nullptr;
        if (!quark)
            return nullptr;
        std::uint8_t* systems = *reinterpret_cast<std::uint8_t**>(quark + 0x98);   // the game's system table
        return systems ? *reinterpret_cast<void**>(systems + 0x20) : nullptr;      // GameUtilityImpl
    }
}

bool GameCamera::IsOnScreen(const float* position, float radius)
{
    __try
    {
        void* gameUtility = GameUtility();
        if (!gameUtility)
            return false;
        alignas(16) float at[4] = { position[0], position[1], position[2], 0.0f };
        void** vtable = *reinterpret_cast<void***>(gameUtility);
        const auto isInView = reinterpret_cast<IsInView_t>(vtable[1]);   // +0x08, camera frustum test of a sphere
        return isInView(gameUtility, at, radius);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

float GameCamera::DistanceSq(const float* position)
{
    __try
    {
        void* gameUtility = GameUtility();
        if (!gameUtility)
            return 0.0f;
        alignas(16) float at[4] = { position[0], position[1], position[2], 0.0f };
        void** vtable = *reinterpret_cast<void***>(gameUtility);
        const auto cameraDistanceSq = reinterpret_cast<CameraDistanceSq_t>(vtable[4]);   // +0x20, squared and scaled by the camera zoom
        return cameraDistanceSq(gameUtility, at);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return 0.0f;
    }
}
