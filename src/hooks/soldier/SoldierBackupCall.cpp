#include <pch.h>
#include "SoldierBackupCall.h"

#include <Windows.h>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <mutex>

#include "AddressSet.h"
#include "FakeSurrender.h"
#include "FoxHashes.h"
#include "GameClock.h"
#include "HookUtils.h"
#include "log.h"
#include "MissionCodeGuard.h"
#include "FeatureSwitch.h"
#include "NoticeControllerImpl_CheckForceNotice.h"
#include "RadioControllerImpl_StepRadioWaitResponse.h"
#include "SoldierCommandPost.h"

namespace
{
    using StepProc_t      = void(__fastcall*)(std::uint8_t* noticeAi, std::uint32_t soldierIndex, int stepProc, std::uint8_t* knowledge);
    using PreUpdate_t     = void(__fastcall*)(std::uint8_t* node, std::uint64_t soldierIndex, std::uint8_t** knowledge);
    using GetNoticeType_t = int(__fastcall*)(void* noticeController, std::uint32_t soldierIndex);
    using IsInTower_t     = bool(__fastcall*)(void* utility, const float* position);

    struct NoticeNode   // where a notice AI keeps its step, and the steps a call for backup moves the guard between
    {
        const char*  what;
        std::size_t  stepAt;
        std::size_t  levelAt;
        std::uint8_t callHelp;   // "Let's check it out. On me."
        std::uint8_t waitHelp;   // stands with his eyes on the spot
        std::uint8_t goAlone;    // where vanilla sends him once his call is over
    };

    constexpr NoticeNode kSightNode = { "saw someone", 0x2C, 0x2D, 1, 6, 8 };       // NoticeIndisAiImpl
    constexpr NoticeNode kNoiseNode = { "heard something", 0x2E, 0x2F, 2, 7, 9 };   // NoticeNoiseAiImpl

    constexpr int           kProcEnter        = 0;
    constexpr int           kProcExit         = 1;
    constexpr int           kProcUpdate       = 3;
    constexpr int           kProcResume       = 4;
    constexpr std::uint8_t  kSearcherLevel    = 1;      // 2 is the comrade he takes along, 3 a guard who can't leave his post
    constexpr std::size_t   kSightCallFlags   = 0x30;
    constexpr std::uint8_t  kSightCallIsNoise = 0x10;   // the sight node then radios the noise report
    constexpr std::uint8_t  kPersonReport     = 0x29;   // CPR0290 "Spotted a suspicious individual. Going to check it out."
    constexpr std::uint8_t  kNoiseReport      = 0x2C;   // CPR0295 "Heard a strange noise just now. Gonna go check it out."

    constexpr std::size_t   kNodeSoldiers            = 0x70;
    constexpr std::size_t   kSoldierCount            = 0x08;
    constexpr std::size_t   kSoldierTransforms       = 0x18;
    constexpr std::size_t   kTransformActiveBits     = 0x08;
    constexpr std::size_t   kTransformPositions      = 0x20;   // one Vector4 per soldier
    constexpr std::size_t   kSoldierLife             = 0x38;   // 0x40 per soldier at +8
    constexpr std::size_t   kLifeRecords             = 0x08;
    constexpr std::size_t   kLifeRecordSize          = 0x40;
    constexpr std::size_t   kLifeFlags               = 0x12;
    constexpr std::uint16_t kLifeFlagsDown           = 0x3;
    constexpr std::size_t   kLifeState               = 0x3E;
    constexpr std::uint8_t  kLifeStateDown           = 2;
    constexpr std::size_t   kSoldierNoticeController = 0x68;
    constexpr std::size_t   kGetNoticeTypeVtblIndex  = 0x50 / 8;
    constexpr int           kNoNotice                = 0x3E;   // nothing on his mind, the only state vanilla's comrade searches take
    constexpr std::size_t   kSoldierCommandPost      = 0xD0;
    constexpr std::size_t   kCommandPostController   = 0x08;
    constexpr std::size_t   kCommandPostClient       = 0x30;
    constexpr std::size_t   kControllerClients       = 0x1F0;  // soldiers, helis... each with its own run of members
    constexpr std::size_t   kClientSize              = 0x48;
    constexpr std::size_t   kClientFirstMember       = 0x40;
    constexpr std::size_t   kSoldierStatus           = 0x110;  // 0x28 per soldier at +8, the count at +0x10
    constexpr std::size_t   kStatusRecords           = 0x08;
    constexpr std::size_t   kStatusCount             = 0x10;
    constexpr std::size_t   kStatusRecordSize        = 0x28;
    constexpr std::uint32_t kSkippedByComradeSearch  = 1u << 0xD;
    constexpr std::size_t   kStatusSyncFlags         = 0x24;
    constexpr std::uint32_t kSyncHeldUp              = 1u << 28;
    constexpr std::uint64_t kHeld                    = 1ull << 16;
    constexpr std::size_t   kSoldierParams           = 0x128;
    constexpr std::size_t   kParamsHearing           = 0x18;   // ten distances per weather, normal first: zero ss hs s m l hll ll alert special
    constexpr std::size_t   kHearingHll              = 6;      // 60 m
    constexpr float         kSendSearchPadding       = 20.0f;  // vanilla's send-someone search (CPR0291) reaches hll + 20 m
    constexpr std::size_t   kSoldierCommonUtility    = 0x138;
    constexpr std::size_t   kIsInTowerVtblIndex      = 0x30 / 8;

