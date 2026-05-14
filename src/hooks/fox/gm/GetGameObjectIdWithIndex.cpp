#include "pch.h"

#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "HookUtils.h"
#include "FoxHashes.h"
#include "log.h"
#include "GetGameObjectIdWithIndex.h"

namespace
{
    static constexpr std::uint32_t kInvalidGameObjectId = 0xFFFFu;

    // TppGameObject.GAME_OBJECT_TYPE_SOLDIER2 appears to be type index 2.
    // Native GameObjectId packing from disassembly:
    //   gameObjectId = (typeIndex << 9) | (index & 0x1FF)
    static constexpr std::uint16_t kTppSoldier2TypeIndex = 2;

    struct NativeGameObjectId
    {
        std::uint16_t value = 0xFFFF;
    };

    using GetGameObjectIdWithIndex_t =
        void(__fastcall*)(NativeGameObjectId* out,
            std::uint32_t typeNameId,
            std::uint32_t index);

    static GetGameObjectIdWithIndex_t g_GetGameObjectIdWithIndex = nullptr;

    static std::uint32_t BuildGameObjectIdFromTypeIndex(std::uint16_t typeIndex,
        std::uint32_t index)
    {
        return (static_cast<std::uint32_t>(typeIndex) << 9) |
            (index & 0x1FFu);
    }

    static bool IsTypeName(const char* lhs, const char* rhs)
    {
        if (!lhs || !rhs)
            return false;

        return std::strcmp(lhs, rhs) == 0;
    }

    static bool TryFallbackGameObjectIdWithIndex(const char* typeName,
        std::uint32_t index,
        std::uint32_t& gameObjectIdOut)
    {
        if (IsTypeName(typeName, "TppSoldier2"))
        {
            gameObjectIdOut =
                BuildGameObjectIdFromTypeIndex(kTppSoldier2TypeIndex, index);

            Log("[GetGameObjectIdWithIndex] native failed, fallback type=%s index=%u -> gameObjectId=%u\n",
                typeName,
                index,
                gameObjectIdOut);

            return true;
        }

        return false;
    }
}

bool Install_GetGameObjectIdWithIndex()
{
    if (g_GetGameObjectIdWithIndex)
        return true;

    if (!gAddr.GetGameObjectIdWithIndex)
    {
        Log("[GetGameObjectIdWithIndex] ERROR: address is missing for this build.\n");
        return false;
    }

    void* target = ResolveGameAddress(gAddr.GetGameObjectIdWithIndex);
    if (!target)
    {
        Log("[GetGameObjectIdWithIndex] ERROR: ResolveGameAddress failed. abs=0x%llX\n",
            static_cast<unsigned long long>(gAddr.GetGameObjectIdWithIndex));
        return false;
    }

    g_GetGameObjectIdWithIndex =
        reinterpret_cast<GetGameObjectIdWithIndex_t>(target);

    Log("[GetGameObjectIdWithIndex] installed. target=%p abs=0x%llX\n",
        target,
        static_cast<unsigned long long>(gAddr.GetGameObjectIdWithIndex));

    return true;
}

bool Uninstall_GetGameObjectIdWithIndex()
{
    g_GetGameObjectIdWithIndex = nullptr;

    Log("[GetGameObjectIdWithIndex] uninstalled.\n");
    return true;
}

bool Is_GetGameObjectIdWithIndex_Installed()
{
    return g_GetGameObjectIdWithIndex != nullptr;
}

bool GetGameObjectIdWithIndex(const char* typeName,
    std::uint32_t index,
    std::uint32_t& gameObjectIdOut)
{
    gameObjectIdOut = kInvalidGameObjectId;

    if (!typeName || !typeName[0])
        return false;

    bool nativeAttempted = false;

    if (!g_GetGameObjectIdWithIndex)
        Install_GetGameObjectIdWithIndex();

    if (g_GetGameObjectIdWithIndex)
    {
        const std::uint32_t typeNameId = FoxHashes::StrCode32(typeName);
        NativeGameObjectId result{};

        nativeAttempted = true;

        __try
        {
            g_GetGameObjectIdWithIndex(&result, typeNameId, index);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            Log("[GetGameObjectIdWithIndex] SEH exception type=%s index=%u\n",
                typeName,
                index);

            result.value = 0xFFFF;
        }

        if (result.value != 0xFFFFu)
        {
            gameObjectIdOut = static_cast<std::uint32_t>(result.value);
            return true;
        }
    }

    if (TryFallbackGameObjectIdWithIndex(typeName, index, gameObjectIdOut))
        return true;

    if (nativeAttempted)
    {
        Log("[GetGameObjectIdWithIndex] no GameObjectId type=%s index=%u\n",
            typeName,
            index);
    }

    return false;
}

bool GetSoldierGameObjectIdWithIndex(std::uint32_t soldierIndex,
    std::uint32_t& gameObjectIdOut)
{
    return GetGameObjectIdWithIndex("TppSoldier2",
        soldierIndex,
        gameObjectIdOut);
}