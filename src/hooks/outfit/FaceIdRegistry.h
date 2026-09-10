#pragma once

#include <cstdint>

namespace outfit
{
    constexpr std::int32_t kFaceIdBandFirst = 650;
    constexpr std::int32_t kFaceIdBandLast  = 679;

    constexpr std::int32_t kFaceResidueAny = -1;

    bool IsValidFaceKey(const char* key);

    std::int32_t RegisterFaceId(const char* key, std::int32_t preferResidue);
    std::int32_t FindFaceId(const char* key);

    bool IsManagedFaceId(std::int32_t faceId);

    std::int32_t FaceIdPoolSize();
    std::int32_t FaceIdFreeSlotCount(std::int32_t preferResidue);
}
