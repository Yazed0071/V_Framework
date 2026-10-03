#include "pch.h"

#include "StaffControllerImpl_AddStaff.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <string>

#include <HookUtils.h>

#include "log.h"
#include "AddressSet.h"
#include "CustomFaceTable.h"
#include "CustomHeadRegistry.h"
#include "FaceIdRegistry.h"
#include "../../core/MissionCodeGuard.h"
#include "../../core/V_FrameWorkState.h"

namespace outfit
{
    namespace
    {
        constexpr std::uint64_t kMaxStaffId = 0x1FFFFFFFFFFFFFull;
        constexpr const char* kFaceSpace = "STAFFFACE";
        constexpr const char* kProxySpace = "STAFFPROXY";

        using AddStaffShort_t = void (__fastcall*)(void*, std::uint64_t, std::uint8_t, char,
                                                  std::uint32_t, std::uint16_t, char);
        using AddStaffFull_t = std::uint64_t (__fastcall*)(void*, std::uint64_t, std::uint8_t, char,
                                                          std::uint64_t, std::uint64_t, std::uint64_t,
                                                          std::uint64_t, std::uint64_t, std::uint64_t,
                                                          std::uint64_t, std::uint64_t);
        using StaffPortrait_t = std::uint64_t* (__fastcall*)(void*, std::uint64_t*,
                                                            std::uint32_t, std::uint32_t);
        using RefreshFaceRecord_t = void (__fastcall*)(void*, void*, std::uint16_t, std::int16_t);

        AddStaffShort_t g_OriginalShort = nullptr;
        AddStaffFull_t g_OriginalFull = nullptr;
        StaffPortrait_t g_OriginalPortrait = nullptr;
        RefreshFaceRecord_t g_OriginalRefreshFaceRecord = nullptr;
        thread_local void* g_PortraitContextController = nullptr;
        std::mutex g_Mutex;
        std::map<std::uint64_t, std::uint64_t> g_PendingOwners;
        std::map<std::uint64_t, std::uint64_t> g_PersistedOwners;
        std::set<std::uint64_t> g_ConflictingProxies;
        std::atomic<bool> g_ReportedFallbackRecruit{ false };
        std::atomic<bool> g_ReportedHealSave{ false };
        std::atomic<bool> g_ReportedRosterLayout{ false };
        std::atomic<bool> g_ReportedHealThread{ false };
        std::atomic<DWORD> g_AddStaffThread{ 0 };
        std::atomic<bool> g_AddStaffOnSeveralThreads{ false };

        void NoteAddStaffThread()
        {
            const DWORD current = GetCurrentThreadId();
            DWORD first = 0;
            if (!g_AddStaffThread.compare_exchange_strong(first, current, std::memory_order_relaxed)
                && first != current)
                g_AddStaffOnSeveralThreads.store(true, std::memory_order_relaxed);
        }
        std::mutex g_HealMutex;
        std::atomic<std::uint64_t> g_LastHealTick{ 0 };
        std::atomic<std::uint32_t> g_LastHealMission{ 0xFFFFFFFFu };
        constexpr std::uint64_t kHealIntervalMs = 10000;

        constexpr std::uint32_t kStaffCapacity = 0xDAC;
        constexpr std::size_t kOff_RemoverHeaders = 0x9E20;
        constexpr std::uint32_t kRemoverCapacity = 0x40;
        constexpr std::uint32_t kFaceFlag_MbIneligible = 0x00000008u;
        constexpr std::uint32_t kFaceFlag_HasFaceFova = 0x00020000u;
        constexpr std::uint32_t kFaceWord_Unique = 0x80000000u;

        struct Recruitment
        {
            std::uint64_t originalId = 0;
            std::uint64_t proxyId = 0;
            std::int32_t faceId = -1;
            std::int32_t donorId = -1;
            bool managed = false;
            bool existing = false;
            bool reserved = false;
        };

        std::uint16_t HeaderFaceId(std::uint64_t staffId)
        {
            return static_cast<std::uint16_t>((staffId >> 20) & 0x3FF);
        }

        std::uint64_t WithHeaderFaceId(std::uint64_t staffId, std::uint16_t faceId)
        {
            return (staffId & ~0x3FF00000ull) | (static_cast<std::uint64_t>(faceId) << 20);
        }

        bool IsValidStaffId(std::uint64_t staffId)
        {
            return staffId <= kMaxStaffId && ((staffId >> 7) & 0x3F) != 0;
        }

        bool IsMintedFaceId(std::int32_t faceId)
        {
            return IsFaceIdInPool(faceId) && (!IsFaceRowDefined(faceId) || HasFrameworkFaceMark(faceId));
        }

        bool IsPlainVanillaFace(std::int32_t faceId)
        {
            if (faceId < 0 || faceId >= 600 || IsFaceIdInPool(faceId))
                return false;
            FaceDiagnostics diagnostics;
            return ReadFaceDiagnostics(faceId, diagnostics) && diagnostics.rowDefined
                && (diagnostics.flags & kFaceFlag_MbIneligible) == 0
                && (diagnostics.flags & kFaceFlag_HasFaceFova) != 0
                && (diagnostics.fovaWord & kFaceWord_Unique) == 0;
        }

        std::string StaffKey(std::uint64_t staffId)
        {
            char text[17]{};
            std::snprintf(text, sizeof(text), "%016llX", static_cast<unsigned long long>(staffId));
            return text;
        }

        bool ParseStaffKey(const char* text, std::uint64_t& staffId)
        {
            if (!text)
                return false;
            staffId = 0;
            for (std::size_t i = 0; i < 16; ++i)
            {
                const char c = text[i];
                std::uint32_t digit = 0;
                if (c >= '0' && c <= '9')
                    digit = static_cast<std::uint32_t>(c - '0');
                else if (c >= 'A' && c <= 'F')
                    digit = static_cast<std::uint32_t>(c - 'A' + 10);
                else
                    return false;
                staffId = (staffId << 4) | digit;
            }
            return text[16] == '\0' && IsValidStaffId(staffId);
        }

        void* QuarkTable()
        {
            using GetQuarkSystemTable_t = void* (__fastcall*)();
            const auto getTable = reinterpret_cast<GetQuarkSystemTable_t>(
                ResolveGameAddress(gAddr.GetQuarkSystemTable));
            return getTable ? getTable() : nullptr;
        }

