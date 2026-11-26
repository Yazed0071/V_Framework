#include <pch.h>
#include "HoldupAiImpl_StepHoldupStand.h"

#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "log.h"

// Vanilla ends a hold-up only when a comrade frees the soldier or the CP goes to Alert, so a soldier left alone stays
// held up forever. Here his personal ability "holdup" (the one vanilla rolls fake surrender on) gives him a chance to get
// out: low never, mid 10% after 180 s, high 20% after 120 s, sp 50% after 60 s alone. Then he takes the same end step the
// comrade rescue takes, and the end state plays the unused enetasr_s_hdu_ed_b / enetasr_c_hdu_ed_b
// (HoldupActionImpl_State_EndStandHoldup.cpp). Once he is up he
// gets the notice WakeUpAiImpl::AddNoticeInfo gives a soldier who comes round alone, so he reacts and calls it in (CPR0050).
// Like StepWakeUp, never while he still needs his weapon: that node outranks the reaction, the notice is dropped while it
// runs, and the node after it won't wake on a damage notice. So it goes in as the weapon node sleeps.

namespace
{
    using Step_t           = void(__fastcall*)(std::uint8_t* ai, std::uint32_t soldier, std::uint32_t proc, std::uint8_t* knowledge);
    using SoldierClock_t   = const std::uint8_t*(__fastcall*)(void* clocks, std::uint32_t soldier);
    using PlayerPosition_t = void*(__fastcall*)(void* players, float* position, std::uint32_t player);
    using AddNoticeToFire_t = void(__fastcall*)(void* notices, std::uint32_t soldier, const void* notice);
    using CheckNoise_t     = void(__fastcall*)(void* notices, std::uint32_t soldier, void* ai, const std::uint8_t* noise, void* knowledge);

    constexpr std::uint32_t kProcEnter  = 0;
    constexpr std::uint32_t kProcUpdate = 3;
    constexpr std::uint32_t kProcResume = 4;

    constexpr std::uint8_t kStepStand    = 1;
    constexpr std::uint8_t kStepStandEnd = 2;
    constexpr std::uint8_t kStepDown     = 3;
    constexpr std::uint8_t kStepDownEnd  = 4;
    constexpr std::uint8_t kStepDone     = 10;  // re::voidreturn, ShouldSleep sleeps the node on it

    constexpr std::size_t  kKnowledgeStep   = 0x22;

    constexpr std::size_t kHub            = 0x48;
    constexpr std::size_t kHubClocks      = 0x40;
    constexpr std::size_t kHubNotices     = 0x68;
    constexpr std::size_t kHubWeaponFlags = 0xA0;   // +0x48: a byte per soldier, bit 0 = he has to get his weapon first
    constexpr std::size_t kNoticeHub      = 0x58;   // NoticeControllerImpl's Soldier2Impl
    constexpr std::size_t kHubPositions   = 0x18;
    constexpr std::size_t kHubStatus      = 0x110;
    constexpr std::size_t kHubPlayers     = 0x188;
    constexpr std::size_t kHubPlayerIndex = 0x28C;
    constexpr std::size_t kHubAbilities   = 0x198;  // a qword per soldier, a one-hot nibble per ability (SetPersonalAbility)
    constexpr unsigned    kHoldupAbility  = 9 * 4;  // nibble 9 = "holdup"
    constexpr std::size_t kStatusStride   = 0x28;
    constexpr std::size_t kClockDelta     = 0x30;

    constexpr std::uint64_t kHeldUp        = 0x10000;
    constexpr std::uint64_t kHoldupSettled = 0x20; // vanilla ends no hold-up without it; a save restore sets it with kHeldUp

    constexpr unsigned kSkillLow  = 0;
    constexpr unsigned kSkillMid  = 1;   // every soldier's default (GeneratePersonalAbility 0x2222222222)
    constexpr unsigned kSkillHigh = 2;
    constexpr unsigned kSkillSp   = 3;

    constexpr float kMidChance   = 0.10f;
    constexpr float kMidSeconds  = 180.0f;
    constexpr float kHighChance  = 0.20f;
    constexpr float kHighSeconds = 120.0f;
    constexpr float kSpChance    = 0.50f;
    constexpr float kSpSeconds   = 60.0f;

