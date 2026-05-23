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

    struct NativeGameObjectId
    {
        std::uint16_t value = 0xFFFF;
    };

    using GetGameObjectIdWithIndex_t =
        void(__fastcall*)(NativeGameObjectId* out,
            std::uint32_t typeArg,
            std::uint32_t index);

    static GetGameObjectIdWithIndex_t g_GetGameObjectIdWithIndex = nullptr;

    static bool IsTypeName(const char* lhs, const char* rhs)
    {
        if (!lhs || !rhs)
            return false;

        return std::strcmp(lhs, rhs) == 0;
    }

    static std::uint16_t LookupTypeIndex(const char* typeName)
    {
        if (IsTypeName(typeName, "TppSoldier2"))
            return TppGameObjectType::kSoldier2;

        return TppGameObjectType::kUnknown;
    }

    static std::uint32_t PackGameObjectId(std::uint16_t typeIndex,
        std::uint32_t index)
    {
        return (static_cast<std::uint32_t>(typeIndex) << 9) |
            (index & 0x1FFu);
    }

    static bool TryNativeWithStrCode32(const char* typeName,
        std::uint32_t index,
        std::uint32_t& gameObjectIdOut)
    {
        if (!g_GetGameObjectIdWithIndex)
            return false;

        const std::uint32_t typeNameId = FoxHashes::StrCode32(typeName);
        NativeGameObjectId result{};

        __try
        {
            g_GetGameObjectIdWithIndex(&result, typeNameId, index);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            Log("[GetGameObjectIdWithIndex] SEH exception type=%s index=%u\n",
                typeName,
                index);
            return false;
        }

        if (result.value == 0xFFFFu)
            return false;

        gameObjectIdOut = static_cast<std::uint32_t>(result.value);
        return true;
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

    const std::uint16_t typeIndex = LookupTypeIndex(typeName);
    if (typeIndex != TppGameObjectType::kUnknown)
    {
        gameObjectIdOut = PackGameObjectId(typeIndex, index);
        return true;
    }

    if (!g_GetGameObjectIdWithIndex)
        Install_GetGameObjectIdWithIndex();

    if (TryNativeWithStrCode32(typeName, index, gameObjectIdOut))
        return true;

    Log("[GetGameObjectIdWithIndex] unmapped type=%s index=%u (add it to TppGameObjectType)\n",
        typeName,
        index);

    return false;
}

bool GetSoldierGameObjectIdWithIndex(std::uint32_t soldierIndex,
    std::uint32_t& gameObjectIdOut)
{
    return GetGameObjectIdWithIndex("TppSoldier2",
        soldierIndex,
        gameObjectIdOut);
}

std::uint32_t GetGameObjectIdByIndex(const char* typeName,
    std::uint32_t index)
{
    std::uint32_t out = 0;
    return GetGameObjectIdWithIndex(typeName, index, out) ? out : 0;
}
