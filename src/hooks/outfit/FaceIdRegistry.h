#pragma once

#include <cstdint>
#include <string>

namespace outfit
{
    struct FaceIdRange
    {
        std::int32_t first;
        std::int32_t last;
    };

    constexpr FaceIdRange kFaceIdRanges[] =
    {
        { 568, 599 },
        { 520, 549 },
        { 480, 499 },
        { 400, 449 },
        { 304, 319 },
        { 650, 679 },
    };

    constexpr bool IsFaceIdInPool(std::int32_t faceId)
    {
        for (const FaceIdRange& range : kFaceIdRanges)
            if (faceId >= range.first && faceId <= range.last)
                return true;
        return false;
    }

    enum class FaceIdRegistrationFailure
    {
        None,
        InvalidKey,
        PoolExhausted,
        PersistedFaceIdConflict,
    };

    struct FaceFallbackMetadata
    {
        std::int32_t faceId = 0;
        std::int32_t gender = 0;
        std::int32_t race = 0;
        std::int32_t donorFaceId = -1;
    };

    bool IsValidFaceKey(const char* key);

    std::int32_t RegisterFaceId(const char* key, FaceIdRegistrationFailure* failure = nullptr);
    std::int32_t FindFaceId(const char* key);
    void NoteFaceKeyRequested(const char* key);
    bool FindUnregisteredFaceKey(std::int32_t faceId, std::string& key);
    bool IsReusedFaceId(std::int32_t faceId);

    bool IsManagedFaceId(std::int32_t faceId);

    bool GetFaceFallbackMetadata(std::int32_t faceId, FaceFallbackMetadata& out);
    void RememberFaceMetadata(std::int32_t faceId, std::int32_t gender, std::int32_t race);
    void PreparePersistedFaceFallbacks();

    std::int32_t FaceIdPoolSize();
}
