#include <pch.h>
#include "NoticeControllerImpl_CheckForceNotice.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"
#include "MissionCodeGuard.h"
#include "PlayerAlertPhase.h"
#include "HoldupAiImpl_StepHoldupStand.h"

namespace
{
    using CheckForceNotice_t    = void(__fastcall*)(std::uint8_t* noticeController, std::uint32_t soldierIndex);
    using AddNoticeInfo_t       = bool(__fastcall*)(void* noticeInterface, std::uint32_t soldierIndex, const void* noticeInfo);
    using GetQuarkSystemTable_t = std::uint8_t*(__fastcall*)();
    using SendSignal_t          = void(__fastcall*)(void* signalSystem, void* error, std::uint16_t targetId, void* signal);
    using IsNight_t             = bool(__fastcall*)(void* utility);

    constexpr std::size_t   kNoticeInterface        = 0x20;      // the interface soldiers hold sits 0x20 into the controller
    constexpr std::size_t   kAddNoticeInfoVtblIndex = 0;
    constexpr std::size_t   kNoticeSlots            = 0x60;      // 0x80 per soldier; the notice he is reacting to comes first
    constexpr std::size_t   kNoticeSlotSize         = 0x80;
    constexpr std::size_t   kSlotNoticePointer      = 0x08;
    constexpr std::size_t   kNoticeSoldiers         = 0x58;      // the controller's Soldier2Impl

    constexpr std::size_t   kSoldierCount           = 0x08;
    constexpr std::size_t   kSoldierTransforms      = 0x18;
    constexpr std::size_t   kTransformActiveBits    = 0x08;
    constexpr std::size_t   kTransformPositions     = 0x20;      // one Vector4 per soldier
    constexpr std::size_t   kSoldierParams          = 0x128;     // Soldier2ParameterTablesImpl
    constexpr std::size_t   kParamsSight            = 0x08;      // sightFormParameter and the sight sets
    constexpr std::size_t   kParamsHearing          = 0x18;      // hearingRangeParameter: normal, sandstorm, rain; ten classes each
    constexpr std::size_t   kSoldierCommonUtility   = 0x138;
    constexpr std::size_t   kIsNightVtblIndex       = 0x20 / 8;
    constexpr std::size_t   kSoldierFlags           = 0x2A4;
    constexpr std::uint32_t kFlagsRain              = 1u << 0xD;
    constexpr std::uint32_t kFlagsSandstorm         = 1u << 0xE;

    constexpr std::size_t   kHearingSandstorm       = 10;
    constexpr std::size_t   kHearingRain            = 20;
    constexpr std::size_t   kHearingLoud            = 5;         // "l", 30 m in vanilla
    constexpr std::size_t   kSightDimDay            = 0x54;      // baseSight.dim.distance
    constexpr std::size_t   kSightDimNight          = 0x90;      // nightSight.dim.distance
    constexpr std::size_t   kSightDimCombat         = 0xCC;      // combatSight.dim.distance
    constexpr std::size_t   kSightSandstormRate     = 0x2D0;     // sandstormSight.distanceRate

    constexpr std::size_t   kQuarkSignalSystem      = 0x60;
    constexpr std::size_t   kSendSignalVtblIndex    = 0x38 / 8;
    constexpr std::uint64_t kGetPositionSignal      = 0xD0A5D0C7E373ull;   // what the notice AIs send to learn where an object is

    constexpr std::uint8_t  kSourceIsGameObject     = 1;
    constexpr std::uint8_t  kSourceIsPosition       = 2;
    constexpr std::uint32_t kMaxSoldiers            = 1024;
    constexpr std::uint32_t kMaxBroadcasts          = 16;
    constexpr int           kLastKind               = 0x3C;      // AddNoticeInfo's priority table stops here; 0x3E is the game's "no notice"

    constexpr std::uint16_t kTypePlayer             = 0x00;      // game object id >> 9
    constexpr std::uint16_t kTypeSoldier            = 0x02;
    constexpr std::uint16_t kTypeCardboardBox       = 0x42;
    constexpr std::uint16_t kTypeDecoy              = 0x44;
    constexpr std::uint16_t kSoldierIdBase          = 0x400;
    constexpr std::uint16_t kIndexMask              = 0x1FF;