    constexpr std::size_t   kStepTimer       = 0x20;    // seconds in the current step: the node adds its dt, a step change zeroes it
    constexpr float         kNoticeLandsS    = 3.0f;    // his walk-over notice is posted on his next notice tick
    constexpr float         kLongestWaitS    = 90.0f;
    constexpr float         kNoticeClearS    = 2.0f;    // a 0x34 still on him turns "On me" away
    constexpr float         kBesideM         = 4.0f;
    constexpr float         kHeldShortM      = 8.0f;    // rocks, or the caller standing on the spot he walks to
    constexpr float         kMovedM          = 0.5f;
    constexpr std::uint64_t kHeldShortMs     = 1500;
    constexpr std::size_t   kNodeToSteps     = 0x20;    // NoticeNoiseAiImpl::PreUpdate's this, less this, is its steps' (every adjust is 0)
    constexpr std::size_t   kNoiseTargetId   = 0x24;
    constexpr std::size_t   kNoiseWakeKind   = 0x30;    // the notice kind the node woke for
    constexpr std::size_t   kNoiseStepFlags  = 0x31;    // cleared with the timer on a step change
    constexpr std::uint8_t  kNoiseStepEnd    = 0x15;
    constexpr std::size_t   kStepEntrySize   = 0x10;    // step function, this adjust
    constexpr std::uint64_t kLongestCallMs   = 60000;   // a busy CP can keep his call queued a while
    constexpr std::uint64_t kCallerGoneMs    = 5000;    // his wait step stopped running: he was interrupted
    constexpr std::uint32_t kNobody          = 0xFFFFFFFFu;
    constexpr std::uint16_t kNoMember        = 0xFFFF;
    constexpr std::uint16_t kSoldierIdBase   = 0x400;
    constexpr std::size_t   kMaxRequests     = 16;

    constexpr std::uint8_t   kNoiseRadioStartStep = 8;

    struct BuildSites
    {
        std::uintptr_t sightRadioStart;
        std::uintptr_t sightWaitHelp;
        std::uintptr_t noiseRadioStart;
        std::uintptr_t noiseWaitHelp;
        std::uintptr_t noisePreUpdate;
        std::uintptr_t noiseStepTable;   // filled at startup
    };


    const std::uint8_t kSightRadioStartEntry[] = { 0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x54, 0x41, 0x56, 0x41, 0x57, 0x48,
                                                   0x8D, 0x6C, 0x24, 0xC0, 0x48, 0x81, 0xEC, 0x40, 0x01, 0x00, 0x00 };
    const std::uint8_t kNoiseRadioStartEntry[] = { 0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48,
                                                   0x8D, 0x6C, 0x24, 0xC0, 0x48, 0x81, 0xEC, 0x40, 0x01, 0x00, 0x00 };
    const std::uint8_t kSightWaitHelpEntry[]   = { 0x40, 0x55, 0x56, 0x57, 0x48, 0x81, 0xEC, 0x10, 0x01, 0x00, 0x00,
                                                   0x48, 0x8B, 0x05 };   // the stack cookie's rel32 follows, different on each build
    const std::uint8_t kNoiseWaitHelpEntry[]   = { 0x40, 0x53, 0x56, 0x57, 0x48, 0x81, 0xEC, 0x10, 0x01, 0x00, 0x00,
                                                   0x48, 0x8B, 0x05 };
    const std::uint8_t kNoisePreUpdateEntry[]  = { 0x40, 0x53, 0x55, 0x56, 0x57, 0x41, 0x55, 0x41, 0x57, 0x48, 0x83, 0xEC,
                                                   0x78, 0x48, 0x8B, 0x05 };

    enum class Stage : std::uint8_t
    {
        Free,
        Asking,      // his call is queued or on air
        Coming,      // his backup walks over, he waits
        Arrived,     // the walk-over ended at his side; "On me" once the backup can hear it
        Cancelled,   // he stopped waiting: the backup's walk-over ends on the backup's next tick
    };

    struct Request
    {
        Stage         stage     = Stage::Free;
        bool          noise     = false;
        std::uint32_t caller    = kNobody;
        std::uint32_t backup    = kNobody;   // kNobody while asking: the CP has nobody to send
        std::uint16_t member    = kNoMember;
        std::uint32_t label     = 0;
        std::uint64_t askedMs   = 0;
        std::uint64_t seenMs    = 0;
        float         arrivedAt = -1.0f;     // how long the caller had waited when the backup got there
        float         anchor[3] = {};        // where the backup last moved from
        std::uint64_t anchorMs  = 0;
    };

    struct Taken   // soldiers in other calls for backup: nobody is sent twice
    {
        std::uint32_t soldiers[kMaxRequests * 2] = {};
        std::size_t   count = 0;

        bool Has(std::uint32_t index) const
        {
            for (std::size_t i = 0; i < count; ++i)
            {
                if (soldiers[i] == index)
                    return true;
            }
            return false;
        }
    };

