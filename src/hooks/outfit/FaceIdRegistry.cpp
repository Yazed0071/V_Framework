#include "pch.h"

#include "FaceIdRegistry.h"

#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "log.h"
#include "CustomFaceTable.h"
#include "../../core/V_FrameWorkState.h"

namespace outfit
{
    namespace
    {
        constexpr const char* kSpaceTag     = "FACEID";
        constexpr const char* kMetadataSpaceTag = "FACEMETA";
        constexpr const char* kRetiredSpaceTag = "FACERETIRED";
        constexpr const char* kReusedSpaceTag = "FACEREUSED";
        constexpr std::size_t kMaxKeyLength = 96;

        std::mutex                          g_Mutex;
        std::map<std::string, std::int32_t> g_IdByKey;
        std::map<std::string, std::int32_t> g_PersistedByKey;
        std::set<std::string>               g_RequestedKeys;
        std::set<std::int32_t>              g_Claimed;
        std::set<std::int32_t>              g_Retired;
        std::map<std::int32_t, FaceFallbackMetadata> g_MetadataById;
        std::set<std::int32_t> g_FallbackFailuresReported;

        bool ParseFaceIdName(const char* name, std::int32_t& faceId)
        {
            if (!name || name[0] != 'f' || !name[1])
                return false;
            faceId = 0;
            for (const char* digit = name + 1; *digit; ++digit)
            {
                if (*digit < '0' || *digit > '9' || faceId > 899)
                    return false;
                faceId = faceId * 10 + (*digit - '0');
            }
            return IsFaceIdInPool(faceId) && std::string(name) == "f" + std::to_string(faceId);
        }

        std::string FaceIdName(std::int32_t faceId)
        {
            return "f" + std::to_string(faceId);
        }

        void CollectRetired(const char* key, std::int32_t value)
        {
            std::int32_t faceId = 0;
            if (value != 0 && ParseFaceIdName(key, faceId))
                g_Retired.insert(faceId);
        }

        void CollectPersisted(const char* key, std::int32_t value)
        {
            if (key && IsFaceIdInPool(value))
                g_PersistedByKey[key] = value;
        }

        void CollectMetadata(const char* key, std::int32_t value)
        {
            if (value < 0 || (value & ~0x13FFF) != 0 || (value & 0x10000) == 0)
                return;
            std::int32_t faceId = 0;
            if (!ParseFaceIdName(key, faceId))
                return;
            const std::int32_t donor = (value & 0x3FF) - 1;
            if (donor >= 600 || (donor >= 0 && IsFaceIdInPool(donor)))
                return;
            g_MetadataById[faceId] = { faceId, (value >> 10) & 1,
                                      (value >> 11) & 7, donor };
        }

        void RefreshPersisted_NoLock()
        {
            g_PersistedByKey.clear();
            V_FrameWorkState::ForEachPersistedConstant(kSpaceTag, &CollectPersisted);
            g_MetadataById.clear();
            V_FrameWorkState::ForEachPersistedConstant(kMetadataSpaceTag, &CollectMetadata);
        }

        bool IsOwnedByAnotherKey_NoLock(const char* key, std::int32_t id,
                                       std::string& owner)
        {
            for (const auto& entry : g_IdByKey)
            {
                if (entry.second != id)
                    continue;
                owner = entry.first;
                return owner != key;
            }
            for (const auto& entry : g_PersistedByKey)
            {
                if (entry.second != id || !IsValidFaceKey(entry.first.c_str()))
                    continue;
                owner = entry.first;
                return owner != key;
            }
            return false;
        }

        void RefreshClaimed_NoLock()
        {
            RefreshPersisted_NoLock();
            g_Retired.clear();
            V_FrameWorkState::ForEachPersistedConstant(kRetiredSpaceTag, &CollectRetired);
            g_Claimed.clear();
            for (const auto& entry : g_PersistedByKey)
                g_Claimed.insert(entry.second);
            for (const auto& entry : g_IdByKey)
                g_Claimed.insert(entry.second);
            for (const auto& entry : g_MetadataById)
                g_Claimed.insert(entry.first);

            for (const FaceIdRange& range : kFaceIdRanges)
                for (std::int32_t id = range.first; id <= range.last; ++id)
                    if (IsFaceRowDefined(id))
                        g_Claimed.insert(id);
        }

