#pragma once

#include <Windows.h>
#include <cstddef>
#include <cstdint>

namespace VehicleData
{
    constexpr std::size_t kDataBodyFoxDataBody = 0x10;
    constexpr std::size_t kFoxDataBodyHandle   = 0x38;   // the fox DataBody reaches its fox2 entity through a handle
    constexpr std::size_t kHandleEntity        = 0x18;
    constexpr std::size_t kFilePath            = 0x30;

    inline std::uint8_t* EntityOfDataBody(const std::uint8_t* dataBody)
    {
        __try
        {
            const std::uint8_t* foxDataBody = *reinterpret_cast<std::uint8_t* const*>(dataBody + kDataBodyFoxDataBody);
            const std::uint8_t* handle = *reinterpret_cast<std::uint8_t* const*>(foxDataBody + kFoxDataBodyHandle);
            return handle ? *reinterpret_cast<std::uint8_t* const*>(handle + kHandleEntity) : nullptr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    inline std::uint64_t PathOfFileField(const std::uint8_t* entity, std::size_t fileField)
    {
        __try
        {
            const std::uint8_t* file = *reinterpret_cast<std::uint8_t* const*>(entity + fileField);
            return file ? *reinterpret_cast<const std::uint64_t*>(file + kFilePath) : 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    inline bool ReadByte(const std::uint8_t* entity, std::size_t field, std::uint8_t& value)
    {
        __try
        {
            value = entity[field];
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    inline bool WriteByte(std::uint8_t* entity, std::size_t field, std::uint8_t value)
    {
        __try
        {
            entity[field] = value;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }
}
