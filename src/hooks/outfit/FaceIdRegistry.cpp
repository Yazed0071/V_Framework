#include "pch.h"

#include "FaceIdRegistry.h"

#include <map>
#include <mutex>
#include <set>
#include <string>

#include "log.h"
#include "CustomFaceTable.h"
#include "../../core/V_FrameWorkState.h"

namespace outfit
{
    namespace
    {
        constexpr const char* kSpaceTag     = "FACEID";
        constexpr std::size_t kMaxKeyLength = 96;

        std::mutex                          g_Mutex;
        std::map<std::string, std::int32_t> g_IdByKey;
        std::set<std::int32_t>              g_Claimed;

        void CollectClaimed(const char*, std::int32_t value)
        {
            g_Claimed.insert(value);
        }

        void RefreshClaimed_NoLock()
        {
            g_Claimed.clear();
            V_FrameWorkState::ForEachPersistedConstant(kSpaceTag, &CollectClaimed);
            for (const auto& entry : g_IdByKey)
                g_Claimed.insert(entry.second);

            for (std::int32_t id = kFaceIdBandFirst; id <= kFaceIdBandLast; ++id)
                if (IsFaceRowDefined(id))
                    g_Claimed.insert(id);
        }

        std::int32_t Lookup_NoLock(const char* key)
        {
            auto cached = g_IdByKey.find(key);
            if (cached != g_IdByKey.end())
                return cached->second;

            const std::int32_t stored = V_FrameWorkState::GetPersistedConstant(kSpaceTag, key);
            if (stored >= kFaceIdBandFirst && stored <= kFaceIdBandLast)
            {
                g_IdByKey[key] = stored;
                return stored;
            }
            return 0;
        }
    }

    bool IsValidFaceKey(const char* key)
    {
        if (!key || !key[0])
            return false;

        const char head = key[0];
        if (!((head >= 'A' && head <= 'Z') || (head >= 'a' && head <= 'z') || head == '_'))
            return false;

        std::size_t length = 0;
        for (const char* p = key; *p; ++p, ++length)
        {
            const char c = *p;
            const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
                         || (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-';
            if (!ok)
                return false;
        }
        return length <= kMaxKeyLength;
    }

    std::int32_t FaceIdPoolSize()
    {
        return kFaceIdBandLast - kFaceIdBandFirst + 1;
    }

    bool IsManagedFaceId(std::int32_t faceId)
    {
        if (faceId < kFaceIdBandFirst || faceId > kFaceIdBandLast)
            return false;

        std::lock_guard<std::mutex> lock(g_Mutex);
        for (const auto& entry : g_IdByKey)
            if (entry.second == faceId)
                return true;

        g_Claimed.clear();
        V_FrameWorkState::ForEachPersistedConstant(kSpaceTag, &CollectClaimed);
        return g_Claimed.find(faceId) != g_Claimed.end();
    }

    std::int32_t FindFaceId(const char* key)
    {
        if (!IsValidFaceKey(key))
            return 0;

        std::lock_guard<std::mutex> lock(g_Mutex);
        return Lookup_NoLock(key);
    }

    std::int32_t RegisterFaceId(const char* key, std::int32_t preferResidue)
    {
        if (!IsValidFaceKey(key))
            return 0;

        std::lock_guard<std::mutex> lock(g_Mutex);

        const std::int32_t known = Lookup_NoLock(key);
        if (known > 0)
            return known;

        RefreshClaimed_NoLock();

        for (std::int32_t id = kFaceIdBandLast; id >= kFaceIdBandFirst; --id)
        {
            if (g_Claimed.find(id) != g_Claimed.end())
                continue;
            if (preferResidue >= 0 && (id & 3) != preferResidue)
                continue;

            V_FrameWorkState::SetPersistedConstant(kSpaceTag, key, id);
            g_IdByKey[key] = id;
            return id;
        }
        return 0;
    }

    std::int32_t FaceIdFreeSlotCount(std::int32_t preferResidue)
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        RefreshClaimed_NoLock();

        std::int32_t free = 0;
        for (std::int32_t id = kFaceIdBandFirst; id <= kFaceIdBandLast; ++id)
        {
            if (g_Claimed.find(id) != g_Claimed.end())
                continue;
            if (preferResidue >= 0 && (id & 3) != preferResidue)
                continue;
            ++free;
        }
        return free;
    }
}
