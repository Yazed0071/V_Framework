#pragma once

#include <Windows.h>
#include <cstdint>

#include "AddressSet.h"
#include "EquipAliasLease.h"
#include "HookUtils.h"

namespace ThrowingSystemLayout
{
    constexpr std::size_t kGameSystemTable    = 0x98;    // QuarkSystemTable -> GameSystemTable
    constexpr std::size_t kThrowingSystem     = 0x200;   // GameSystemTable -> ThrowingSystem interface
    constexpr std::size_t kRangeAttackSystem  = 0x258;
    constexpr std::size_t kBlastSystem        = 0x220;

    constexpr std::size_t kPositions          = 0x08;    // f32x4 per handle
    constexpr std::size_t kRotations          = 0x10;    // quaternion per handle
    constexpr std::size_t kThrowingInfo       = 0x18;    // u16 equipId, u16 owner per handle
    constexpr std::size_t kLatestThrownByPlayer = 0x20;
    constexpr std::size_t kInternalSystem     = 0xE8;
    constexpr std::size_t kCapacity           = 0x70;
    constexpr std::size_t kImplCount          = 0x74;
    constexpr std::size_t kImplInterfaces     = 0x98;    // ThrowingImpl + 0x20 per impl
    constexpr std::size_t kImplSliceSizes     = 0xA0;    // u8 per impl
    constexpr std::size_t kRecords            = 0xA8;

    constexpr std::size_t kRecordSize         = 0x24;
    constexpr std::size_t kRecordLifeLeft     = 0x04;
    constexpr std::size_t kRecordOwner        = 0x16;
    constexpr std::size_t kRecordFlags        = 0x1C;
    constexpr std::size_t kRecordFlags2       = 0x20;

    constexpr std::size_t kImplFirstHandle    = 0x54;
    constexpr std::size_t kImplVelocities     = 0x98;    // f32x4 per local item
    constexpr std::size_t kImplEquipIds       = 0x120;   // u16 equipId + u16 owner per local item
    constexpr std::size_t kImplRecords        = 0x128;

    constexpr std::uint32_t kFlagAlive        = 0x1;
    constexpr std::uint32_t kFlagsStopped     = 0x3000;
    constexpr std::uint32_t kFlagDone         = 0x4000;
    constexpr std::uint32_t kFlagActivated    = 0x8000000;
    constexpr std::uint32_t kFlagActivateNow  = 0x10000000;
    constexpr std::uint32_t kReboundCountShift = 8;
    constexpr std::uint32_t kReboundCountMask  = 0xF;

    constexpr std::uint32_t kNoHandle         = 0xFF;

    using GetQuarkSystemTable_t = std::uint8_t*(__fastcall*)();

    inline std::uint8_t* GameSystemTable()
    {
        auto getQuark = reinterpret_cast<GetQuarkSystemTable_t>(ResolveGameAddress(gAddr.GetQuarkSystemTable));
        if (!getQuark)
            return nullptr;
        std::uint8_t* quark = getQuark();
        return quark ? *reinterpret_cast<std::uint8_t**>(quark + kGameSystemTable) : nullptr;
    }

    inline std::uint8_t* ThrowingSystem()
    {
        std::uint8_t* gst = GameSystemTable();
        return gst ? *reinterpret_cast<std::uint8_t**>(gst + kThrowingSystem) : nullptr;
    }

    template <typename T>
    inline T Read(const std::uint8_t* base, std::size_t offset)
    {
        return *reinterpret_cast<const T*>(base + offset);
    }

    inline std::uint32_t Capacity(const std::uint8_t* system)
    {
        return Read<std::uint32_t>(system, kCapacity);
    }

    inline std::uint8_t* Record(std::uint8_t* system, std::uint32_t handle)
    {
        return Read<std::uint8_t*>(system, kRecords) + handle * kRecordSize;
    }

    inline const float* Position(std::uint8_t* system, std::uint32_t handle)
    {
        return Read<const float*>(system, kPositions) + handle * 4;
    }

    inline const float* Rotation(std::uint8_t* system, std::uint32_t handle)
    {
        return Read<const float*>(system, kRotations) + handle * 4;
    }

    inline std::uint16_t EquipId(std::uint8_t* system, std::uint32_t handle)
    {
        return Read<std::uint16_t>(Read<std::uint8_t*>(system, kThrowingInfo), handle * 4);
    }

    inline std::uint16_t RealEquipId(std::uint8_t* system, std::uint32_t handle)
    {
        return static_cast<std::uint16_t>(equip::FromEngine(EquipId(system, handle)));
    }

    inline bool ResolveImpl(std::uint8_t* system, std::uint32_t handle, std::uint8_t*& impl, std::uint32_t& localIndex)
    {
        const std::uint32_t implCount = Read<std::uint32_t>(system, kImplCount);
        const std::uint8_t* sliceSizes = Read<const std::uint8_t*>(system, kImplSliceSizes);
        auto* const* interfaces = Read<std::uint8_t* const*>(system, kImplInterfaces);
        std::uint32_t local = handle;
        for (std::uint32_t i = 0; i < implCount; ++i)
        {
            if (local < sliceSizes[i])
            {
                impl = interfaces[i] - 0x20;
                localIndex = local;
                return true;
            }
            local -= sliceSizes[i];
        }
        return false;
    }

    inline std::uint32_t HandleOf(const std::uint8_t* impl, std::uint32_t localIndex)
    {
        return Read<std::uint32_t>(impl, kImplFirstHandle) + localIndex;
    }

    inline std::uint8_t* ImplRecord(std::uint8_t* impl, std::uint32_t localIndex)
    {
        return Read<std::uint8_t*>(impl, kImplRecords) + localIndex * kRecordSize;
    }

    inline std::uint16_t ImplEquipId(const std::uint8_t* impl, std::uint32_t localIndex)
    {
        return Read<std::uint16_t>(Read<const std::uint8_t*>(impl, kImplEquipIds), localIndex * 4);
    }

    inline std::uint16_t ImplRealEquipId(const std::uint8_t* impl, std::uint32_t localIndex)
    {
        return static_cast<std::uint16_t>(equip::FromEngine(ImplEquipId(impl, localIndex)));
    }
}