    struct alignas(16) NoticeInfo
    {
        std::uint8_t   type;
        std::uint8_t   sourceKind;
        std::uint16_t  sourceId;
        std::uint32_t  unused;
        const void*    noise;      // noise notices point at the noise (position first); some readers follow it whatever the source
    };
    static_assert(sizeof(NoticeInfo) == 16, "the game copies notices as 16 bytes");

    struct alignas(16) FakeNoise   // what a noise notice points at: the noise system's own record
    {
        float         position[4];
        std::uint8_t  kind;
        std::uint8_t  flags;
        std::uint16_t source;      // who made it; 0 would be the player
        std::uint8_t  rest[0x2C];
    };
    static_assert(offsetof(FakeNoise, source) == 0x12, "NoticeNoiseAiImpl::Wake reads the source at +0x12");

    struct alignas(16) PositionSignal
    {
        std::uint64_t id;
        std::uint64_t unused;
        float         offset[4];
        float         position[4];
        std::uint8_t  answered;
        std::uint8_t  rest[15];
    };
    static_assert(offsetof(PositionSignal, position) == 0x20 && offsetof(PositionSignal, answered) == 0x30,
                  "laid out as NoticeDiscoveryAiImpl::Wake builds it");

    struct PendingNotice
    {
        bool          waiting     = false;
        std::uint8_t  kind        = 0;
        std::uint16_t targetId    = SoldierNotice::kNoTarget;
        bool          hasPosition = false;
        float         position[3] = {};
    };

    struct Broadcast
    {
        PendingNotice notice;
        float         range = 0.0f;   // 0: the soldiers' own hearing or sight
    };

    CheckForceNotice_t    g_Orig                = nullptr;
    void*                 g_HookTarget          = nullptr;
    GetQuarkSystemTable_t g_GetQuarkSystemTable = nullptr;
    std::mutex            g_PendingLock;
    PendingNotice         g_Pending[kMaxSoldiers];
    FakeNoise             g_Noises[kMaxSoldiers][2];   // two each, so a new notice never moves the spot of the one he is still reacting to
    std::atomic<int>      g_PendingCount{ 0 };
    Broadcast             g_Broadcasts[kMaxBroadcasts];
    std::atomic<int>      g_BroadcastCount{ 0 };

    template <typename T>
    T& At(std::uint8_t* base, std::size_t offset)
    {
        return *reinterpret_cast<T*>(base + offset);
    }

    template <typename Fn>
    Fn VirtualAt(void* object, std::size_t index)
    {
        return (*reinterpret_cast<Fn**>(object))[index];
    }

    bool IsNoiseKind(int kind)
    {
        return kind == 0x32 || kind == 0x33 || kind == 0x34;
    }

    void QueueLocked(std::uint32_t soldierIndex, const PendingNotice& notice)
    {
        PendingNotice& pending = g_Pending[soldierIndex];
        if (!pending.waiting)
            g_PendingCount.fetch_add(1, std::memory_order_acq_rel);
        pending = notice;
        pending.waiting = true;
    }

    FakeNoise& FreeNoise(std::uint8_t* noticeController, std::uint32_t soldierIndex)
    {
        std::uint8_t* slot = At<std::uint8_t*>(noticeController, kNoticeSlots) + soldierIndex * kNoticeSlotSize;
        const void* inUse = At<const void*>(slot, kSlotNoticePointer);
        FakeNoise* pair = g_Noises[soldierIndex];
        return inUse == &pair[0] ? pair[1] : pair[0];
    }