    constexpr float     kCloseMetres  = 10.0f;
    constexpr ULONGLONG kSameHoldupMs = 2000;   // a step entered this soon after the last update is the same hold-up (stand <-> ground)

    constexpr ULONGLONG kReportWaitMs     = 30000;   // to get his weapon back; after that he goes back to his route
    constexpr ULONGLONG kWeaponNodeWaitMs = 5000;    // no weapon node by then: post it on the notice tick
    constexpr ULONGLONG kRadioCallWaitMs  = 60000;   // NoticeDamageAiImpl gets to its radio step well within this

    constexpr std::uint64_t kWeaponNodeName = 0x0ABA9A9FEF70ull;   // GetAiNodeSpec name; ShouldWake 0x1415A2C90 needs the weapon flag
    constexpr std::size_t   kNodeHub        = 0x28;

    constexpr std::uint32_t kMaxSoldiers = 1024;

    constexpr std::size_t   kNoiseSource = 0x12;   // gameObjectId of whoever made it
    constexpr std::uint16_t kPlayerType  = 0;      // CheckNoiseNoticePlayer also takes type 0x12, BuddyQuiet2

    constexpr std::size_t  kAddNoticeToFireSlot = 0x08;
    constexpr std::uint8_t kNoticeDamaged       = 0x02;   // wakes NoticeDamageAiImpl
    constexpr std::uint8_t kSourceAttacker      = 0x03;
    constexpr std::uint8_t kDamageGeneric       = 0x0B;   // its StepRadio calls this one in as radio type 0x0B, CPR0050

    const std::uint8_t kStepPrologue[] = { 0x48, 0x89, 0x5C, 0x24, 0x18, 0x55, 0x56, 0x57, 0x48, 0x81, 0xEC, 0xF0, 0x00, 0x00, 0x00 };
    const std::uint8_t kUnholsterPrologue[] = { 0x40, 0x56, 0x57, 0x48, 0x81, 0xEC, 0xF8, 0x00, 0x00, 0x00 };
    const std::uint8_t kCheckNoisePrologue[] = {
        0x48, 0x89, 0x5C, 0x24, 0x18, 0x55, 0x56, 0x57, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8D, 0x6C, 0x24, 0x90,
        0x48, 0x81, 0xEC, 0x70, 0x01, 0x00, 0x00
    };

    struct Countdown
    {
        float     full;
        float     left;
        bool      frees;        // this hold-up's roll
        ULONGLONG lastSeenMs;
    };

    struct alignas(16) Notice   // as WakeUpAiImpl::AddNoticeInfo fills it for an attacker it can name
    {
        std::uint8_t  type;
        std::uint8_t  source;
        std::uint16_t unused;
        std::uint32_t one;
        std::uint16_t attacker;
        std::uint8_t  damage;
        std::uint8_t  rest[5];
    };
    static_assert(sizeof(Notice) == 16, "the game copies notices as 16 bytes");

    struct Snapshot
    {
        std::uint64_t status;
        void*         clocks;
        void*         players;
        std::uint32_t player;
        float         x;
        float         y;
        float         z;
    };

    struct Addresses
    {
        std::uintptr_t stand;
        std::uintptr_t down;
        std::uintptr_t unholster;
        std::uintptr_t checkNoisePlayer;   // NoticeControllerImpl::CheckNoiseNoticePlayer, every noise he hears that Snake made
    };

    Countdown                  g_Countdown[kMaxSoldiers] = {};
    std::atomic<bool>          g_Freed[kMaxSoldiers]     = {};
    std::atomic<bool>          g_Report[kMaxSoldiers]    = {};
    std::atomic<ULONGLONG>     g_ReportUntilMs[kMaxSoldiers] = {};
    std::atomic<ULONGLONG>     g_RadioUntilMs[kMaxSoldiers]  = {};
    std::atomic<bool>          g_HeardSnake[kMaxSoldiers]    = {};
    std::atomic<bool>          g_Enabled{ true };
    std::atomic<std::uint8_t>  g_SoldierSetting[kMaxSoldiers] = {};   // SetHoldupSelfRelease on him: 0 none, 1 on, 2 off
    std::atomic<std::uint64_t> g_Rng{ 0 };

