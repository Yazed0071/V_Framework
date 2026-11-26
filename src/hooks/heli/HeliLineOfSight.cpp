#include <pch.h>
#include "HeliLineOfSight.h"

#include <Windows.h>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "HookUtils.h"

namespace
{
    using QuarkTable_t = std::uint8_t*(__fastcall*)();
    using GeoSegment_t = int(__fastcall*)(void* geo, void* desc, std::uint32_t mask, const float* from,
                                          const float* to, float radius);

    constexpr std::size_t   kQuarkGeo        = 0x80;
    constexpr std::size_t   kGeoQuery        = 0x08;
    constexpr std::size_t   kGeoSlotSegment  = 0x10;
    constexpr std::size_t   kDescRecordSize  = 0x60;
    constexpr std::size_t   kDescBytes       = 0x70 + 2 * kDescRecordSize;
    constexpr std::uint64_t kDescCapacityOne = 0x10000ull;   // capacity 1 stops at the first hit

    void InitSegmentDesc(std::uint8_t* desc)
    {
        std::memset(desc, 0, kDescBytes);
        *reinterpret_cast<std::int32_t*>(desc + 0x3C)  = -1;
        *reinterpret_cast<std::int32_t*>(desc + 0x64)  = -1;
        *reinterpret_cast<std::uint64_t*>(desc + 0x68) = kDescCapacityOne;
        for (std::uint8_t* record = desc + 0x70; record < desc + kDescBytes; record += kDescRecordSize)
        {
            *reinterpret_cast<std::uint32_t*>(record + 0x0C) = 0xFFFF0000u;
            *reinterpret_cast<std::uint16_t*>(record + 0x1C) = 0xFFFFu;
            *reinterpret_cast<std::uint16_t*>(record + 0x1E) = 0x00FFu;
            *reinterpret_cast<std::uint32_t*>(record + 0x2C) = 0xFF7FFFFFu;
        }
        *reinterpret_cast<std::uint64_t*>(desc + 0x00) = 0xFFFFFFFFFFFFFFFFull;   // match every kind of geometry
        *reinterpret_cast<std::uint64_t*>(desc + 0x18) = 0x2ull;
    }

    bool QuerySEH(QuarkTable_t quarkTable, std::uint8_t* desc, std::uint32_t mask, const float* from, const float* to,
                  bool* blocked)
    {
        __try
        {
            std::uint8_t* table  = quarkTable();
            std::uint8_t* holder = table ? *reinterpret_cast<std::uint8_t**>(table + kQuarkGeo) : nullptr;
            void* geo = holder ? *reinterpret_cast<void**>(holder + kGeoQuery) : nullptr;
            if (!geo)
                return false;
            void** vtbl = *reinterpret_cast<void***>(geo);
            *blocked = reinterpret_cast<GeoSegment_t>(vtbl[kGeoSlotSegment / sizeof(void*)])(geo, desc, mask, from, to, 0.0f) > 0;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }
}

bool HeliLineOfSight::Blocked(const float* from, const float* to, std::uint32_t mask, bool* blocked)
{
    static const QuarkTable_t quarkTable = reinterpret_cast<QuarkTable_t>(ResolveGameAddress(gAddr.Fox_GetQuarkSystemTable));
    if (!quarkTable || !from || !to || !blocked)
        return false;
    alignas(16) float a[4] = { from[0], from[1], from[2], 1.0f };
    alignas(16) float b[4] = { to[0], to[1], to[2], 1.0f };
    alignas(16) std::uint8_t desc[kDescBytes];
    InitSegmentDesc(desc);
    return QuerySEH(quarkTable, desc, mask, a, b, blocked);
}