    void PostNotice(std::uint8_t* noticeController, std::uint32_t soldierIndex, const PendingNotice& pending)
    {
        FakeNoise& noise = FreeNoise(noticeController, soldierIndex);
        noise = FakeNoise{};
        noise.source = SoldierNotice::kNoTarget;
        if (pending.hasPosition)
        {
            noise.position[0] = pending.position[0];
            noise.position[1] = pending.position[1];
            noise.position[2] = pending.position[2];
        }

        NoticeInfo notice{};
        notice.type       = pending.kind;
        notice.sourceKind = pending.targetId != SoldierNotice::kNoTarget ? kSourceIsGameObject : kSourceIsPosition;
        notice.sourceId   = pending.targetId;
        notice.noise      = &noise;

        void* noticeInterface = noticeController + kNoticeInterface;
        VirtualAt<AddNoticeInfo_t>(noticeInterface, kAddNoticeInfoVtblIndex)(noticeInterface, soldierIndex, &notice);
    }

    bool TakePending(std::uint32_t soldierIndex, PendingNotice& out)
    {
        std::lock_guard<std::mutex> lock(g_PendingLock);
        PendingNotice& pending = g_Pending[soldierIndex];
        if (!pending.waiting)
            return false;
        out = pending;
        pending.waiting = false;
        g_PendingCount.fetch_sub(1, std::memory_order_acq_rel);
        return true;
    }

    bool PositionOf(std::uint8_t* soldiers, std::uint16_t targetId, float* out)   // where a game object is, as the notice AIs ask it
    {
        if ((targetId >> 9) == kTypeSoldier)
        {
            const std::uint32_t index = targetId & kIndexMask;
            if (index >= At<std::uint32_t>(soldiers, kSoldierCount))
                return false;
            const float* at = At<float*>(At<std::uint8_t*>(soldiers, kSoldierTransforms), kTransformPositions) + index * 4;
            out[0] = at[0];
            out[1] = at[1];
            out[2] = at[2];
            return true;
        }
        if (!g_GetQuarkSystemTable)
            return false;

        PositionSignal signal{};
        signal.id        = kGetPositionSignal;
        signal.offset[3] = 1.0f;
        void* signalSystem = At<void*>(g_GetQuarkSystemTable(), kQuarkSignalSystem);
        std::uint64_t error = 0;
        VirtualAt<SendSignal_t>(signalSystem, kSendSignalVtblIndex)(signalSystem, &error, targetId, &signal);
        if (!signal.answered)
            return false;
        out[0] = signal.position[0];
        out[1] = signal.position[1];
        out[2] = signal.position[2];
        return true;
    }

    float DefaultReach(std::uint8_t* soldiers, int kind)   // vanilla's own parameters: a loud noise for the noise kinds, the "dim" sight band for the rest
    {
        std::uint8_t* params = At<std::uint8_t*>(soldiers, kSoldierParams);
        const std::uint32_t weather = At<std::uint32_t>(soldiers, kSoldierFlags);
        if (IsNoiseKind(kind))
        {
            const float* hearing = At<float*>(params, kParamsHearing);
            std::size_t table = 0;
            if (weather & kFlagsRain)
                table = kHearingRain;
            else if (weather & kFlagsSandstorm)
                table = kHearingSandstorm;
            return hearing[table + kHearingLoud];
        }

        std::uint8_t* sight = At<std::uint8_t*>(params, kParamsSight);
        void* utility = At<void*>(soldiers, kSoldierCommonUtility);
        std::size_t band = kSightDimDay;
        if (PlayerAlertPhase::Read() == PlayerAlertPhase::kAlert)
            band = kSightDimCombat;
        else if (VirtualAt<IsNight_t>(utility, kIsNightVtblIndex)(utility))
            band = kSightDimNight;
        float reach = At<float>(sight, band);
        if (weather & kFlagsSandstorm)
            reach *= At<float>(sight, kSightSandstormRate);
        return reach;
    }

