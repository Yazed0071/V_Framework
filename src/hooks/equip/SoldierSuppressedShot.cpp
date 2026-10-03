#include "pch.h"
#include "SoldierSuppressedShot.h"

#include <Windows.h>
#include <cstddef>
#include <cstdint>

#include "MissionCodeGuard.h"

namespace
{
    constexpr std::size_t   kRealized_RecordOwner = 0x58;
    constexpr std::size_t   kOwner_Records        = 0x28;
    constexpr std::size_t   kRecord_Stride        = 0xE;
    constexpr std::size_t   kRecord_GunInfoIndex  = 0x8;
    constexpr std::size_t   kRecord_SuppressorByte = 0xB;
    constexpr std::uint8_t  kRecord_SuppressorVisible = 0x10;

    constexpr std::size_t   kRealized_GunInfoSource = 0x68;
    constexpr std::size_t   kGunInfo_EnemySuppressedShotId = 0xC;

    constexpr std::size_t   kFireRequest_Flags    = 0x54;

    using GetGunInfo_t = const std::uint8_t*(__fastcall*)(void* source, std::uint32_t gunInfoIndex);
    constexpr std::uint32_t kFire_Suppressed      = 0x01;
    constexpr std::uint32_t kFire_Underbarrel     = 0x02;
    constexpr std::uint32_t kFire_Player          = 0x40;
}

void SoldierShot_MarkSuppressed(void* realizedEquipObject, std::uint32_t recordIndex, void* fireRequest)
{
    if (!realizedEquipObject || !fireRequest || MissionCodeGuard::ShouldBypassHooks())
        return;
    __try
    {
        auto* flags = reinterpret_cast<std::uint32_t*>(
            static_cast<std::uint8_t*>(fireRequest) + kFireRequest_Flags);
        if (*flags & (kFire_Player | kFire_Suppressed | kFire_Underbarrel))
            return;
        const auto* owner = *reinterpret_cast<std::uint8_t* const*>(
            static_cast<std::uint8_t*>(realizedEquipObject) + kRealized_RecordOwner);
        if (!owner)
            return;
        const auto* records = *reinterpret_cast<std::uint8_t* const*>(owner + kOwner_Records);
        if (!records)
            return;
        const std::uint8_t* record = records + static_cast<std::size_t>(recordIndex) * kRecord_Stride;
        if ((record[kRecord_SuppressorByte] & kRecord_SuppressorVisible) == 0)
            return;
        void* source = *reinterpret_cast<void* const*>(
            static_cast<std::uint8_t*>(realizedEquipObject) + kRealized_GunInfoSource);
        if (!source)
            return;
        const auto getGunInfo = reinterpret_cast<GetGunInfo_t>((*reinterpret_cast<void** const*>(source))[1]);
        const std::uint8_t* gunInfo = getGunInfo ? getGunInfo(source, record[kRecord_GunInfoIndex]) : nullptr;
        if (gunInfo && *reinterpret_cast<const std::uint32_t*>(gunInfo + kGunInfo_EnemySuppressedShotId) != 0)
            *flags |= kFire_Suppressed;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
}