        std::int32_t Lookup_NoLock(const char* key)
        {
            auto cached = g_IdByKey.find(key);
            if (cached != g_IdByKey.end())
                return cached->second;

            const auto persisted = g_PersistedByKey.find(key);
            if (persisted != g_PersistedByKey.end())
            {
                std::string owner;
                if (!IsOwnedByAnotherKey_NoLock(key, persisted->second, owner))
                    return persisted->second;
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
        std::int32_t size = 0;
        for (const FaceIdRange& range : kFaceIdRanges)
            size += range.last - range.first + 1;
        return size;
    }

    bool IsManagedFaceId(std::int32_t faceId)
    {
        if (!IsFaceIdInPool(faceId) || IsFaceRowDefinedByOtherMod(faceId))
            return false;

        std::lock_guard<std::mutex> lock(g_Mutex);
        for (const auto& entry : g_IdByKey)
            if (entry.second == faceId)
                return true;

        RefreshPersisted_NoLock();
        if (g_MetadataById.find(faceId) != g_MetadataById.end())
            return true;
        for (const auto& entry : g_PersistedByKey)
            if (entry.second == faceId && IsValidFaceKey(entry.first.c_str()))
                return true;
        return false;
    }

    bool GetFaceFallbackMetadata(std::int32_t faceId, FaceFallbackMetadata& out)
    {
        if (!IsFaceIdInPool(faceId))
            return false;
        std::lock_guard<std::mutex> lock(g_Mutex);
        RefreshPersisted_NoLock();
        const auto found = g_MetadataById.find(faceId);
        if (found == g_MetadataById.end())
            return false;
        out = found->second;
        return true;
    }

    void RememberFaceMetadata(std::int32_t faceId, std::int32_t gender, std::int32_t race)
    {
        if (!IsFaceIdInPool(faceId) || gender < 0 || gender > 1 || race < 0 || race > 7)
            return;
        FaceFallbackMetadata previous;
        const bool known = GetFaceFallbackMetadata(faceId, previous);
        const std::int32_t preferred = known && previous.gender == gender
            ? previous.donorFaceId : -1;
        const std::int32_t donor = PickCompatibleVanillaDonorFaceId(faceId, gender, race, preferred);
        const std::int32_t packed = 0x10000 | (race << 11) | (gender << 10) | (donor + 1);
        const std::string name = "f" + std::to_string(faceId);
        V_FrameWorkState::SetPersistedConstant(kMetadataSpaceTag, name.c_str(), packed);
    }

    void PreparePersistedFaceFallbacks()
    {
        if (!IsFaceSystemReady())
            return;
        std::vector<FaceFallbackMetadata> metadata;
        {
            std::lock_guard<std::mutex> lock(g_Mutex);
            RefreshPersisted_NoLock();
            for (const auto& entry : g_MetadataById)
                metadata.push_back(entry.second);
        }
        for (const FaceFallbackMetadata& entry : metadata)
        {
            if (IsFaceRowDefined(entry.faceId) && !IsCustomFaceFallback(entry.faceId))
                continue;
            std::int32_t donor = -1;
            std::string reason;
            if (!WriteMissingCustomFaceFallback(entry.faceId, entry.gender, entry.race,
                                                entry.donorFaceId, donor, reason))
            {
                std::lock_guard<std::mutex> lock(g_Mutex);
                if (g_FallbackFailuresReported.insert(entry.faceId).second)
                    LogWarn("CustomFace: missing faceId %d got no temporary fallback: %s",
                        entry.faceId, reason.c_str());
            }
        }
    }

    std::int32_t FindFaceId(const char* key)
    {
        if (!IsValidFaceKey(key))
            return 0;

        std::lock_guard<std::mutex> lock(g_Mutex);
        RefreshPersisted_NoLock();
        const std::int32_t id = Lookup_NoLock(key);
        if (id > 0 && IsFaceRowDefinedByOtherMod(id))
            return 0;
        return id;
    }

    void NoteFaceKeyRequested(const char* key)
    {
        if (!IsValidFaceKey(key))
            return;
        std::lock_guard<std::mutex> lock(g_Mutex);
        g_RequestedKeys.insert(key);
        V_FrameWorkState::GetPersistedConstant(kSpaceTag, key);
    }

    bool FindUnregisteredFaceKey(std::int32_t faceId, std::string& key)
    {
        if (!IsFaceIdInPool(faceId))
            return false;
        std::lock_guard<std::mutex> lock(g_Mutex);
        for (const auto& entry : g_IdByKey)
            if (entry.second == faceId)
                return false;
        RefreshPersisted_NoLock();
        for (const auto& entry : g_PersistedByKey)
        {
            if (entry.second != faceId || !IsValidFaceKey(entry.first.c_str()))
                continue;
            if (g_RequestedKeys.count(entry.first) != 0)
                return false;
            key = entry.first;
            return true;
        }
        return false;
    }

    bool IsReusedFaceId(std::int32_t faceId)
    {
        return IsFaceIdInPool(faceId)
            && V_FrameWorkState::GetPersistedConstant(kReusedSpaceTag, FaceIdName(faceId).c_str()) != 0;
    }

    std::int32_t RegisterFaceId(const char* key, FaceIdRegistrationFailure* failure)
    {
        if (failure)
            *failure = FaceIdRegistrationFailure::None;
        if (!IsValidFaceKey(key))
        {
            if (failure)
                *failure = FaceIdRegistrationFailure::InvalidKey;
            return 0;
        }
        std::lock_guard<std::mutex> lock(g_Mutex);
        RefreshClaimed_NoLock();

        const auto active = g_IdByKey.find(key);
        const std::int32_t stored = V_FrameWorkState::GetPersistedConstant(kSpaceTag, key);
        const std::int32_t known = active != g_IdByKey.end() ? active->second
                                  : (IsFaceIdInPool(stored) ? stored : 0);

        std::string owner;
        const bool duplicate = known > 0 && IsOwnedByAnotherKey_NoLock(key, known, owner);
        const bool otherMod = known > 0 && IsFaceRowDefinedByOtherMod(known);
        if (known > 0 && (otherMod || duplicate))
        {
            if (failure)
                *failure = FaceIdRegistrationFailure::PersistedFaceIdConflict;
            LogWarn("CustomFace '%s': persisted faceId %d taken (foreign row %d, "
                "duplicate key %d), not registered", key, known, otherMod ? 1 : 0,
                duplicate ? 1 : 0);
            return 0;
        }
        if (known > 0)
        {
            g_IdByKey[key] = known;
            return known;
        }

        for (int pass = 0; pass < 2; ++pass)
        {
            const bool takeRetired = pass == 1;
            for (const FaceIdRange& range : kFaceIdRanges)
            {
                for (std::int32_t id = range.last; id >= range.first; --id)
                {
                    if (g_Claimed.find(id) != g_Claimed.end()
                        || (g_Retired.find(id) != g_Retired.end()) != takeRetired)
                        continue;

                    const std::string name = FaceIdName(id);
                    {
                        V_FrameWorkState::SaveBatch batch;
                        V_FrameWorkState::SetPersistedConstant(kSpaceTag, key, id);
                        if (takeRetired)
                        {
                            V_FrameWorkState::SetPersistedConstant(kReusedSpaceTag, name.c_str(), 1);
                            V_FrameWorkState::SetPersistedConstant(kRetiredSpaceTag, name.c_str(), 0);
                        }
                    }
                    g_IdByKey[key] = id;
                    return id;
                }
            }
        }
        if (failure)
            *failure = FaceIdRegistrationFailure::PoolExhausted;
        return 0;
    }
}