        void* CurrentStaffController_Guarded()
        {
            __try
            {
                const auto table = static_cast<std::uint8_t*>(QuarkTable());
                if (!table)
                    return nullptr;
                const auto holder = *reinterpret_cast<std::uint8_t**>(table + 0x98);
                if (!holder)
                    return nullptr;
                const auto management = *reinterpret_cast<std::uint8_t**>(holder + 0x110);
                return management ? *reinterpret_cast<void**>(management + 0xAC0) : nullptr;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return nullptr;
            }
        }

        bool CountIdentity_Guarded(void* controller, std::uint64_t staffId, std::uint32_t& matches)
        {
            matches = 0;
            __try
            {
                if (!controller)
                    return false;
                const auto bytes = static_cast<std::uint8_t*>(controller);
                const auto headers = *reinterpret_cast<const std::uint32_t**>(bytes + 0x9C78);
                const auto seeds = *reinterpret_cast<const std::uint32_t**>(bytes + 0x9C80);
                const std::uint32_t count = *reinterpret_cast<const std::uint16_t*>(bytes + 0x9E6C);
                if (!headers || !seeds || count > 0xDAC)
                    return false;
                const auto header = static_cast<std::uint32_t>(staffId);
                const auto seed = static_cast<std::uint32_t>(staffId >> 21) & 0xFFFFF800u;
                for (std::uint32_t i = 0; i < count; ++i)
                    if (headers[i] == header && seeds[i] == seed)
                        ++matches;
                return count == *reinterpret_cast<const std::uint16_t*>(bytes + 0x9E6C)
                    && headers == *reinterpret_cast<const std::uint32_t**>(bytes + 0x9C78)
                    && seeds == *reinterpret_cast<const std::uint32_t**>(bytes + 0x9C80);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        bool ReadLocalPlayerIdentity_Guarded(std::uint64_t& staffId)
        {
            __try
            {
                const auto table = static_cast<std::uint8_t*>(QuarkTable());
                if (!table)
                    return false;
                const auto holder = *reinterpret_cast<std::uint8_t**>(table + 0x98);
                if (!holder)
                    return false;
                const auto vars = *reinterpret_cast<std::uint8_t**>(holder + 0x10);
                if (!vars)
                    return false;
                const std::uint32_t header = *reinterpret_cast<const std::uint32_t*>(vars + 0x134);
                const std::uint32_t seed = *reinterpret_cast<const std::uint32_t*>(vars + 0x138);
                if ((seed & 0x7FF) != 0)
                    return false;
                staffId = (static_cast<std::uint64_t>(seed) << 21) | header;
                return IsValidStaffId(staffId);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        bool ReadLocalPlayerFace_Guarded(std::uint8_t& playerType, std::uint16_t& faceId)
        {
            __try
            {
                const auto table = static_cast<std::uint8_t*>(QuarkTable());
                if (!table)
                    return false;
                const auto holder = *reinterpret_cast<std::uint8_t**>(table + 0x98);
                if (!holder)
                    return false;
                const auto vars = *reinterpret_cast<std::uint8_t**>(holder + 0x10);
                if (!vars)
                    return false;
                playerType = vars[0xFB];
                faceId = *reinterpret_cast<const std::uint16_t*>(vars + 0xFC);
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        void CollectProxyOwner(const char* name, std::int32_t value)
        {
            std::uint64_t originalId = 0;
            if (!ParseStaffKey(name, originalId) || !IsFaceIdInPool(HeaderFaceId(originalId))
                || value < 1 || value > 600 || IsFaceIdInPool(value - 1))
                return;
            const std::uint64_t proxyId = WithHeaderFaceId(originalId, static_cast<std::uint16_t>(value - 1));
            const auto inserted = g_PersistedOwners.emplace(proxyId, originalId);
            if (!inserted.second && inserted.first->second != originalId)
                g_ConflictingProxies.insert(proxyId);
        }

        void RefreshProxyOwners_NoLock()
        {
            g_PersistedOwners.clear();
            g_ConflictingProxies.clear();
            V_FrameWorkState::ForEachPersistedConstant(kProxySpace, &CollectProxyOwner);
        }

        std::map<std::int32_t, std::uint32_t> g_LinksByFace;
        std::atomic<bool> g_ReportedUnregisteredFaces{ false };

        void CollectStaffLink(const char* name, std::int32_t value)
        {
            std::uint64_t proxyId = 0;
            if (ParseStaffKey(name, proxyId) && value >= 1 && IsFaceIdInPool(value - 1))
                ++g_LinksByFace[value - 1];
        }

        void ReportUnregisteredStaffFaces()
        {
            if (g_ReportedUnregisteredFaces.exchange(true, std::memory_order_relaxed))
                return;
            std::map<std::int32_t, std::uint32_t> links;
            {
                std::lock_guard<std::mutex> lock(g_Mutex);
                g_LinksByFace.clear();
                V_FrameWorkState::ForEachPersistedConstant(kFaceSpace, &CollectStaffLink);
                links.swap(g_LinksByFace);
            }
            for (const auto& face : links)
            {
                std::string key;
                if (!FindUnregisteredFaceKey(face.first, key))
                    continue;
                LogWarn("StaffFace: %u staff linked to unregistered face '%s' (faceId %d), vanilla stand-in shown",
                    face.second, key.c_str(), face.first);
            }
        }

        bool IsCompatibleDonor(std::int32_t donorId, const FaceFallbackMetadata& metadata)
        {
            if (donorId < 0 || donorId >= 600 || IsFaceIdInPool(donorId))
                return false;
            FaceDiagnostics diagnostics;
            if (!ReadFaceDiagnostics(donorId, diagnostics) || !diagnostics.rowDefined)
                return false;
            return PickCompatibleVanillaDonorFaceId(metadata.faceId, metadata.gender,
                static_cast<std::int32_t>(diagnostics.flags & 7), donorId) == donorId;
        }

        bool CandidateAvailable_NoLock(void* controller, const Recruitment& request, std::int32_t donorId)
        {
            const std::uint64_t proxyId = WithHeaderFaceId(request.originalId, static_cast<std::uint16_t>(donorId));
            if (g_ConflictingProxies.count(proxyId) || g_PendingOwners.count(proxyId))
                return false;
            const auto owner = g_PersistedOwners.find(proxyId);
            if (owner != g_PersistedOwners.end() && owner->second != request.originalId)
                return false;
            const std::string key = StaffKey(proxyId);
            if (V_FrameWorkState::GetPersistedConstant(kFaceSpace, key.c_str()) != 0)
                return false;
            std::uint32_t matches = 0;
            return CountIdentity_Guarded(controller, proxyId, matches) && matches == 0;
        }

        std::int32_t ChooseProxyDonor_NoLock(void* controller, const Recruitment& request,
                                             const FaceFallbackMetadata* metadata)
        {
            std::int32_t first = -1;
            if (metadata)
                first = PickCompatibleVanillaDonorFaceId(request.faceId, metadata->gender,
                                                         metadata->race, metadata->donorFaceId);
            if (first < 0)
                first = PickVanillaDonorFaceId(static_cast<std::uint32_t>(request.faceId));
            if (first >= 0 && CandidateAvailable_NoLock(controller, request, first))
                return first;
            const std::int32_t start = first < 0 ? 0 : first;
            for (std::int32_t pass = metadata ? 0 : 1; pass < 2; ++pass)
            {
                for (std::int32_t offset = 1; offset < 600; ++offset)
                {
                    const std::int32_t donor = (start + offset) % 600;
                    if (!IsPlainVanillaFace(donor) || (pass == 0 && !IsCompatibleDonor(donor, *metadata))
                        || !CandidateAvailable_NoLock(controller, request, donor))
                        continue;
                    return donor;
                }
            }
            return first >= 0 ? first : 0;
        }

        bool StoreAssociation(const Recruitment& request, bool present)
        {
            const std::string originalKey = StaffKey(request.originalId);
            const std::string proxyKey = StaffKey(request.proxyId);
            {
                V_FrameWorkState::SaveBatch batch;
                V_FrameWorkState::SetPersistedConstant(kProxySpace, originalKey.c_str(),
                    present ? request.donorId + 1 : 0);
                V_FrameWorkState::SetPersistedConstant(kFaceSpace, proxyKey.c_str(),
                    present ? request.faceId + 1 : 0);
            }
            return V_FrameWorkState::PersistNow()
                && V_FrameWorkState::GetPersistedConstant(kProxySpace, originalKey.c_str())
                    == (present ? request.donorId + 1 : 0)
                && V_FrameWorkState::GetPersistedConstant(kFaceSpace, proxyKey.c_str())
                    == (present ? request.faceId + 1 : 0);
        }

        bool PrepareRecruitment(void* controller, std::uint64_t staffId, Recruitment& request)
        {
            request.originalId = staffId;
            request.proxyId = staffId;
            request.faceId = HeaderFaceId(staffId);
            if (!IsMintedFaceId(request.faceId))
                return true;
            request.managed = true;
            FaceFallbackMetadata metadata;
            if (!IsValidStaffId(staffId) || !GetFaceFallbackMetadata(request.faceId, metadata)
                || controller != CurrentStaffController_Guarded())
                return false;
            std::uint32_t originalMatches = 0;
            if (!CountIdentity_Guarded(controller, staffId, originalMatches) || originalMatches != 0)
                return false;
            std::lock_guard<std::mutex> lock(g_Mutex);
            RefreshProxyOwners_NoLock();
            const std::string originalKey = StaffKey(staffId);
            const std::int32_t savedDonor = V_FrameWorkState::GetPersistedConstant(kProxySpace, originalKey.c_str());
            if (savedDonor != 0)
            {
                if (savedDonor < 1 || savedDonor > 600)
                    return false;
                request.donorId = savedDonor - 1;
                if (!IsCompatibleDonor(request.donorId, metadata))
                    return false;
                request.proxyId = WithHeaderFaceId(staffId, static_cast<std::uint16_t>(request.donorId));
                const std::string proxyKey = StaffKey(request.proxyId);
                std::uint32_t matches = 0;
                if (g_ConflictingProxies.count(request.proxyId)
                    || g_PendingOwners.count(request.proxyId)
                    || V_FrameWorkState::GetPersistedConstant(kFaceSpace, proxyKey.c_str()) != request.faceId + 1
                    || !CountIdentity_Guarded(controller, request.proxyId, matches) || matches > 1
                    || !V_FrameWorkState::PersistNow())
                    return false;
                request.existing = true;
            }
            else
            {
                const std::int32_t preferred = PickCompatibleVanillaDonorFaceId(
                    request.faceId, metadata.gender, metadata.race, metadata.donorFaceId);
                if (preferred >= 0 && CandidateAvailable_NoLock(controller, request, preferred))
                    request.donorId = preferred;
                for (std::int32_t pass = 0; request.donorId < 0 && pass < 2; ++pass)
                {
                    for (std::int32_t donor = 0; donor < 600; ++donor)
                    {
                        if (IsFaceIdInPool(donor))
                            continue;
                        FaceDiagnostics diagnostics;
                        if (!ReadFaceDiagnostics(donor, diagnostics) || !diagnostics.rowDefined
                            || (pass == 0 && static_cast<std::int32_t>(diagnostics.flags & 7) != metadata.race)
                            || !IsCompatibleDonor(donor, metadata)
                            || !CandidateAvailable_NoLock(controller, request, donor))
                            continue;
                        request.donorId = donor;
                        break;
                    }
                }
                if (request.donorId < 0)
                    return false;
                request.proxyId = WithHeaderFaceId(staffId, static_cast<std::uint16_t>(request.donorId));
                if (!StoreAssociation(request, true))
                {
                    if (!StoreAssociation(request, false))
                        LogError("StaffFace: association rollback save failed for staff %016llX",
                            static_cast<unsigned long long>(staffId));
                    LogError("StaffFace: face association save failed for staff %016llX, recruited with vanilla face",
                        static_cast<unsigned long long>(staffId));
                    return false;
                }
            }
            g_PendingOwners.emplace(request.proxyId, staffId);
            request.reserved = true;
            return true;
        }

        void FinishRecruitment(void* controller, const Recruitment& request, bool nativeSucceeded)
        {
            if (!request.managed)
                return;
            std::lock_guard<std::mutex> lock(g_Mutex);
            if (request.reserved)
                g_PendingOwners.erase(request.proxyId);
            std::uint32_t matches = 0;
            const bool found = CountIdentity_Guarded(controller, request.proxyId, matches);
            if (!nativeSucceeded || !found || matches != 1)
            {
                if (!request.existing && (!nativeSucceeded || (found && matches == 0))
                    && !StoreAssociation(request, false))
                    LogError("StaffFace: association rollback save failed for staff %016llX",
                        static_cast<unsigned long long>(request.originalId));
                if (nativeSucceeded && (!found || matches != 1))
                    LogWarn("StaffFace: staff %016llX recruit not verifiable, saved association kept",
                        static_cast<unsigned long long>(request.proxyId));
                return;
            }
            if (!request.existing)
                LogInfo("StaffFace: staff %016llX recruited with custom face %d (stand-in face %d)",
                    static_cast<unsigned long long>(request.proxyId), request.faceId, request.donorId);
        }

        void PrepareFallbackRecruitment(void* controller, std::uint64_t staffId, Recruitment& request)
        {
            request = Recruitment{};
            request.originalId = staffId;
            request.faceId = HeaderFaceId(staffId);
            FaceFallbackMetadata metadata;
            const bool haveMetadata = GetFaceFallbackMetadata(request.faceId, metadata);
            {
                std::lock_guard<std::mutex> lock(g_Mutex);
                RefreshProxyOwners_NoLock();
                const std::string originalKey = StaffKey(staffId);
                const std::int32_t savedDonor =
                    V_FrameWorkState::GetPersistedConstant(kProxySpace, originalKey.c_str());
                if (savedDonor >= 1 && savedDonor <= 600 && IsPlainVanillaFace(savedDonor - 1))
                    request.donorId = savedDonor - 1;
                else
                    request.donorId = ChooseProxyDonor_NoLock(controller, request,
                                                              haveMetadata ? &metadata : nullptr);
            }
            request.proxyId = WithHeaderFaceId(staffId, static_cast<std::uint16_t>(request.donorId));
            if (!g_ReportedFallbackRecruit.exchange(true, std::memory_order_relaxed))
                LogWarn("StaffFace: staff %016llX custom face %d has no free stand-in, recruited with vanilla face %d",
                    static_cast<unsigned long long>(staffId), request.faceId, request.donorId);
        }

        struct HealTally
        {
            bool wrote = false;
            std::map<std::uint64_t, std::uint64_t> proxies;
        };

        std::uint64_t HealProxyFor_NoLock(void* controller, std::uint64_t originalId,
                                          const FaceFallbackMetadata* metadata,
                                          bool mayShareIdentity, HealTally& tally)
        {
            const std::string originalKey = StaffKey(originalId);
            const std::int32_t savedDonor =
                V_FrameWorkState::GetPersistedConstant(kProxySpace, originalKey.c_str());
            bool mayStore = true;
            if (savedDonor >= 1 && savedDonor <= 600 && IsPlainVanillaFace(savedDonor - 1))
            {
                const std::uint64_t savedProxy =
                    WithHeaderFaceId(originalId, static_cast<std::uint16_t>(savedDonor - 1));
                std::uint32_t matches = 0;
                if (mayShareIdentity
                    || (CountIdentity_Guarded(controller, savedProxy, matches) && matches == 0))
                {
                    return savedProxy;
                }
                mayStore = false;
            }

            Recruitment request;
            request.originalId = originalId;
            request.faceId = HeaderFaceId(originalId);
            request.donorId = ChooseProxyDonor_NoLock(controller, request, metadata);
            request.proxyId = WithHeaderFaceId(originalId, static_cast<std::uint16_t>(request.donorId));
            if (mayStore && metadata && IsValidStaffId(originalId) && !IsReusedFaceId(request.faceId)
                && IsCompatibleDonor(request.donorId, *metadata)
                && CandidateAvailable_NoLock(controller, request, request.donorId))
            {
                const std::string proxyKey = StaffKey(request.proxyId);
                V_FrameWorkState::SetPersistedConstant(kProxySpace, originalKey.c_str(), request.donorId + 1);
                V_FrameWorkState::SetPersistedConstant(kFaceSpace, proxyKey.c_str(), request.faceId + 1);
                g_PersistedOwners.emplace(request.proxyId, originalId);
                tally.wrote = true;
            }
            return request.proxyId;
        }

        std::uint64_t HealProxyFor(void* controller, std::uint64_t originalId,
                                   bool mayShareIdentity, HealTally& tally)
        {
            if (mayShareIdentity)
            {
                const auto chosen = tally.proxies.find(originalId);
                if (chosen != tally.proxies.end())
                    return chosen->second;
            }
            FaceFallbackMetadata metadata;
            const bool haveMetadata = GetFaceFallbackMetadata(HeaderFaceId(originalId), metadata);
            std::uint64_t proxyId = 0;
            {
                std::lock_guard<std::mutex> lock(g_Mutex);
                RefreshProxyOwners_NoLock();
                proxyId = HealProxyFor_NoLock(controller, originalId, haveMetadata ? &metadata : nullptr,
                                              mayShareIdentity, tally);
            }
            tally.proxies.emplace(originalId, proxyId);
            return proxyId;
        }

        bool ReadRoster_Guarded(void* controller, std::uint32_t*& headers, const std::uint32_t*& seeds,
                                std::uint32_t& count)
        {
            __try
            {
                const auto bytes = static_cast<std::uint8_t*>(controller);
                headers = *reinterpret_cast<std::uint32_t**>(bytes + 0x9C78);
                seeds = *reinterpret_cast<const std::uint32_t**>(bytes + 0x9C80);
                count = *reinterpret_cast<const std::uint16_t*>(bytes + 0x9E6C);
                if (count > kStaffCapacity)
                    count = kStaffCapacity;
                return headers && seeds;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        bool RosterLooksValid_Guarded(void* controller)
        {
            __try
            {
                const auto bytes = static_cast<const std::uint8_t*>(controller);
                const auto headers = *reinterpret_cast<const std::uint8_t* const*>(bytes + 0x9C78);
                const auto seeds = *reinterpret_cast<const std::uint8_t* const*>(bytes + 0x9C80);
                const std::uint32_t count = *reinterpret_cast<const std::uint16_t*>(bytes + 0x9E6C);
                return headers && seeds && count <= kStaffCapacity
                    && seeds - headers == static_cast<std::ptrdiff_t>(kStaffCapacity * sizeof(std::uint32_t));
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        bool ReadRosterEntry_Guarded(const std::uint32_t* headers, const std::uint32_t* seeds,
                                     std::uint32_t index, std::uint32_t& header, std::uint32_t& seed)
        {
            __try
            {
                header = headers[index];
                seed = seeds[index];
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        std::uint32_t* ReadRemoverRing_Guarded(void* controller)
        {
            __try
            {
                return *reinterpret_cast<std::uint32_t**>(static_cast<std::uint8_t*>(controller) + kOff_RemoverHeaders);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return nullptr;
            }
        }

        bool ReadHeader_Guarded(const std::uint32_t* headers, std::uint32_t index, std::uint32_t& header)
        {
            __try
            {
                header = headers[index];
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        bool ReplaceHeader_Guarded(std::uint32_t* headers, const std::uint32_t* seeds, std::uint32_t index,
                                   std::uint32_t expected, std::uint32_t expectedSeed, std::uint32_t header)
        {
            __try
            {
                if (seeds && seeds[index] != expectedSeed)
                    return false;
                return static_cast<std::uint32_t>(_InterlockedCompareExchange(
                    reinterpret_cast<volatile long*>(headers + index),
                    static_cast<long>(header), static_cast<long>(expected))) == expected;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        bool ReplaceLocalPlayerHeader_Guarded(std::uint32_t expected, std::uint32_t expectedSeed,
                                              std::uint32_t header)
        {
            __try
            {
                const auto table = static_cast<std::uint8_t*>(QuarkTable());
                if (!table)
                    return false;
                const auto holder = *reinterpret_cast<std::uint8_t**>(table + 0x98);
                if (!holder)
                    return false;
                const auto vars = *reinterpret_cast<std::uint8_t**>(holder + 0x10);
                if (!vars)
                    return false;
                if (*reinterpret_cast<const std::uint32_t*>(vars + 0x138) != expectedSeed)
                    return false;
                return static_cast<std::uint32_t>(_InterlockedCompareExchange(
                    reinterpret_cast<volatile long*>(vars + 0x134),
                    static_cast<long>(header), static_cast<long>(expected))) == expected;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        std::uint32_t HealRoster(void* controller, HealTally& tally)
        {
            std::uint32_t* headers = nullptr;
            const std::uint32_t* seeds = nullptr;
            std::uint32_t count = 0;
            if (!ReadRoster_Guarded(controller, headers, seeds, count))
                return 0;
            std::uint32_t healed = 0;
            for (std::uint32_t i = 0; i < count; ++i)
            {
                std::uint32_t header = 0;
                std::uint32_t seed = 0;
                if (!ReadRosterEntry_Guarded(headers, seeds, i, header, seed))
                    break;
                const std::uint64_t staffId = (static_cast<std::uint64_t>(seed) << 21) | header;
                if (!IsMintedFaceId(HeaderFaceId(staffId)))
                    continue;
                const std::uint64_t proxyId = HealProxyFor(controller, staffId, false, tally);
                if (ReplaceHeader_Guarded(headers, seeds, i, header, seed, static_cast<std::uint32_t>(proxyId)))
                    ++healed;
            }
            return healed;
        }

        std::uint32_t HealRemoverRing(void* controller)
        {
            std::uint32_t* ring = ReadRemoverRing_Guarded(controller);
            if (!ring)
                return 0;
            std::uint32_t healed = 0;
            for (std::uint32_t i = 0; i < kRemoverCapacity; ++i)
            {
                std::uint32_t header = 0;
                if (!ReadHeader_Guarded(ring, i, header))
                    break;
                const std::uint16_t faceId = HeaderFaceId(header);
                if (!IsMintedFaceId(faceId))
                    continue;
                std::int32_t donor = -1;
                {
                    std::lock_guard<std::mutex> lock(g_Mutex);
                    RefreshProxyOwners_NoLock();
                    for (const auto& owner : g_PersistedOwners)
                    {
                        if (static_cast<std::uint32_t>(owner.second) != header)
                            continue;
                        donor = HeaderFaceId(owner.first);
                        break;
                    }
                }
                if (donor < 0)
                    donor = PickVanillaDonorFaceId(faceId);
                if (donor < 0)
                    donor = 0;
                const auto healedHeader = static_cast<std::uint32_t>(
                    WithHeaderFaceId(header, static_cast<std::uint16_t>(donor)));
                if (ReplaceHeader_Guarded(ring, nullptr, i, header, 0, healedHeader))
                    ++healed;
            }
            return healed;
        }

        std::uint32_t HealLocalPlayerHeader(void* controller, HealTally& tally)
        {
            std::uint64_t staffId = 0;
            if (!ReadLocalPlayerIdentity_Guarded(staffId) || !IsMintedFaceId(HeaderFaceId(staffId)))
                return 0;
            const std::uint64_t proxyId = HealProxyFor(controller, staffId, true, tally);
            return ReplaceLocalPlayerHeader_Guarded(static_cast<std::uint32_t>(staffId),
                                                    static_cast<std::uint32_t>(staffId >> 21) & 0xFFFFF800u,
                                                    static_cast<std::uint32_t>(proxyId)) ? 1 : 0;
        }

        void HealStaffCarriers(void* controller)
        {
            if (!controller || !IsFaceSystemReady() || controller != CurrentStaffController_Guarded())
                return;
            std::lock_guard<std::mutex> healLock(g_HealMutex);
            std::uint32_t healed = 0;
            HealTally tally;
            {
                V_FrameWorkState::SaveBatch batch;
                healed += HealRoster(controller, tally);
                healed += HealRemoverRing(controller);
                healed += HealLocalPlayerHeader(controller, tally);
            }
            if (healed == 0)
                return;
            const bool saved = !tally.wrote || V_FrameWorkState::PersistNow();
            if (!saved && !g_ReportedHealSave.exchange(true, std::memory_order_relaxed))
                LogError("StaffFace: stand-in face save failed, healed staff show vanilla face after restart");
        }

        void __fastcall hkAddStaffShort(void* controller, std::uint64_t staffId, std::uint8_t a3, char a4,
                                        std::uint32_t a5, std::uint16_t a6, char a7)
        {
            NoteAddStaffThread();
            HealStaffCarriers(controller);
            Recruitment request;
            if (!PrepareRecruitment(controller, staffId, request))
                PrepareFallbackRecruitment(controller, staffId, request);
            g_OriginalShort(controller, request.proxyId, a3, a4, a5, a6, a7);
            FinishRecruitment(controller, request, true);
        }

        std::uint64_t __fastcall hkAddStaffFull(void* controller, std::uint64_t staffId, std::uint8_t a3, char a4,
                                               std::uint64_t a5, std::uint64_t a6, std::uint64_t a7,
                                               std::uint64_t a8, std::uint64_t a9, std::uint64_t a10,
                                               std::uint64_t a11, std::uint64_t a12)
        {
            NoteAddStaffThread();
            HealStaffCarriers(controller);
            Recruitment request;
            if (!PrepareRecruitment(controller, staffId, request))
                PrepareFallbackRecruitment(controller, staffId, request);
            const std::uint64_t result = g_OriginalFull(controller, request.proxyId, a3, a4,
                a5, a6, a7, a8, a9, a10, a11, a12);
            FinishRecruitment(controller, request, result != 0);
            return result;
        }

        bool TryBuildStaffPortraitHeader(void* controller, std::uint32_t header,
                                         std::uint32_t seed, std::uint32_t& visualHeader)
        {
            visualHeader = header;
            if (!controller || controller != g_PortraitContextController || (seed & 0x7FFu) != 0
                || controller != CurrentStaffController_Guarded())
                return false;
            const std::uint64_t staffId = (static_cast<std::uint64_t>(seed) << 21) | header;
            const std::uint16_t nativeFace = HeaderFaceId(staffId);
            std::uint16_t visualFace = nativeFace;
            if (!TryResolveStaffVisualFace(staffId, nativeFace, visualFace) || visualFace == nativeFace)
                return false;
            FaceDiagnostics diagnostics;
            if (!ReadFaceDiagnostics(visualFace, diagnostics) || !diagnostics.rowDefined
                || diagnostics.uiTextureCount == 0 || diagnostics.uiTextureCount > 3
                || diagnostics.uiTexturePaths[0] == 0)
                return false;
            visualHeader = static_cast<std::uint32_t>(WithHeaderFaceId(header, visualFace));
            return true;
        }

        std::uint64_t* __fastcall hkStaffSeedToFaceTexturePathId(
            void* controller, std::uint64_t* outPath, std::uint32_t header, std::uint32_t seed)
        {
            std::uint32_t visualHeader = header;
            if (outPath && TryBuildStaffPortraitHeader(controller, header, seed, visualHeader))
            {
                std::uint64_t* result = g_OriginalPortrait(controller, outPath, visualHeader, seed);
                if (result && *result != 0)
                {
                    return result;
                }
            }
            return g_OriginalPortrait(controller, outPath, header, seed);
        }

        void* ReadOwnPortraitController_Guarded(const void* ui)
        {
            __try
            {
                if (!ui)
                    return nullptr;
                const auto* bytes = static_cast<const std::uint8_t*>(ui);
                const std::uint8_t mode = bytes[0x1C91];
                return mode == 13 || mode == 15 ? nullptr
                    : *reinterpret_cast<void* const*>(bytes + 0x50);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return nullptr;
            }
        }

        void __fastcall hkRefreshStaffFaceRecord(void* ui, void* record,
                                                  std::uint16_t slot, std::int16_t mode)
        {
            void* previous = g_PortraitContextController;
            g_PortraitContextController = ReadOwnPortraitController_Guarded(ui);
            __try
            {
                g_OriginalRefreshFaceRecord(ui, record, slot, mode);
            }
            __finally
            {
                g_PortraitContextController = previous;
            }
        }

        bool RefreshFaceRecordSiteMatches_Guarded(const void* target)
        {
            constexpr std::uint8_t expected[] = {
                0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18,
                0x48, 0x89, 0x74, 0x24, 0x20, 0x57, 0x41, 0x56, 0x41, 0x57,
                0x48, 0x83, 0xEC, 0x30, 0x0F, 0xB6, 0x81, 0x91, 0x1C, 0x00, 0x00
            };
            __try
            {
                return target && std::memcmp(target, expected, sizeof(expected)) == 0;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        bool PortraitSiteMatches_Guarded(const void* target)
        {
            constexpr std::uint8_t expected[] = {
                0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74,
                0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x20,
                0x45, 0x33, 0xDB, 0x41, 0x8B, 0xC0, 0x41, 0x8B, 0xF9
            };
            __try
            {
                return target && std::memcmp(target, expected, sizeof(expected)) == 0;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        using CanSetMotherBase_t = std::uint32_t (__fastcall*)(void*, void*, std::uint16_t*, std::uint16_t);
        using EventFobStaff_t = std::uint64_t (__fastcall*)(void*, std::uint64_t, std::uint64_t, std::uint64_t,
                                                           std::uint64_t, std::uint64_t, std::uint64_t);

        constexpr std::size_t kFaceSlot_CanSetMotherBase = 0x20;
        constexpr std::size_t kStaffSlot_EventFobStaff = 0x1C8;

        CanSetMotherBase_t g_OriginalCanSetMotherBase = nullptr;
        EventFobStaff_t g_OriginalEventFobStaff = nullptr;
        void* g_CanSetMotherBaseTarget = nullptr;
        void* g_EventFobStaffTarget = nullptr;
        std::mutex g_FobGuardMutex;
        bool g_FobGuardTried = false;
        bool g_FobGuardReady = false;
        int g_StaffHooksState = 0;
        thread_local int t_FobStaffGeneration = 0;

        bool EnableHookNow(void* target, void* detour, void** original)
        {
            const MH_STATUS created = MH_CreateHook(target, detour, original);
            if (created != MH_OK)
            {
                if (created == MH_ERROR_MEMORY_ALLOC)
                    HookArena::NoteExhausted();
                *original = nullptr;
                return false;
            }
            if (MH_EnableHook(target) != MH_OK)
            {
                MH_RemoveHook(target);
                *original = nullptr;
                return false;
            }
            return true;
        }

        bool HookJumpPresent_Guarded(const void* target)
        {
            __try
            {
                const auto bytes = static_cast<const std::uint8_t*>(target);
                return bytes && (bytes[0] == 0xE9 || bytes[0] == 0xEB);
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        bool FobFlagsSet_Guarded()
        {
            __try
            {
                const auto table = static_cast<std::uint8_t*>(QuarkTable());
                if (!table)
                    return false;
                const auto holder = *reinterpret_cast<std::uint8_t**>(table + 0x98);
                if (!holder)
                    return false;
                const auto vars = *reinterpret_cast<std::uint8_t**>(holder + 0x10);
                if (!vars)
                    return false;
                return vars[0x24] != 0;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return true;
            }
        }

        bool IsFobStaffContext()
        {
            return t_FobStaffGeneration > 0 || MissionCodeGuard::ShouldBypassHooks() || FobFlagsSet_Guarded();
        }

        std::uint32_t __fastcall hkGetSoldierFaceIdCanSetMotherBase(void* self, void* info,
                                                                    std::uint16_t* out, std::uint16_t cap)
        {
            const std::uint32_t found = g_OriginalCanSetMotherBase(self, info, out, cap);
            if (!out || found == 0 || !IsFobStaffContext())
                return found;
            std::uint32_t kept = 0;
            for (std::uint32_t i = 0; i < found; ++i)
            {
                const std::uint16_t faceId = out[i];
                if (IsFaceIdInPool(faceId) && HasFrameworkFaceMark(faceId))
                    continue;
                out[kept++] = faceId;
            }
            return kept;
        }

        std::uint64_t __fastcall hkGenerateEventFobStaff(void* self, std::uint64_t a2, std::uint64_t a3,
                                                         std::uint64_t a4, std::uint64_t a5, std::uint64_t a6,
                                                         std::uint64_t a7)
        {
            std::uint64_t result = 0;
            ++t_FobStaffGeneration;
            __try
            {
                result = g_OriginalEventFobStaff(self, a2, a3, a4, a5, a6, a7);
            }
            __finally
            {
                --t_FobStaffGeneration;
            }
            return result;
        }

        void* FaceSystemFromQuark_Guarded()
        {
            __try
            {
                const auto table = static_cast<std::uint8_t*>(QuarkTable());
                if (!table)
                    return nullptr;
                const auto holder = *reinterpret_cast<std::uint8_t**>(table + 0x98);
                return holder ? *reinterpret_cast<void**>(holder + 0x2D8) : nullptr;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return nullptr;
            }
        }

        void* ReadVtableSlot_Guarded(void* object, std::size_t slot)
        {
            __try
            {
                if (!object)
                    return nullptr;
                void* const* vtable = *reinterpret_cast<void* const* const*>(object);
                return vtable ? vtable[slot / sizeof(void*)] : nullptr;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return nullptr;
            }
        }

        bool CanSetMotherBaseMatches_Guarded(const void* target)
        {
            constexpr std::uint8_t expected[] = {
                0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10,
                0x48, 0x89, 0x7C, 0x24, 0x18, 0x4C, 0x89, 0x74, 0x24, 0x20,
                0x8B, 0x7A, 0x08, 0x8B, 0x42, 0x04
            };
            __try
            {
                return target && std::memcmp(target, expected, sizeof(expected)) == 0;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        bool EventFobStaffMatches_Guarded(const void* target)
        {
            constexpr std::uint8_t head[] = {
                0x40, 0x55, 0x56, 0x57, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x68, 0x48, 0x8B, 0x05
            };
            constexpr std::uint8_t tail[] = {
                0x48, 0x33, 0xC4, 0x48, 0x89, 0x44, 0x24, 0x38, 0x41, 0x0F, 0xB6, 0xF1,
                0x41, 0x0F, 0xB6, 0xE8, 0x44, 0x0F, 0xB6, 0xF2, 0x48, 0x8B, 0xF9,
                0x41, 0x80, 0xF8, 0x3F
            };
            __try
            {
                const auto bytes = static_cast<const std::uint8_t*>(target);
                return bytes && std::memcmp(bytes, head, sizeof(head)) == 0
                    && std::memcmp(bytes + sizeof(head) + 4, tail, sizeof(tail)) == 0;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }

        void InstallStaffPortraitHook()
        {
            void* target = ResolveGameAddress(gAddr.StaffControllerImpl_StaffSeedToFaceTexturePathId);
            void* refresh = ResolveGameAddress(gAddr.StaffListCallbackImpl_RefreshFaceRecord);
            if (!PortraitSiteMatches_Guarded(target))
            {
                LogError("StaffFace: portrait address missing or code mismatch, portraits show stand-in face");
                return;
            }
            if (!CreateAndEnableHook(target, &hkStaffSeedToFaceTexturePathId,
                                     reinterpret_cast<void**>(&g_OriginalPortrait)))
            {
                LogError("StaffFace: portrait hook failed, portraits show stand-in face");
                return;
            }
            if (!RefreshFaceRecordSiteMatches_Guarded(refresh))
            {
                LogError("StaffFace: staff list address missing or code mismatch, list shows stand-in portraits");
                return;
            }
            if (!CreateAndEnableHook(refresh, &hkRefreshStaffFaceRecord,
                                     reinterpret_cast<void**>(&g_OriginalRefreshFaceRecord)))
                LogError("StaffFace: staff list hook failed, list shows stand-in portraits");
        }
    }

    bool TryResolveStaffVisualFace(std::uint64_t staffId, std::uint16_t nativeFaceId,
                                   std::uint16_t& visualFaceId)
    {
        if (!IsValidStaffId(staffId) || HeaderFaceId(staffId) != nativeFaceId)
            return false;
        const std::string proxyKey = StaffKey(staffId);
        const std::int32_t savedFace = V_FrameWorkState::GetPersistedConstant(kFaceSpace, proxyKey.c_str());
        if (savedFace < 1 || savedFace > 900)
            return false;
        const std::int32_t faceId = savedFace - 1;
        if (!IsFaceIdInPool(faceId) || !IsManagedFaceId(faceId) || !IsFaceRowDefined(faceId)
            || IsFaceRowDefinedByOtherMod(faceId) || IsCustomFaceFallback(faceId))
            return false;
        const std::uint64_t originalId = WithHeaderFaceId(staffId, static_cast<std::uint16_t>(faceId));
        const std::string originalKey = StaffKey(originalId);
        FaceFallbackMetadata metadata;
        if (V_FrameWorkState::GetPersistedConstant(kProxySpace, originalKey.c_str()) != nativeFaceId + 1
            || !GetFaceFallbackMetadata(faceId, metadata) || !IsCompatibleDonor(nativeFaceId, metadata))
            return false;
        std::uint32_t matches = 0;
        if (!CountIdentity_Guarded(CurrentStaffController_Guarded(), staffId, matches) || matches != 1)
            return false;
        visualFaceId = static_cast<std::uint16_t>(faceId);
        return true;
    }

    bool TryResolveLocalPlayerStaffFace(std::uint16_t nativeFaceId, std::uint16_t& visualFaceId)
    {
        std::uint64_t staffId = 0;
        return ReadLocalPlayerIdentity_Guarded(staffId)
            && TryResolveStaffVisualFace(staffId, nativeFaceId, visualFaceId);
    }

    void* EnterOwnRosterPortraitContext(void* staffController)
    {
        void* previous = g_PortraitContextController;
        g_PortraitContextController = staffController;
        return previous;
    }

    void LeaveOwnRosterPortraitContext(void* previous)
    {
        g_PortraitContextController = previous;
    }

    bool IsLocalPlayerWearingCustomFace(std::uint16_t& customFaceId)
    {
        std::uint8_t playerType = 0;
        std::uint16_t faceId = 0;
        if (!ReadLocalPlayerFace_Guarded(playerType, faceId) || !IsDdPlayerType(playerType))
            return false;
        if (IsMintedFaceId(faceId))
        {
            customFaceId = faceId;
            return true;
        }
        std::uint16_t visualFaceId = faceId;
        if (!TryResolveLocalPlayerStaffFace(faceId, visualFaceId) || visualFaceId == faceId)
            return false;
        customFaceId = visualFaceId;
        return true;
    }

    void PollStaffFaceHeal(std::uint32_t missionCode)
    {
        if (MissionCodeGuard::ShouldBypassHooks())
            return;
        if (missionCode >= 10000 && !g_ReportedUnregisteredFaces.load(std::memory_order_relaxed)
            && IsFaceSystemReady())
            ReportUnregisteredStaffFaces();
        const DWORD addStaffThread = g_AddStaffThread.load(std::memory_order_relaxed);
        if (g_AddStaffOnSeveralThreads.load(std::memory_order_relaxed)
            || (addStaffThread != 0 && addStaffThread != GetCurrentThreadId()))
        {
            if (!g_ReportedHealThread.exchange(true, std::memory_order_relaxed))
                LogWarn("StaffFace: AddStaff runs on another thread, staff records healed only on recruit");
            return;
        }
        const std::uint64_t now = GetTickCount64();
        if (missionCode == g_LastHealMission.load(std::memory_order_relaxed)
            && now - g_LastHealTick.load(std::memory_order_relaxed) < kHealIntervalMs)
            return;
        g_LastHealMission.store(missionCode, std::memory_order_relaxed);
        g_LastHealTick.store(now, std::memory_order_relaxed);
        void* controller = CurrentStaffController_Guarded();
        if (!controller)
            return;
        if (!RosterLooksValid_Guarded(controller))
        {
            if (!g_ReportedRosterLayout.exchange(true, std::memory_order_relaxed))
                LogWarn("StaffFace: staff roster layout unexpected, staff records healed only on recruit");
            return;
        }
        HealStaffCarriers(controller);
    }

    bool Install_StaffFaceContainment()
    {
        void* shortBody = ResolveGameAddress(gAddr.StaffControllerImpl_AddStaffShort);
        void* fullBody = ResolveGameAddress(gAddr.StaffControllerImpl_AddStaffFull);
        if (!shortBody || !fullBody)
        {
            LogError("StaffFace: AddStaff address missing, custom staff faces off");
            return false;
        }
        const bool shortOk = CreateAndEnableHook(shortBody, &hkAddStaffShort,
                                                 reinterpret_cast<void**>(&g_OriginalShort));
        const bool fullOk = CreateAndEnableHook(fullBody, &hkAddStaffFull,
                                                reinterpret_cast<void**>(&g_OriginalFull));
        if (!shortOk || !fullOk)
        {
            if (shortOk)
            {
                DisableAndRemoveHook(shortBody);
                g_OriginalShort = nullptr;
            }
            if (fullOk)
            {
                DisableAndRemoveHook(fullBody);
                g_OriginalFull = nullptr;
            }
            LogError("StaffFace: AddStaff hook failed (short=%d full=%d), custom staff faces off",
                shortOk ? 1 : 0, fullOk ? 1 : 0);
            return false;
        }
        InstallStaffPortraitHook();
        return true;
    }

    bool EnsureFobFaceGuard()
    {
        std::lock_guard<std::mutex> lock(g_FobGuardMutex);
        if (g_FobGuardTried)
            return g_FobGuardReady;
        if (g_StaffHooksState == 2 || !g_OriginalShort || !g_OriginalFull)
            return false;
        void* faceSystem = FaceSystemFromQuark_Guarded();
        void* staffController = CurrentStaffController_Guarded();
        if (!faceSystem || !staffController)
            return false;
        g_FobGuardTried = true;
        void* canSet = ReadVtableSlot_Guarded(faceSystem, kFaceSlot_CanSetMotherBase);
        void* eventFob = ReadVtableSlot_Guarded(staffController, kStaffSlot_EventFobStaff);
        if (!CanSetMotherBaseMatches_Guarded(canSet) || !EventFobStaffMatches_Guarded(eventFob))
        {
            LogError("StaffFace: FOB face guard code mismatch, mbEligible faces stay out of random picks");
            return false;
        }
        if (!EnableHookNow(canSet, &hkGetSoldierFaceIdCanSetMotherBase,
                           reinterpret_cast<void**>(&g_OriginalCanSetMotherBase)))
        {
            LogError("StaffFace: FOB face guard hook failed, mbEligible faces stay out of random picks");
            return false;
        }
        if (!EnableHookNow(eventFob, &hkGenerateEventFobStaff,
                           reinterpret_cast<void**>(&g_OriginalEventFobStaff)))
        {
            MH_DisableHook(canSet);
            MH_RemoveHook(canSet);
            g_OriginalCanSetMotherBase = nullptr;
            LogError("StaffFace: FOB staff guard hook failed, mbEligible faces stay out of random picks");
            return false;
        }
        g_CanSetMotherBaseTarget = canSet;
        g_EventFobStaffTarget = eventFob;
        g_FobGuardReady = true;
        return true;
    }

    void NoteStaffHooksApplied(bool applied)
    {
        bool revoke = false;
        {
            std::lock_guard<std::mutex> lock(g_FobGuardMutex);
            const bool live = applied && g_OriginalShort && g_OriginalFull
                && HookJumpPresent_Guarded(ResolveGameAddress(gAddr.StaffControllerImpl_AddStaffShort))
                && HookJumpPresent_Guarded(ResolveGameAddress(gAddr.StaffControllerImpl_AddStaffFull));
            g_StaffHooksState = live ? 1 : 2;
            if (!live && g_FobGuardReady)
            {
                g_FobGuardReady = false;
                revoke = true;
            }
        }
        if (!revoke)
            return;
        RevokeMbEligibility();
        LogError("StaffFace: staff hooks not live, mbEligible faces taken out of random picks");
    }

    void Uninstall_StaffFaceContainment()
    {
        if (g_EventFobStaffTarget)
        {
            DisableAndRemoveHook(g_EventFobStaffTarget);
            g_EventFobStaffTarget = nullptr;
            g_OriginalEventFobStaff = nullptr;
        }
        if (g_CanSetMotherBaseTarget)
        {
            DisableAndRemoveHook(g_CanSetMotherBaseTarget);
            g_CanSetMotherBaseTarget = nullptr;
            g_OriginalCanSetMotherBase = nullptr;
        }
        if (g_OriginalPortrait)
        {
            DisableAndRemoveHook(ResolveGameAddress(gAddr.StaffControllerImpl_StaffSeedToFaceTexturePathId));
            g_OriginalPortrait = nullptr;
        }
        if (g_OriginalRefreshFaceRecord)
        {
            DisableAndRemoveHook(ResolveGameAddress(gAddr.StaffListCallbackImpl_RefreshFaceRecord));
            g_OriginalRefreshFaceRecord = nullptr;
        }
        if (g_OriginalShort)
        {
            DisableAndRemoveHook(ResolveGameAddress(gAddr.StaffControllerImpl_AddStaffShort));
            g_OriginalShort = nullptr;
        }
        if (g_OriginalFull)
        {
            DisableAndRemoveHook(ResolveGameAddress(gAddr.StaffControllerImpl_AddStaffFull));
            g_OriginalFull = nullptr;
        }
    }
}