    void SpreadBroadcast(std::uint8_t* soldiers, const Broadcast& broadcast)
    {
        const PendingNotice& notice = broadcast.notice;
        float spot[3] = { notice.position[0], notice.position[1], notice.position[2] };
        if (!notice.hasPosition && !PositionOf(soldiers, notice.targetId, spot))
        {
            LogWarn("AddNotice: can't tell where 0x%04X is, give pos instead; ignored", notice.targetId);
            return;
        }

        const float reach = broadcast.range > 0.0f ? broadcast.range : DefaultReach(soldiers, notice.kind);
        const std::uint32_t count = At<std::uint32_t>(soldiers, kSoldierCount);
        std::uint8_t* transforms = At<std::uint8_t*>(soldiers, kSoldierTransforms);
        const std::uint8_t* active = At<std::uint8_t*>(transforms, kTransformActiveBits);
        const float* positions = At<float*>(transforms, kTransformPositions);

        int reached = 0;
        std::lock_guard<std::mutex> lock(g_PendingLock);
        for (std::uint32_t index = 0; index < count && index < kMaxSoldiers; ++index)
        {
            if (!(active[index >> 3] & (1u << (index & 7))))
                continue;
            if (notice.targetId == (kSoldierIdBase | index))
                continue;   // nobody notices himself
            const float dx = positions[index * 4] - spot[0];
            const float dy = positions[index * 4 + 1] - spot[1];
            const float dz = positions[index * 4 + 2] - spot[2];
            if (dx * dx + dy * dy + dz * dz > reach * reach)
                continue;
            QueueLocked(index, notice);
            ++reached;
        }
    }

    void SpreadBroadcasts(std::uint8_t* noticeController)
    {
        Broadcast taken[kMaxBroadcasts];
        int count = 0;
        {
            std::lock_guard<std::mutex> lock(g_PendingLock);
            count = g_BroadcastCount.exchange(0, std::memory_order_acq_rel);
            for (int i = 0; i < count; ++i)
                taken[i] = g_Broadcasts[i];
        }

        std::uint8_t* soldiers = At<std::uint8_t*>(noticeController, kNoticeSoldiers);
        if (!soldiers)
            return;
        for (int i = 0; i < count; ++i)
            SpreadBroadcast(soldiers, taken[i]);
    }

    void __fastcall hk_CheckForceNotice(std::uint8_t* noticeController, std::uint32_t soldierIndex)
    {
        g_Orig(noticeController, soldierIndex);   // the game's own forced notice first; it clears that request itself
        if (!MissionCodeGuard::ShouldBypassHooks())
            HoldupSelfRelease_OnNoticeTick(noticeController, soldierIndex);   // a held-up soldier who freed himself calls it in

        if (MissionCodeGuard::ShouldBypassHooks())
            return;
        if (g_BroadcastCount.load(std::memory_order_acquire) > 0)
            SpreadBroadcasts(noticeController);
        if (g_PendingCount.load(std::memory_order_acquire) <= 0 || soldierIndex >= kMaxSoldiers)
            return;

        PendingNotice pending;
        if (TakePending(soldierIndex, pending))
            PostNotice(noticeController, soldierIndex, pending);
    }

    PendingNotice MakeNotice(int kind, std::uint16_t targetId, const float* position)
    {
        PendingNotice notice;
        notice.kind        = static_cast<std::uint8_t>(kind);
        notice.targetId    = targetId;
        notice.hasPosition = position != nullptr;
        if (position)
        {
            notice.position[0] = position[0];
            notice.position[1] = position[1];
            notice.position[2] = position[2];
        }
        return notice;
    }
}

const char* SoldierNotice::Problem(int kind, std::uint16_t targetId, bool hasPosition)   // what each notice's AI reads, from its Wake
{
    const bool          hasTarget = targetId != kNoTarget;
    const std::uint16_t type      = static_cast<std::uint16_t>(targetId >> 9);
    switch (kind)
    {
    case 0x00:
    case 0x05:
        return hasTarget && type == kTypePlayer ? nullptr : "needs targetId = the player";
    case 0x0A:
        return hasTarget && type != kTypePlayer && type != kTypeSoldier ? nullptr : "needs targetId = an animal or Quiet";
    case 0x19:
        return hasTarget && (type == kTypePlayer || type == kTypeCardboardBox) ? nullptr : "needs targetId = the player or a cardboard box";
    case 0x1B:
        return hasTarget && type == kTypeDecoy ? nullptr : "needs targetId = a decoy";
    case 0x24:
        return nullptr;   // he always goes for the player's supply box
    case 0x26:
        return hasTarget && type == kTypePlayer && (targetId & kIndexMask) < 5 ? nullptr : "needs targetId = the player";
    case 0x32:
    case 0x33:
    case 0x34:
        if (hasTarget)
            return type == kTypeSoldier ? nullptr : "needs pos, or targetId = a soldier";
        return hasPosition ? nullptr : "needs pos, or targetId = a soldier";
    case 0x37:
        return hasTarget ? nullptr : "needs targetId";
    default:
        return "isn't a V_NoticeKind";
    }
}