    struct BackupState
    {
        bool  up         = false;
        int   noticeType = kNoNotice;
        float distance   = 0.0f;   // squared, to his caller
        float at[3]      = {};
    };

    std::mutex                 g_Mutex;
    Request                    g_Requests[kMaxRequests];
    std::atomic<std::uint32_t> g_Walking{ 0 };   // requests whose backup's walk-over is ours to end
    std::uint32_t g_PersonBackupLabel = 0;
    std::uint32_t g_PersonAloneLabel  = 0;
    std::uint32_t g_NoiseBackupLabel  = 0;
    std::uint32_t g_NoiseAloneLabel   = 0;

    StepProc_t g_OrigSightRadioStart   = nullptr;
    StepProc_t g_OrigSightWaitHelp     = nullptr;
    StepProc_t g_OrigNoiseRadioStart   = nullptr;
    StepProc_t    g_OrigNoiseWaitHelp     = nullptr;
    PreUpdate_t   g_OrigNoisePreUpdate    = nullptr;
    void*         g_SightRadioStartTarget = nullptr;
    void*         g_SightWaitHelpTarget   = nullptr;
    void*         g_NoiseRadioStartTarget = nullptr;
    void*         g_NoiseWaitHelpTarget   = nullptr;
    void*         g_NoisePreUpdateTarget  = nullptr;
    std::uint8_t* g_NoiseStepTable        = nullptr;

    template <typename T>
    T& At(std::uint8_t* base, std::size_t offset)
    {
        return *reinterpret_cast<T*>(base + offset);
    }

    const BuildSites* SitesFor()
    {
        static const BuildSites sites = { gAddr.NoticeSightAiImpl_RadioStart, gAddr.NoticeSightAiImpl_WaitHelp, gAddr.NoticeNoiseAiImpl_RadioStart, gAddr.NoticeNoiseAiImpl_WaitHelp, gAddr.NoticeNoiseAiImpl_PreUpdate, gAddr.NoticeNoiseAiImpl_StepTable };
        return &sites;
    }