    Step_t        g_OrigStand   = nullptr;
    Step_t        g_OrigDown    = nullptr;
    Step_t        g_OrigUnholster = nullptr;
    CheckNoise_t  g_OrigCheckNoise = nullptr;
    void*         g_StandTarget = nullptr;
    void*         g_DownTarget  = nullptr;
    void*         g_UnholsterTarget = nullptr;
    void*         g_CheckNoiseTarget = nullptr;

    Addresses AddressesForBuild()
    {
        return { gAddr.HoldupAiImpl_StepHoldupStand, gAddr.HoldupAiImpl_StepHoldupDown, gAddr.HoldupAiImpl_Unholster, gAddr.NoticeControllerImpl_CheckNoiseNoticePlayer };
    }

    std::uint16_t NoiseSourceSEH(const std::uint8_t* noise)
    {
        __try
        {
            return noise ? *reinterpret_cast<const std::uint16_t*>(noise + kNoiseSource) : 0xFFFF;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0xFFFF;
        }
    }

    void* VirtualSEH(void* object, std::size_t offset)
    {
        __try
        {
            return object ? (*reinterpret_cast<void***>(object))[offset / sizeof(void*)] : nullptr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    float ReadFloatSEH(const std::uint8_t* at)
    {
        __try
        {
            return at ? *reinterpret_cast<const float*>(at) : 0.0f;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0.0f;
        }
    }

    bool ReadSnapshotSEH(const std::uint8_t* ai, std::uint32_t soldier, Snapshot& out)
    {
        __try
        {
            const std::uint8_t* hub    = *reinterpret_cast<std::uint8_t* const*>(ai + kHub);
            const std::uint8_t* status = *reinterpret_cast<std::uint8_t* const*>(hub + kHubStatus);
            if (soldier >= *reinterpret_cast<const std::uint32_t*>(status + 0x10))
                return false;
            const std::uint8_t* record = *reinterpret_cast<std::uint8_t* const*>(status + 8) + soldier * kStatusStride;
            const float* position = *reinterpret_cast<float* const*>(*reinterpret_cast<std::uint8_t* const*>(hub + kHubPositions) + 0x20) + soldier * 4;

            out.status        = *reinterpret_cast<const std::uint64_t*>(record);
            out.clocks        = *reinterpret_cast<void* const*>(hub + kHubClocks);
            out.players       = *reinterpret_cast<void* const*>(hub + kHubPlayers);
            out.player        = *reinterpret_cast<const std::uint32_t*>(hub + kHubPlayerIndex);
            out.x             = position[0];
            out.y             = position[1];
            out.z             = position[2];
            return out.clocks && out.players;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool ReadNoticeTargetSEH(const std::uint8_t* hub, void*& notices, std::uint16_t& player)
    {
        __try
        {
            notices = *reinterpret_cast<void* const*>(hub + kHubNotices);
            player  = static_cast<std::uint16_t>(*reinterpret_cast<const std::uint32_t*>(hub + kHubPlayerIndex));
            return notices != nullptr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool NeedsWeaponSEH(const std::uint8_t* hub, std::uint32_t soldier, bool& needs)
    {
        __try
        {
            needs = ((*reinterpret_cast<std::uint8_t* const*>(*reinterpret_cast<std::uint8_t* const*>(hub + kHubWeaponFlags) + 0x48))[soldier] & 1) != 0;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool ReportAttack(const std::uint8_t* hub, std::uint32_t soldier)
    {
        void* notices = nullptr;
        std::uint16_t player = 0;
        auto addToFire = ReadNoticeTargetSEH(hub, notices, player) ? reinterpret_cast<AddNoticeToFire_t>(VirtualSEH(notices, kAddNoticeToFireSlot)) : nullptr;
        if (!addToFire)
        {
            LogWarn("HoldupSelfRelease: soldier %u can't take the notice, he goes back to his route without calling it in", soldier);
            return false;
        }
        Notice notice{};
        notice.type     = kNoticeDamaged;
        notice.source   = kSourceAttacker;
        notice.one      = 1;
        notice.attacker = player;
        notice.damage   = kDamageGeneric;
        addToFire(notices, soldier, &notice);
        g_RadioUntilMs[soldier].store(GetTickCount64() + kRadioCallWaitMs, std::memory_order_release);
        return true;
    }

    float Random01()
    {
        std::uint64_t state = g_Rng.load(std::memory_order_relaxed);
        if (state == 0)
            state = static_cast<std::uint64_t>(GetTickCount64()) ^ 0x9E3779B97F4A7C15ull;
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        g_Rng.store(state, std::memory_order_relaxed);
        return static_cast<float>(state >> 40) / static_cast<float>(1ull << 24);
    }

    unsigned HoldupSkillSEH(const std::uint8_t* ai, std::uint32_t soldier)   // as Soldier2::GetPersonalAbilityLevel reads it
    {
        __try
        {
            const std::uint8_t* hub = *reinterpret_cast<std::uint8_t* const*>(ai + kHub);
            const std::uint64_t abilities = (*reinterpret_cast<std::uint64_t* const*>(hub + kHubAbilities))[soldier] >> kHoldupAbility;
            for (unsigned level = kSkillSp; level > kSkillLow; --level)
            {
                if ((abilities >> level) & 1)
                    return level;
            }
            return kSkillLow;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return kSkillMid;
        }
    }

    bool PlayerIsClose(const Snapshot& snapshot)
    {
        auto position = reinterpret_cast<PlayerPosition_t>(VirtualSEH(snapshot.players, 0x10)); // same call HoldupAiImpl's CheckGunAim makes
        if (!position)
            return false;
        alignas(16) float player[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        position(snapshot.players, player, snapshot.player);
        const float dx = player[0] - snapshot.x;
        const float dy = player[1] - snapshot.y;
        const float dz = player[2] - snapshot.z;
        return dx * dx + dy * dy + dz * dz < kCloseMetres * kCloseMetres;
    }

    float DeltaSeconds(const Snapshot& snapshot, std::uint32_t soldier)
    {
        auto clock = reinterpret_cast<SoldierClock_t>(VirtualSEH(snapshot.clocks, 0x60)); // same call StepHoldupStand makes
        return clock ? ReadFloatSEH(clock(snapshot.clocks, soldier) + kClockDelta) : 0.0f;
    }

    bool SelfReleaseOn(std::uint32_t soldier)
    {
        const std::uint8_t setting = g_SoldierSetting[soldier].load(std::memory_order_relaxed);
        return setting ? setting == 1 : g_Enabled.load(std::memory_order_relaxed);
    }

    void Roll(Countdown& countdown, const std::uint8_t* ai, std::uint32_t soldier)
    {
        if (!SelfReleaseOn(soldier))
        {
            countdown.frees = false;
            countdown.full = countdown.left = 0.0f;
            return;
        }
        const unsigned skill = HoldupSkillSEH(ai, soldier);
        float chance = 0.0f;
        countdown.full = 0.0f;
        if (skill == kSkillMid)
        {
            chance = kMidChance;
            countdown.full = kMidSeconds;
        }
        else if (skill == kSkillHigh)
        {
            chance = kHighChance;
            countdown.full = kHighSeconds;
        }
        else if (skill == kSkillSp)
        {
            chance = kSpChance;
            countdown.full = kSpSeconds;
        }
        countdown.frees = chance > 0.0f && Random01() < chance;
        countdown.left = countdown.full;
    }

    void Restart(Countdown& countdown, const std::uint8_t* ai, std::uint32_t soldier)
    {
        const ULONGLONG now = GetTickCount64();
        if (!countdown.lastSeenMs || now - countdown.lastSeenMs > kSameHoldupMs)
            Roll(countdown, ai, soldier);
        else
            countdown.left = countdown.full;
        countdown.lastSeenMs = now;
    }

    void CountDown(std::uint8_t* ai, std::uint32_t soldier, std::uint32_t proc, std::uint8_t* knowledge, std::uint8_t holdStep, std::uint8_t endStep)
    {
        if (soldier >= kMaxSoldiers)
            return;
        Countdown& countdown = g_Countdown[soldier];
        if (proc == kProcEnter || proc == kProcResume)
        {
            Restart(countdown, ai, soldier);
            g_HeardSnake[soldier].store(false, std::memory_order_release);
            g_Freed[soldier].store(false, std::memory_order_release);
            g_Report[soldier].store(false, std::memory_order_release);
            g_ReportUntilMs[soldier].store(0, std::memory_order_release);
            return;
        }
        if (proc != kProcUpdate || knowledge[kKnowledgeStep] != holdStep) // vanilla already left: comrade, Alert, discovery
            return;
        if (!countdown.lastSeenMs)
            Restart(countdown, ai, soldier);
        countdown.lastSeenMs = GetTickCount64();
        if (!countdown.frees || !SelfReleaseOn(soldier))   // switched off while he waits: vanilla, he stays
            return;

        Snapshot snapshot{};
        if (!ReadSnapshotSEH(ai, soldier, snapshot))
            return;
        const bool settled = (snapshot.status & kHeldUp) != 0 && (snapshot.status & kHoldupSettled) != 0;
        const bool heardSnake = g_HeardSnake[soldier].exchange(false, std::memory_order_acq_rel);
        if (!settled || heardSnake || PlayerIsClose(snapshot))   // interrogating him is always within that range
        {
            countdown.left = countdown.full;
            return;
        }

        const float delta = DeltaSeconds(snapshot, soldier);
        countdown.left -= delta;
        if (countdown.left > 0.0f)
            return;
        g_Freed[soldier].store(true, std::memory_order_release);
        g_Report[soldier].store(true, std::memory_order_release);
        knowledge[kKnowledgeStep] = endStep;
        countdown.frees = false;
    }

    void __fastcall hk_StepHoldupStand(std::uint8_t* ai, std::uint32_t soldier, std::uint32_t proc, std::uint8_t* knowledge)
    {
        MISSION_GUARD_ORIGINAL_VOID(g_OrigStand, ai, soldier, proc, knowledge);
        g_OrigStand(ai, soldier, proc, knowledge);
        CountDown(ai, soldier, proc, knowledge, kStepStand, kStepStandEnd);
    }

    void __fastcall hk_StepHoldupDown(std::uint8_t* ai, std::uint32_t soldier, std::uint32_t proc, std::uint8_t* knowledge)
    {
        MISSION_GUARD_ORIGINAL_VOID(g_OrigDown, ai, soldier, proc, knowledge);
        g_OrigDown(ai, soldier, proc, knowledge);
        CountDown(ai, soldier, proc, knowledge, kStepDown, kStepDownEnd);
    }

    void __fastcall hk_StepUnholster(std::uint8_t* ai, std::uint32_t soldier, std::uint32_t proc, std::uint8_t* knowledge)
    {
        MISSION_GUARD_ORIGINAL_VOID(g_OrigUnholster, ai, soldier, proc, knowledge);
        if ((proc == kProcEnter || proc == kProcResume) && soldier < kMaxSoldiers && g_Report[soldier].exchange(false, std::memory_order_acq_rel))
        {
            g_ReportUntilMs[soldier].store(GetTickCount64() + kReportWaitMs, std::memory_order_release);
            knowledge[kKnowledgeStep] = kStepDone; // like WakeUpAiImpl: the notice AI draws the gun and searches, this node sleeps
            return;
        }
        g_OrigUnholster(ai, soldier, proc, knowledge);
    }

    void __fastcall hk_CheckNoiseNoticePlayer(void* notices, std::uint32_t soldier, void* ai, const std::uint8_t* noise, void* knowledge)
    {
        MISSION_GUARD_ORIGINAL_VOID(g_OrigCheckNoise, notices, soldier, ai, noise, knowledge);
        if (soldier < kMaxSoldiers && (NoiseSourceSEH(noise) >> 9) == kPlayerType)
            g_HeardSnake[soldier].store(true, std::memory_order_release);   // only reaches him within the noise's own hearing range
        g_OrigCheckNoise(notices, soldier, ai, noise, knowledge);
    }
}

bool HoldupSelfRelease_TakeFreed(std::uint32_t soldier)
{
    return soldier < kMaxSoldiers && g_Freed[soldier].exchange(false, std::memory_order_acq_rel);
}

void HoldupSelfRelease_OnNoticeTick(std::uint8_t* noticeController, std::uint32_t soldier)
{
    if (soldier >= kMaxSoldiers)
        return;
    const ULONGLONG until = g_ReportUntilMs[soldier].load(std::memory_order_acquire);
    if (!until)
        return;
    if (GetTickCount64() > until)
    {
        g_ReportUntilMs[soldier].store(0, std::memory_order_release);
        return;
    }
    const std::uint8_t* hub = nullptr;
    __try
    {
        hub = *reinterpret_cast<std::uint8_t* const*>(noticeController + kNoticeHub);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return;
    }
    bool needsWeapon = true;
    if (GetTickCount64() + kReportWaitMs - until < kWeaponNodeWaitMs || !hub || !NeedsWeaponSEH(hub, soldier, needsWeapon) || needsWeapon)
        return;
    g_ReportUntilMs[soldier].store(0, std::memory_order_release);
    ReportAttack(hub, soldier);
}

bool HoldupSelfRelease_TakeRadioCall(std::uint32_t soldier)
{
    if (soldier >= kMaxSoldiers)
        return false;
    const ULONGLONG until = g_RadioUntilMs[soldier].exchange(0, std::memory_order_acq_rel);
    return until && GetTickCount64() <= until;
}

void HoldupSelfRelease_OnNodeSleep(std::uint32_t agent, void* node)
{
    if (agent >= kMaxSoldiers || !node || !g_ReportUntilMs[agent].load(std::memory_order_acquire) || MissionCodeGuard::ShouldBypassHooks())
        return;
    struct { std::uint64_t name; std::uint32_t knowledge; std::uint32_t pad; } spec = {};
    const std::uint8_t* hub = nullptr;
    __try
    {
        using GetSpec_t = void(__fastcall*)(void*, void*);
        reinterpret_cast<GetSpec_t>((*reinterpret_cast<void***>(node))[0])(node, &spec);
        hub = *reinterpret_cast<std::uint8_t* const*>(static_cast<std::uint8_t*>(node) + kNodeHub);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return;
    }
    if ((spec.name & 0xFFFFFFFFFFFFull) != kWeaponNodeName || !hub)
        return;
    g_ReportUntilMs[agent].store(0, std::memory_order_release);
    ReportAttack(hub, agent);
}

void HoldupSelfRelease_ClearForReload()
{
    for (std::uint32_t i = 0; i < kMaxSoldiers; ++i)
    {
        g_Countdown[i] = {};
        g_Freed[i].store(false, std::memory_order_relaxed);
        g_Report[i].store(false, std::memory_order_relaxed);
        g_ReportUntilMs[i].store(0, std::memory_order_relaxed);
        g_RadioUntilMs[i].store(0, std::memory_order_relaxed);
        g_HeardSnake[i].store(false, std::memory_order_relaxed);
    }
}

void HoldupSelfRelease_Clear()
{
    HoldupSelfRelease_ClearForReload();
    for (std::uint32_t i = 0; i < kMaxSoldiers; ++i)
        g_SoldierSetting[i].store(0, std::memory_order_relaxed);
}

void HoldupSelfRelease_SetEnabled(bool enabled)
{
    g_Enabled.store(enabled, std::memory_order_relaxed);
}

void HoldupSelfRelease_SetEnabledFor(std::uint32_t soldier, bool enabled)
{
    if (soldier < kMaxSoldiers)
        g_SoldierSetting[soldier].store(enabled ? 1 : 2, std::memory_order_relaxed);
}

bool Install_HoldupAiImpl_StepHoldupStand_Hooks()
{
    if (g_StandTarget)
        return true;
    const Addresses addresses = AddressesForBuild();
    if (!addresses.stand)
        return true;

    auto* stand  = static_cast<std::uint8_t*>(ResolveGameAddress(addresses.stand));
    auto* down   = static_cast<std::uint8_t*>(ResolveGameAddress(addresses.down));
    auto* unholster = static_cast<std::uint8_t*>(ResolveGameAddress(addresses.unholster));
    const HookEntry standEntry     = CheckHookEntry(stand, kStepPrologue, sizeof(kStepPrologue));
    const HookEntry downEntry      = CheckHookEntry(down, kStepPrologue, sizeof(kStepPrologue));
    const HookEntry unholsterEntry = CheckHookEntry(unholster, kUnholsterPrologue, sizeof(kUnholsterPrologue));
    if (standEntry == HookEntry::Unknown || downEntry == HookEntry::Unknown || unholsterEntry == HookEntry::Unknown)
    {
        LogError("HoldupSelfRelease: hold-up AI code does not match %s, held-up soldiers wait for a comrade as in vanilla", GetGameBuildName(gGameBuild));
        return false;
    }
    if (standEntry == HookEntry::ForeignHook)
        LogHookedOnTop("HoldupSelfRelease: HoldupAiImpl::StepHoldupStand", stand);
    if (downEntry == HookEntry::ForeignHook)
        LogHookedOnTop("HoldupSelfRelease: HoldupAiImpl::StepHoldupDown", down);
    if (unholsterEntry == HookEntry::ForeignHook)
        LogHookedOnTop("HoldupSelfRelease: HoldupAiImpl::StepUnholster", unholster);

    if (!CreateAndEnableHook(stand, reinterpret_cast<void*>(&hk_StepHoldupStand), reinterpret_cast<void**>(&g_OrigStand)))
    {
        LogError("HoldupSelfRelease: HoldupAiImpl::StepHoldupStand hook failed, held-up soldiers wait for a comrade as in vanilla");
        return false;
    }
    g_StandTarget = stand;
    if (!CreateAndEnableHook(down, reinterpret_cast<void*>(&hk_StepHoldupDown), reinterpret_cast<void**>(&g_OrigDown)))
    {
        LogError("HoldupSelfRelease: HoldupAiImpl::StepHoldupDown hook failed, held-up soldiers wait for a comrade as in vanilla");
        Uninstall_HoldupAiImpl_StepHoldupStand_Hooks();
        return false;
    }
    g_DownTarget = down;
    if (!CreateAndEnableHook(unholster, reinterpret_cast<void*>(&hk_StepUnholster), reinterpret_cast<void**>(&g_OrigUnholster)))
        LogError("HoldupSelfRelease: HoldupAiImpl::StepUnholster hook failed, a soldier who frees himself goes back to his route without calling it in");
    else
        g_UnholsterTarget = unholster;
    auto* checkNoise = static_cast<std::uint8_t*>(ResolveGameAddress(addresses.checkNoisePlayer));
    const HookEntry checkNoiseEntry = CheckHookEntry(checkNoise, kCheckNoisePrologue, sizeof(kCheckNoisePrologue));
    if (checkNoiseEntry == HookEntry::ForeignHook)
        LogHookedOnTop("HoldupSelfRelease: NoticeControllerImpl::CheckNoiseNoticePlayer", checkNoise);
    if (checkNoiseEntry == HookEntry::Unknown
        || !CreateAndEnableHook(checkNoise, reinterpret_cast<void*>(&hk_CheckNoiseNoticePlayer), reinterpret_cast<void**>(&g_OrigCheckNoise)))
        LogError("HoldupSelfRelease: NoticeControllerImpl::CheckNoiseNoticePlayer hook failed, Snake's noises don't hold back a soldier freeing himself");
    else
        g_CheckNoiseTarget = checkNoise;
    return true;
}

bool Uninstall_HoldupAiImpl_StepHoldupStand_Hooks()
{
    if (g_CheckNoiseTarget)
    {
        DisableAndRemoveHook(g_CheckNoiseTarget);
        g_CheckNoiseTarget = nullptr;
        g_OrigCheckNoise = nullptr;
    }
    if (g_UnholsterTarget)
    {
        DisableAndRemoveHook(g_UnholsterTarget);
        g_UnholsterTarget = nullptr;
        g_OrigUnholster = nullptr;
    }
    if (g_DownTarget)
    {
        DisableAndRemoveHook(g_DownTarget);
        g_DownTarget = nullptr;
        g_OrigDown = nullptr;
    }
    if (g_StandTarget)
    {
        DisableAndRemoveHook(g_StandTarget);
        g_StandTarget = nullptr;
        g_OrigStand = nullptr;
    }
    HoldupSelfRelease_Clear();
    return true;
}