bool SoldierNotice::Add(std::uint32_t soldierIndex, int kind, std::uint16_t targetId, const float* position)
{
    if (!g_HookTarget || soldierIndex >= kMaxSoldiers || kind < 0 || kind > kLastKind)
        return false;

    std::lock_guard<std::mutex> lock(g_PendingLock);
    QueueLocked(soldierIndex, MakeNotice(kind, targetId, position));
    return true;
}

bool SoldierNotice::AddToAll(int kind, std::uint16_t targetId, const float* position, float range)
{
    if (!g_HookTarget || kind < 0 || kind > kLastKind)
        return false;

    std::lock_guard<std::mutex> lock(g_PendingLock);
    const int count = g_BroadcastCount.load(std::memory_order_acquire);
    if (count >= static_cast<int>(kMaxBroadcasts))
        return false;
    g_Broadcasts[count].notice = MakeNotice(kind, targetId, position);
    g_Broadcasts[count].range  = range;
    g_BroadcastCount.store(count + 1, std::memory_order_release);
    return true;
}

void SoldierNotice::ClearPending()
{
    std::lock_guard<std::mutex> lock(g_PendingLock);
    for (PendingNotice& pending : g_Pending)
        pending.waiting = false;
    g_PendingCount.store(0, std::memory_order_release);
    g_BroadcastCount.store(0, std::memory_order_release);
}

void CallComrade::DeliverComradeCallRelaxed(std::uint32_t comradeIndex, std::uint16_t callerId)
{
    SoldierNotice::Add(comradeIndex, SoldierNotice::kCalledOver, callerId, nullptr);
}

void CallComrade::DeliverComradeCallOnGuard(std::uint32_t comradeIndex, std::uint16_t callerId)
{
    SoldierNotice::Add(comradeIndex, SoldierNotice::kCalledForHelp, callerId, nullptr);
}

bool Install_NoticeControllerImpl_CheckForceNotice_Hook()
{
    if (g_HookTarget)
        return true;

    if (!gAddr.NoticeControllerImpl_CheckForceNotice)
    {
        LogError("CallComradeCommand: CheckForceNotice address missing for %s, called comrades come over on guard and AddNotice does nothing",
                 GetGameBuildName(gGameBuild));
        return true;
    }

    if (gAddr.GetQuarkSystemTable)
        g_GetQuarkSystemTable = reinterpret_cast<GetQuarkSystemTable_t>(ResolveGameAddress(gAddr.GetQuarkSystemTable));
    else
        LogWarn("AddNotice: GetQuarkSystemTable missing for %s, notices sent to every soldier need pos or a soldier targetId",
                GetGameBuildName(gGameBuild));

    void* target = ResolveGameAddress(gAddr.NoticeControllerImpl_CheckForceNotice);
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hk_CheckForceNotice),
                             reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("CallComradeCommand: CheckForceNotice hook failed at %p, called comrades come over on guard and AddNotice does nothing", target);
        return false;
    }

    g_HookTarget = target;
    return true;
}

void Uninstall_NoticeControllerImpl_CheckForceNotice_Hook()
{
    if (g_HookTarget)
    {
        DisableAndRemoveHook(g_HookTarget);
        g_HookTarget = nullptr;
        g_Orig       = nullptr;
    }
    SoldierNotice::ClearPending();
}