    bool IsNoiseStepTable(std::uint8_t* table, const void* waitHelp, const void* radioStart)
    {
        __try
        {
            return At<const void*>(table, kNoiseNode.waitHelp * kStepEntrySize) == waitHelp
                && At<const void*>(table, kNoiseRadioStartStep * kStepEntrySize) == radioStart;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    Request* RequestOf(std::uint32_t caller)   // g_Mutex held
    {
        for (Request& request : g_Requests)
        {
            if (request.stage != Stage::Free && request.caller == caller)
                return &request;
        }
        return nullptr;
    }

    Request* FreeRequest()   // g_Mutex held
    {
        for (Request& request : g_Requests)
        {
            if (request.stage == Stage::Free)
                return &request;
        }
        return nullptr;
    }

    void RecountWalking()   // g_Mutex held
    {
        std::uint32_t walking = 0;
        for (const Request& request : g_Requests)
        {
            if (request.stage == Stage::Coming || request.stage == Stage::Cancelled)
                ++walking;
        }
        g_Walking.store(walking, std::memory_order_release);
    }

    void DropStale(std::uint64_t now)   // g_Mutex held; a caller whose node stopped running leaves his request behind
    {
        for (Request& request : g_Requests)
        {
            if (request.stage == Stage::Asking && now - request.askedMs > kLongestCallMs)
                request = Request{};
            else if (request.stage != Stage::Free && request.stage != Stage::Asking && now - request.seenMs > kCallerGoneMs)
                request = Request{};
        }
        RecountWalking();
    }

    Taken TakenBesides(std::uint32_t caller)   // g_Mutex held
    {
        Taken taken;
        for (const Request& request : g_Requests)
        {
            if (request.stage == Stage::Free || request.caller == caller)
                continue;
            taken.soldiers[taken.count++] = request.caller;
            if (request.backup != kNobody)
                taken.soldiers[taken.count++] = request.backup;
        }
        return taken;
    }

    const float* PositionOf(std::uint8_t* soldiers, std::uint32_t index)
    {
        return At<float*>(At<std::uint8_t*>(soldiers, kSoldierTransforms), kTransformPositions) + index * 4;
    }

    float SquaredDistance(const float* from, const float* to)
    {
        const float dx = to[0] - from[0];
        const float dy = to[1] - from[1];
        const float dz = to[2] - from[2];
        return dx * dx + dy * dy + dz * dz;
    }

    float Hearing(std::uint8_t* soldiers, std::size_t band)
    {
        return At<float*>(At<std::uint8_t*>(soldiers, kSoldierParams), kParamsHearing)[band];
    }

    std::uint8_t* StatusOf(std::uint8_t* soldiers, std::uint32_t index)
    {
        std::uint8_t* status = At<std::uint8_t*>(soldiers, kSoldierStatus);
        std::uint8_t* record = At<std::uint8_t*>(status, kStatusRecords);
        return index < At<std::uint32_t>(status, kStatusCount) ? record + index * kStatusRecordSize : record;
    }

    bool IsUp(std::uint8_t* soldiers, std::uint32_t index)   // standing, awake and nobody holding him up
    {
        std::uint8_t* life = At<std::uint8_t*>(At<std::uint8_t*>(soldiers, kSoldierLife), kLifeRecords) + index * kLifeRecordSize;
        if (At<std::uint8_t>(life, kLifeState) == kLifeStateDown || (At<std::uint16_t>(life, kLifeFlags) & kLifeFlagsDown) != 0)
            return false;
        std::uint8_t* status = StatusOf(soldiers, index);
        return !(At<std::uint32_t>(status, kStatusSyncFlags) & kSyncHeldUp) && !(At<std::uint64_t>(status, 0) & kHeld)
            && !FakeSurrender::IsFaking(soldiers, index);
    }

    int NoticeTypeOf(std::uint8_t* soldiers, std::uint32_t index)
    {
        void* noticeController = At<void*>(soldiers, kSoldierNoticeController);
        return (*reinterpret_cast<GetNoticeType_t**>(noticeController))[kGetNoticeTypeVtblIndex](noticeController, index);
    }

    bool IsIdle(std::uint8_t* soldiers, std::uint32_t index)   // vanilla's send-someone conditions, all but the tower one
    {
        const std::uint8_t* active = At<std::uint8_t*>(At<std::uint8_t*>(soldiers, kSoldierTransforms), kTransformActiveBits);
        if (!(active[index >> 3] & (1u << (index & 7))) || !IsUp(soldiers, index) || NoticeTypeOf(soldiers, index) != kNoNotice)
            return false;
        return !(At<std::uint32_t>(StatusOf(soldiers, index), 0) & kSkippedByComradeSearch);
    }

    bool IsInTower(std::uint8_t* soldiers, std::uint32_t index)   // guards up a tower stay at their post; a trap query, so kept out of the SEH guards
    {
        alignas(16) float position[4] = {};
        const float* at = PositionOf(soldiers, index);
        position[0] = at[0];
        position[1] = at[1];
        position[2] = at[2];
        void* utility = At<void*>(soldiers, kSoldierCommonUtility);
        return (*reinterpret_cast<IsInTower_t**>(utility))[kIsInTowerVtblIndex](utility, position);
    }

    bool IsIdle_SEH(std::uint8_t* soldiers, std::uint32_t index)
    {
        __try
        {
            return index < At<std::uint32_t>(soldiers, kSoldierCount) && IsIdle(soldiers, index);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool IsFree(std::uint8_t* soldiers, std::uint32_t index)
    {
        return IsIdle_SEH(soldiers, index) && !IsInTower(soldiers, index);
    }

    struct Candidates
    {
        std::uint32_t index[64] = {};
        float         distance[64] = {};
        std::uint32_t count = 0;
    };

    void CollectIdle_SEH(std::uint8_t* soldiers, std::uint32_t caller, const Taken& taken, Candidates& out)   // his CP's idle soldiers within vanilla's send reach
    {
        __try
        {
            const std::uint8_t cp = SoldierCommandPost::IndexOf(soldiers, caller);
            if (cp == SoldierCommandPost::kNone)
                return;
            const float reach = Hearing(soldiers, kHearingHll) + kSendSearchPadding;
            const float* from = PositionOf(soldiers, caller);
            const std::uint32_t count = At<std::uint32_t>(soldiers, kSoldierCount);
            for (std::uint32_t index = 0; index < count && out.count < 64; ++index)
            {
                if (index == caller || taken.Has(index))
                    continue;
                const float distance = SquaredDistance(from, PositionOf(soldiers, index));
                if (distance > reach * reach || SoldierCommandPost::IndexOf(soldiers, index) != cp || !IsIdle(soldiers, index))
                    continue;
                out.index[out.count]    = index;
                out.distance[out.count] = distance;
                ++out.count;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            out.count = 0;
        }
    }

    std::uint32_t FindBackup(std::uint8_t* soldiers, std::uint32_t caller, const Taken& taken)   // the nearest of them not up a tower
    {
        Candidates candidates;
        CollectIdle_SEH(soldiers, caller, taken, candidates);
        while (candidates.count > 0)
        {
            std::uint32_t nearest = 0;
            for (std::uint32_t i = 1; i < candidates.count; ++i)
            {
                if (candidates.distance[i] < candidates.distance[nearest])
                    nearest = i;
            }
            const std::uint32_t index = candidates.index[nearest];
            if (!IsInTower(soldiers, index))
                return index;
            --candidates.count;
            candidates.index[nearest]    = candidates.index[candidates.count];
            candidates.distance[nearest] = candidates.distance[candidates.count];
        }
        return kNobody;
    }

    std::uint16_t MemberOf_SEH(std::uint8_t* soldiers, std::uint32_t soldierIndex)   // as the CP numbers his calls
    {
        __try
        {
            std::uint8_t* commandPost = At<std::uint8_t*>(soldiers, kSoldierCommandPost);
            std::uint8_t* controller  = At<std::uint8_t*>(commandPost, kCommandPostController);
            const std::uint32_t client = At<std::uint32_t>(commandPost, kCommandPostClient);
            std::uint8_t* clients = At<std::uint8_t*>(controller, kControllerClients);
            return static_cast<std::uint16_t>(At<std::uint16_t>(clients, client * kClientSize + kClientFirstMember) + soldierIndex);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return kNoMember;
        }
    }

    bool ReadBackup_SEH(std::uint8_t* soldiers, std::uint32_t caller, std::uint32_t backup, BackupState& out)
    {
        __try
        {
            const float* at = PositionOf(soldiers, backup);
            out.up         = IsUp(soldiers, backup);
            out.noticeType = NoticeTypeOf(soldiers, backup);
            out.distance   = SquaredDistance(PositionOf(soldiers, caller), at);
            out.at[0]      = at[0];
            out.at[1]      = at[1];
            out.at[2]      = at[2];
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void AskForBackup(const NoticeNode& node, std::uint8_t* noticeAi, std::uint32_t caller, bool noise)   // before his call goes to the CP: its answer depends on who is free
    {
        Taken taken;
        {
            std::lock_guard<std::mutex> lock(g_Mutex);
            DropStale(GameClock::NowMs());
            if (Request* mine = RequestOf(caller))
            {
                if (mine->stage == Stage::Asking)
                    return;   // the same call, his step resumed
                *mine = Request{};
            }
            taken = TakenBesides(caller);
        }
        if (taken.Has(caller))
            return;   // he is on his way as someone's backup

        std::uint8_t* soldiers = At<std::uint8_t*>(noticeAi, kNodeSoldiers);
        const std::uint16_t member = MemberOf_SEH(soldiers, caller);
        if (member == kNoMember)
            return;
        const std::uint32_t backup = FindBackup(soldiers, caller, taken);

        Request asking;
        asking.stage   = Stage::Asking;
        asking.noise   = noise;
        asking.caller  = caller;
        asking.backup  = backup;
        asking.member  = member;
        asking.askedMs = GameClock::NowMs();
        if (backup != kNobody)
            asking.label = noise ? g_NoiseBackupLabel : g_PersonBackupLabel;
        else
            asking.label = noise ? g_NoiseAloneLabel : g_PersonAloneLabel;

        {
            std::lock_guard<std::mutex> lock(g_Mutex);
            Request* slot = FreeRequest();
            if (!slot)
                return;
            *slot = asking;
        }
    }

    void SendBackup(const NoticeNode& node, std::uint8_t* noticeAi, std::uint32_t caller, std::uint8_t* knowledge)   // his call is over
    {
        Request request;
        Taken taken;
        {
            std::lock_guard<std::mutex> lock(g_Mutex);
            Request* asking = RequestOf(caller);
            if (!asking || asking->stage != Stage::Asking)
                return;
            request = *asking;
            if (request.backup == kNobody)
                *asking = Request{};
            else
                taken = TakenBesides(caller);
        }
        if (request.backup == kNobody)
        {
            return;
        }

        std::uint8_t* soldiers = At<std::uint8_t*>(noticeAi, kNodeSoldiers);
        std::uint32_t backup = request.backup;
        if (!IsFree(soldiers, backup))
            backup = FindBackup(soldiers, caller, taken);   // the one the CP meant got busy during the call
        const bool sent = backup != kNobody
            && SoldierNotice::Add(backup, SoldierNotice::kCalledForHelp, static_cast<std::uint16_t>(kSoldierIdBase | caller), nullptr);

        {
            std::lock_guard<std::mutex> lock(g_Mutex);
            Request* asking = RequestOf(caller);
            if (!asking)
                return;
            if (!sent)
            {
                *asking = Request{};
            }
            else
            {
                asking->backup = backup;
                asking->stage  = Stage::Coming;
                asking->seenMs = GameClock::NowMs();
            }
            RecountWalking();
        }
        if (!sent)
        {
            return;
        }
        knowledge[node.stepAt] = node.waitHelp;
    }

    void WaitForBackup(const NoticeNode& node, std::uint8_t* noticeAi, std::uint32_t caller, std::uint8_t* knowledge)
    {
        const float waited = At<float>(knowledge, kStepTimer);
        Request request;
        {
            std::lock_guard<std::mutex> lock(g_Mutex);
            Request* waiting = RequestOf(caller);
            if (waiting && (waiting->stage == Stage::Coming || waiting->stage == Stage::Arrived))
            {
                waiting->seenMs = GameClock::NowMs();
                if (waiting->stage == Stage::Arrived && waiting->arrivedAt < 0.0f)
                    waiting->arrivedAt = waited;
                request = *waiting;
            }
        }
        if (request.stage == Stage::Free)
        {
            if (&node == &kSightNode && knowledge[node.levelAt] == kSearcherLevel)
            {
                knowledge[node.stepAt] = node.goAlone;   // only a call for backup puts a guard who can leave his post here
            }
            return;
        }

        BackupState backup;
        const bool readable = ReadBackup_SEH(At<std::uint8_t*>(noticeAi, kNodeSoldiers), caller, request.backup, backup);
        const bool walking  = readable && backup.noticeType == SoldierNotice::kCalledForHelp;
        bool here = false;
        const char* gaveUp = nullptr;
        if (!readable || !backup.up)
            gaveUp = "his backup is down";
        else if (request.stage == Stage::Arrived)
            here = backup.noticeType == kNoNotice || waited - request.arrivedAt >= kNoticeClearS;
        else if (backup.noticeType == kNoNotice && waited >= kNoticeLandsS)
        {
            if (backup.distance <= kHeldShortM * kHeldShortM)
                here = true;   // his walk-over ended on its own close by
            else
                gaveUp = "his backup stopped coming";
        }
        else if (!walking && backup.noticeType != kNoNotice)
            gaveUp = "his backup got busy";
        else if (waited >= kLongestWaitS)
            gaveUp = "he waited too long";
        if (!here && !gaveUp)
        {
            knowledge[node.stepAt] = node.waitHelp;   // vanilla would move a guard who can leave his post on to watching the spot
            return;
        }

        {
            std::lock_guard<std::mutex> lock(g_Mutex);
            if (Request* waiting = RequestOf(caller))
            {
                if (gaveUp && walking)
                {
                    waiting->stage  = Stage::Cancelled;   // still on his way
                    waiting->seenMs = GameClock::NowMs();
                }
                else
                {
                    *waiting = Request{};
                }
                RecountWalking();
            }
        }
        if (readable && !backup.up && request.stage == Stage::Coming)
        {
            const std::uint16_t lost = static_cast<std::uint16_t>(request.backup);
            CpRadioResponse::ReportMissing(&lost, 1);   // user: taken out on his way, his CP asks him for a SITREP
            LogDebug("BackupProbe: soldier %u, on his way as soldier %u's backup, was taken out: the CP will ask for a SITREP",
                     request.backup, caller);
        }
        if (gaveUp)
        {
            knowledge[node.stepAt] = node.goAlone;
            return;
        }
        knowledge[node.stepAt] = node.callHelp;
    }

    void CallerLeftWait(std::uint32_t caller)   // his wait step ended under us: a new notice, or another node took over
    {
        std::uint32_t backup = kNobody;
        {
            std::lock_guard<std::mutex> lock(g_Mutex);
            Request* waiting = RequestOf(caller);
            if (!waiting || (waiting->stage != Stage::Coming && waiting->stage != Stage::Arrived))
                return;
            backup = waiting->backup;
            if (waiting->stage == Stage::Coming)
            {
                waiting->stage  = Stage::Cancelled;
                waiting->seenMs = GameClock::NowMs();
            }
            else
            {
                *waiting = Request{};
            }
            RecountWalking();
        }
    }

    void CallNoiseStep(std::uint8_t* node, std::uint8_t step, std::uint32_t soldierIndex, int proc, std::uint8_t* knowledge)
    {
        std::uint8_t* entry = g_NoiseStepTable + step * kStepEntrySize;
        At<StepProc_t>(entry, 0)(node - kNodeToSteps + At<std::int32_t>(entry, 8), soldierIndex, proc, knowledge);
    }

    void EndWalkOver(std::uint8_t* node, std::uint32_t soldierIndex, std::uint8_t* knowledge)   // the step change PreUpdate itself makes, to End
    {
        const std::uint8_t step = knowledge[kNoiseNode.stepAt];
        if (step == kNoiseStepEnd)
            return;
        CallNoiseStep(node, step, soldierIndex, kProcExit, knowledge);
        At<float>(knowledge, kStepTimer) = 0.0f;
        knowledge[kNoiseStepFlags] = 0;
        knowledge[kNoiseNode.stepAt] = kNoiseStepEnd;
        CallNoiseStep(node, kNoiseStepEnd, soldierIndex, kProcEnter, knowledge);
    }

    void BackupTick(std::uint8_t* node, std::uint32_t backup, std::uint8_t* knowledge)   // the backup's own noise node, every tick
    {
        std::uint32_t caller = kNobody;
        Stage stage = Stage::Free;
        {
            std::lock_guard<std::mutex> lock(g_Mutex);
            for (const Request& request : g_Requests)
            {
                if (request.backup == backup && (request.stage == Stage::Coming || request.stage == Stage::Cancelled))
                {
                    caller = request.caller;
                    stage  = request.stage;
                    break;
                }
            }
        }
        if (caller == kNobody)
            return;
        const bool walkOver = At<std::uint8_t>(knowledge, kNoiseWakeKind) == SoldierNotice::kCalledForHelp
            && At<std::uint16_t>(knowledge, kNoiseTargetId) == (kSoldierIdBase | caller);

        if (stage == Stage::Cancelled)
        {
            if (walkOver)
                EndWalkOver(node, backup, knowledge);
            {
                std::lock_guard<std::mutex> lock(g_Mutex);
                Request* cancelled = RequestOf(caller);
                if (cancelled && cancelled->stage == Stage::Cancelled && cancelled->backup == backup)
                    *cancelled = Request{};
                RecountWalking();
            }
            return;
        }
        if (!walkOver)
            return;   // his node woke for something else; his caller sees that and stops waiting

        BackupState state;
        if (!ReadBackup_SEH(At<std::uint8_t*>(node - kNodeToSteps, kNodeSoldiers), caller, backup, state))
            return;
        const bool beside = state.distance <= kBesideM * kBesideM;
        bool heldShort = false;
        {
            std::lock_guard<std::mutex> lock(g_Mutex);
            Request* coming = RequestOf(caller);
            if (!coming || coming->stage != Stage::Coming || coming->backup != backup)
                return;
            const std::uint64_t now = GameClock::NowMs();
            if (!coming->anchorMs || SquaredDistance(coming->anchor, state.at) > kMovedM * kMovedM)
            {
                coming->anchor[0] = state.at[0];
                coming->anchor[1] = state.at[1];
                coming->anchor[2] = state.at[2];
                coming->anchorMs  = now;
            }
            heldShort = state.distance <= kHeldShortM * kHeldShortM && now - coming->anchorMs >= kHeldShortMs;
            if (!beside && !heldShort)
                return;
            coming->stage = Stage::Arrived;
            RecountWalking();
        }
        EndWalkOver(node, backup, knowledge);
    }

    void ForgetCall(std::uint32_t caller)   // his call step ended before the call did
    {
        bool dropped = false;
        {
            std::lock_guard<std::mutex> lock(g_Mutex);
            Request* asking = RequestOf(caller);
            if (asking && asking->stage == Stage::Asking)
            {
                *asking = Request{};
                dropped = true;
            }
        }
    }

    void RadioStart(const NoticeNode& node, StepProc_t original, std::uint8_t* noticeAi, std::uint32_t soldierIndex, int stepProc,
                    std::uint8_t* knowledge, bool noise)
    {
        if (MissionCodeGuard::ShouldBypassHooks() || !noticeAi || !knowledge)
        {
            original(noticeAi, soldierIndex, stepProc, knowledge);
            return;
        }
        if ((stepProc == kProcEnter || stepProc == kProcResume) && knowledge[node.levelAt] == kSearcherLevel
            && FeatureSwitch::IsOn(FeatureSwitch::Feature::BackupRequests))
            AskForBackup(node, noticeAi, soldierIndex, noise);
        original(noticeAi, soldierIndex, stepProc, knowledge);
        if (stepProc == kProcUpdate && knowledge[node.stepAt] == node.goAlone)
            SendBackup(node, noticeAi, soldierIndex, knowledge);
        else if (stepProc == kProcExit)
            ForgetCall(soldierIndex);
    }

    void WaitHelp(const NoticeNode& node, StepProc_t original, std::uint8_t* noticeAi, std::uint32_t soldierIndex, int stepProc,
                  std::uint8_t* knowledge)
    {
        original(noticeAi, soldierIndex, stepProc, knowledge);
        if (!noticeAi || !knowledge || MissionCodeGuard::ShouldBypassHooks())
            return;
        if (stepProc == kProcUpdate)
            WaitForBackup(node, noticeAi, soldierIndex, knowledge);
        else if (stepProc == kProcExit)
            CallerLeftWait(soldierIndex);
    }

    void __fastcall hkSightRadioStart(std::uint8_t* noticeAi, std::uint32_t soldierIndex, int stepProc, std::uint8_t* knowledge)
    {
        const bool noise = knowledge && (knowledge[kSightCallFlags] & kSightCallIsNoise) != 0;
        RadioStart(kSightNode, g_OrigSightRadioStart, noticeAi, soldierIndex, stepProc, knowledge, noise);
    }

    void __fastcall hkNoiseRadioStart(std::uint8_t* noticeAi, std::uint32_t soldierIndex, int stepProc, std::uint8_t* knowledge)
    {
        RadioStart(kNoiseNode, g_OrigNoiseRadioStart, noticeAi, soldierIndex, stepProc, knowledge, true);
    }

    void __fastcall hkSightWaitHelp(std::uint8_t* noticeAi, std::uint32_t soldierIndex, int stepProc, std::uint8_t* knowledge)
    {
        WaitHelp(kSightNode, g_OrigSightWaitHelp, noticeAi, soldierIndex, stepProc, knowledge);
    }

    void __fastcall hkNoiseWaitHelp(std::uint8_t* noticeAi, std::uint32_t soldierIndex, int stepProc, std::uint8_t* knowledge)
    {
        WaitHelp(kNoiseNode, g_OrigNoiseWaitHelp, noticeAi, soldierIndex, stepProc, knowledge);
    }

    void __fastcall hkNoisePreUpdate(std::uint8_t* node, std::uint64_t soldierIndex, std::uint8_t** knowledge)
    {
        g_OrigNoisePreUpdate(node, soldierIndex, knowledge);
        if (g_Walking.load(std::memory_order_acquire) == 0 || !node || !knowledge || !*knowledge || MissionCodeGuard::ShouldBypassHooks())
            return;
        BackupTick(node, static_cast<std::uint32_t>(soldierIndex), *knowledge);
    }

    void* InstallOne(const char* what, std::uintptr_t address, const std::uint8_t* entry, std::size_t entrySize, void* detour,
                     void** original)
    {
        void* target = ResolveGameAddress(address);
        const HookEntry state = CheckHookEntry(target, entry, entrySize);
        if (state == HookEntry::Unknown)
        {
            LogError("BackupCall: %s entry mismatch on %s, guards there never ask the CP for backup", what, GetGameBuildName(gGameBuild));
            return nullptr;
        }
        if (state == HookEntry::ForeignHook)
            LogHookedOnTop(what, target);
        if (!CreateAndEnableHook(target, detour, original))
        {
            LogError("BackupCall: %s hook failed at %p, guards there never ask the CP for backup", what, target);
            return nullptr;
        }
        return target;
    }

    void RemoveOne(void*& target)
    {
        if (target)
            DisableAndRemoveHook(target);
        target = nullptr;
    }
}

std::uint32_t SoldierBackupCall::LabelForCall(std::uint8_t radioType, std::uint16_t member)
{
    if ((radioType != kPersonReport && radioType != kNoiseReport) || member == kNoMember || MissionCodeGuard::ShouldBypassHooks())
        return 0;

    const bool noise = radioType == kNoiseReport;
    std::uint32_t label  = 0;
    std::uint32_t caller = kNobody;
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        for (const Request& request : g_Requests)
        {
            if (request.stage == Stage::Asking && request.noise == noise
                && (request.member == member || (kSoldierIdBase | request.caller) == member))
            {
                label  = request.label;
                caller = request.caller;
                break;
            }
        }
    }
    return label;
}

void SoldierBackupCall::ClearForNewMission()
{
    std::lock_guard<std::mutex> lock(g_Mutex);
    for (Request& request : g_Requests)
        request = Request{};
    RecountWalking();
}

bool Install_SoldierBackupCall_Hooks()
{
    const BuildSites* sites = SitesFor();
    if (!sites)
    {
        LogWarn("BackupCall: addresses unknown for %s, guards never ask the CP for backup", GetGameBuildName(gGameBuild));
        return true;
    }

    g_PersonBackupLabel = FoxHashes::StrCode32("CPR0293");
    g_PersonAloneLabel  = FoxHashes::StrCode32("CPR0294");
    g_NoiseBackupLabel  = FoxHashes::StrCode32("CPR0297");
    g_NoiseAloneLabel   = FoxHashes::StrCode32("CPR0299");

    std::uint8_t* table = static_cast<std::uint8_t*>(ResolveGameAddress(sites->noiseStepTable));
    if (!table || !IsNoiseStepTable(table, ResolveGameAddress(sites->noiseWaitHelp), ResolveGameAddress(sites->noiseRadioStart)))
    {
        LogError("BackupCall: NoticeNoiseAiImpl step table not found at %p on %s, guards never ask the CP for backup", table,
                 GetGameBuildName(gGameBuild));
        return true;
    }
    g_NoiseStepTable = table;
    if (!g_NoisePreUpdateTarget)
        g_NoisePreUpdateTarget = InstallOne("NoticeNoiseAiImpl::PreUpdate", sites->noisePreUpdate, kNoisePreUpdateEntry, sizeof(kNoisePreUpdateEntry),
                                            reinterpret_cast<void*>(&hkNoisePreUpdate), reinterpret_cast<void**>(&g_OrigNoisePreUpdate));
    if (!g_NoisePreUpdateTarget)
        return true;   // a backup's arrival can't be seen: nobody asks for one

    if (!g_SightRadioStartTarget)
        g_SightRadioStartTarget = InstallOne("NoticeIndisAiImpl::StepRadioStart", sites->sightRadioStart, kSightRadioStartEntry,
                                             sizeof(kSightRadioStartEntry), reinterpret_cast<void*>(&hkSightRadioStart),
                                             reinterpret_cast<void**>(&g_OrigSightRadioStart));
    if (!g_SightWaitHelpTarget)
        g_SightWaitHelpTarget = InstallOne("NoticeIndisAiImpl::StepWaitHelp", sites->sightWaitHelp, kSightWaitHelpEntry, sizeof(kSightWaitHelpEntry),
                                           reinterpret_cast<void*>(&hkSightWaitHelp), reinterpret_cast<void**>(&g_OrigSightWaitHelp));
    if (!g_NoiseRadioStartTarget)
        g_NoiseRadioStartTarget = InstallOne("NoticeNoiseAiImpl::StepRadioStart", sites->noiseRadioStart, kNoiseRadioStartEntry,
                                             sizeof(kNoiseRadioStartEntry), reinterpret_cast<void*>(&hkNoiseRadioStart),
                                             reinterpret_cast<void**>(&g_OrigNoiseRadioStart));
    if (!g_NoiseWaitHelpTarget)
        g_NoiseWaitHelpTarget = InstallOne("NoticeNoiseAiImpl::StepWaitHelp", sites->noiseWaitHelp, kNoiseWaitHelpEntry, sizeof(kNoiseWaitHelpEntry),
                                           reinterpret_cast<void*>(&hkNoiseWaitHelp), reinterpret_cast<void**>(&g_OrigNoiseWaitHelp));
    return true;
}

void Uninstall_SoldierBackupCall_Hooks()
{
    RemoveOne(g_NoiseWaitHelpTarget);
    RemoveOne(g_NoiseRadioStartTarget);
    RemoveOne(g_SightWaitHelpTarget);
    RemoveOne(g_SightRadioStartTarget);
    RemoveOne(g_NoisePreUpdateTarget);
    SoldierBackupCall::ClearForNewMission();
}
