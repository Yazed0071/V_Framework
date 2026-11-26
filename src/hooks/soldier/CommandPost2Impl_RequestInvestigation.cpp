#include <pch.h>
#include "CommandPost2Impl_RequestInvestigation.h"

#include <Windows.h>
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>

#include "AddressSet.h"
#include "CpRadioVariants.h"
#include "FoxHashes.h"
#include "LuaBroadcaster.h"
#include "MissionCodeGuard.h"
#include "NoticeControllerImpl_CheckSightNoticePlayer.h"
#include "RadioControllerImpl_StepRadioWaitResponse.h"
#include "log.h"

// The CP makes its one investigation point at the spot and gives members the investigate role, as AssignInvestigation does in
// Caution. Its own state only: the survey team in CpRadioVariants shares nothing with it but that one point per CP.

namespace
{
    using RequestInvestigation_t = void(__fastcall*)(std::uint8_t* service, std::uint32_t cp, const void* request);
    using SetMemberRole_t        = void(__fastcall*)(std::uint8_t* service, std::uint32_t member, std::uint32_t role, const void* payload,
                                                     std::uint64_t size);
    using ClearInvestigation_t   = void(__fastcall*)(void* roleController, std::uint32_t cp, std::uint32_t kinds);

    constexpr std::size_t    kCpCount                  = 0x100;
    constexpr std::uint32_t  kDefaultMemberCount       = 4;        // InvestigateSituation's leader + 3
    constexpr std::uint64_t  kQueuedMostMs             = 30000;    // the CP sends them on its next radio-idle tick
    constexpr std::uint64_t  kMostOutMs                = 240000;   // vanilla never takes the investigate role back
    constexpr std::uint8_t   kSearchCompleteRadio      = 0x3A;     // CPR0070, an investigator's own end report
    constexpr std::uint16_t  kNoMember                 = 0xFFFF;

    constexpr std::ptrdiff_t kServiceToCpController    = -0x18;
    constexpr std::size_t    kServiceRoleController    = 0x18;
    constexpr std::size_t    kRequestInvestigationSlot = 0x80 / sizeof(void*);   // thunk into DoRequestInvestigationWithCpIndex
    constexpr std::size_t    kSetMemberRoleSlot        = 0x130 / sizeof(void*);
    constexpr std::size_t    kClearInvestigationSlot   = 0x28 / sizeof(void*);   // role controller
    constexpr std::size_t    kServiceRoles             = 0x50;     // 0x20 per member: payload, kind at +0x1D
    constexpr std::size_t    kRoleSize                 = 0x20;
    constexpr std::size_t    kRoleKind                 = 0x1D;
    constexpr std::size_t    kRolePointToken           = 0x0C;
    constexpr std::size_t    kServicePoints            = 0x80;     // one investigation point per CP
    constexpr std::size_t    kPointSize                = 0x1C;
    constexpr std::size_t    kPointCause               = 0x10;
    constexpr std::size_t    kPointId                  = 0x12;
    constexpr std::size_t    kPointArea                = 0x14;
    constexpr std::size_t    kPointFlags               = 0x16;
    constexpr std::size_t    kPointInvestigators       = 0x18;
    constexpr std::uint8_t   kPointActive              = 0x40;
    constexpr std::size_t    kRoleLocators             = 0x30;     // role controller: 0x30 per combat locator, its member count at +0x29
    constexpr std::size_t    kLocatorSize              = 0x30;
    constexpr std::size_t    kLocatorMembers           = 0x29;
    constexpr std::size_t    kServiceMemberRadioFlags  = 0x58;     // 0x14 per member
    constexpr std::size_t    kMemberRadioFlagsSize     = 0x14;
    constexpr std::size_t    kMemberPosition           = 0x04;
    constexpr std::size_t    kMemberAbilities          = 0x12;
    constexpr std::uint32_t  kMemberUp                 = 0x80001;
    constexpr std::uint32_t  kInvestigatorBusy         = 0x7303C214;   // AssignInvestigation's bits 2/9/15/16/17/24/28 and 0x62004010
    constexpr std::uint8_t   kCanInvestigate           = 0x02;
    constexpr std::uint8_t   kInvestigateRole          = 1;
    constexpr std::uint8_t   kGuardRole                = 3;
    constexpr std::uint16_t  kNoLocator                = 0x100;
    constexpr std::uint8_t   kNoticeInvestigation      = 3;        // the kind the soldiers' own notice requests carry
    constexpr std::uint16_t  kNoCause                  = 0x1FF;
    constexpr std::uint32_t  kAllKinds                 = 0xFFFFFFFF;
    constexpr std::size_t    kCpControllerMembers      = 0x58;     // 0x18 per member
    constexpr std::size_t    kCpControllerMemberTotal  = 0x174;
    constexpr std::size_t    kMemberSize               = 0x18;
    constexpr std::size_t    kMemberGameObjectId       = 0x10;
    constexpr std::size_t    kMemberCp                 = 0x12;
    constexpr std::uint16_t  kSoldierType              = 0x400;
    constexpr std::uint16_t  kTypeMask                 = 0xFE00;
    constexpr std::uint16_t  kIndexMask                = 0x1FF;
    constexpr std::uint32_t  kCommandPostType          = 0x200;
    constexpr std::size_t    kControllerSoldiers       = 0x58;
    constexpr std::size_t    kSoldiersLife             = 0x38;     // 0x40 per soldier at +8
    constexpr std::size_t    kLifeRecords              = 0x08;
    constexpr std::size_t    kLifeRecordSize           = 0x40;
    constexpr std::size_t    kLifeFlags                = 0x12;
    constexpr std::uint16_t  kLifeFlagsStatus          = 0x3;      // 0 normal, 1 sleep, 2 faint, 3 dying
    constexpr std::size_t    kLifeState                = 0x3E;
    constexpr std::uint8_t   kLifeStateDead            = 2;

    enum class Stage : std::uint8_t { None, Queued, Out };
    struct Investigation
    {
        Stage         stage      = Stage::None;
        std::uint32_t serial     = 0;
        std::uint8_t* service    = nullptr;    // the mission it belongs to
        float         spot[3]    = {};
        float         pointAt[3] = {};         // where the CP put the point; it moves it when it hands the point to another job
        std::uint32_t members[kCpInvestigationMost]  = {};
        std::uint32_t soldiers[kCpInvestigationMost] = {};
        std::uint32_t count      = 0;
        std::uint16_t token      = 0;
        std::uint32_t endLabel   = 0;
        bool          silent     = false;
        bool          takeOver   = false;
        bool          sending    = false;      // the CP tick has it: a takeOver now is reported by that tick
        std::uint64_t queuedUntilMs = 0;
        std::uint64_t untilMs    = 0;
    };
    struct Team   // ours, out, replaced by a takeOver: it goes back before the new one sets off
    {
        std::uint32_t members[kCpInvestigationMost] = {};
        std::uint32_t count = 0;
        std::uint16_t token = 0;
        bool          due   = false;
    };

    std::mutex                                       g_Mutex;
    std::array<Investigation, kCpCount>              g_Investigations{};
    std::array<Team, kCpCount>                       g_Replaced{};
    std::array<std::atomic<std::uint32_t>, kCpCount> g_MemberCount{};
    std::atomic<std::uint8_t*>                       g_Service{ nullptr };
    std::uint32_t                                    g_NextSerial = 0;

    bool SupportedBuild()   // the CP offsets were read on 1.0.15.4 / 1.0.15.4a
    {
        switch (gGameBuild)
        {
        case ::AddressSetRuntime::GameBuild::En_1_0_15_4a:
        case ::AddressSetRuntime::GameBuild::Jp_1_0_15_4a: return true;
        default:                                           return false;
        }
    }

    void EmitEnd(std::uint32_t cp, const char* reason, std::uint32_t count)
    {
        V_FrameWork::EmitMessage("GameObject", "InvestigationEnd", kCommandPostType | cp, FoxHashes::StrCode32(reason), count);
    }

    Team TeamOf(const Investigation& out)
    {
        Team team;
        team.due = true;
        team.token = out.token;
        team.count = out.count;
        for (std::uint32_t i = 0; i < out.count; ++i)
            team.members[i] = out.members[i];
        return team;
    }

    bool InTeam(const Team& team, std::uint32_t member)
    {
        for (std::uint32_t i = 0; i < team.count; ++i)
        {
            if (team.members[i] == member)
                return true;
        }
        return false;
    }

    bool MemberEntry(std::uint8_t* service, std::uint32_t member, std::uint16_t& gameObjectId, std::uint8_t& cp)
    {
        __try
        {
            const std::uint8_t* entry = *reinterpret_cast<std::uint8_t* const*>(service + kServiceToCpController + kCpControllerMembers)
                + member * kMemberSize;
            gameObjectId = *reinterpret_cast<const std::uint16_t*>(entry + kMemberGameObjectId);
            cp = entry[kMemberCp];
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    std::uint32_t MemberTotal(std::uint8_t* service)
    {
        __try
        {
            return *reinterpret_cast<const std::uint32_t*>(service + kServiceToCpController + kCpControllerMemberTotal);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    std::uint8_t RoleKindOf(std::uint8_t* service, std::uint32_t member)
    {
        __try
        {
            return (*reinterpret_cast<std::uint8_t* const*>(service + kServiceRoles))[member * kRoleSize + kRoleKind];
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0xFF;
        }
    }

    bool CanInvestigate(std::uint8_t* service, std::uint32_t member, std::uint32_t soldier, bool ours, float* position)   // AssignInvestigation's own test
    {
        __try
        {
            const std::uint8_t* record = *reinterpret_cast<std::uint8_t* const*>(service + kServiceMemberRadioFlags) + member * kMemberRadioFlagsSize;
            const std::uint32_t flags = *reinterpret_cast<const std::uint32_t*>(record);
            if ((flags & kMemberUp) != kMemberUp || !(record[kMemberAbilities] & kCanInvestigate) || (!ours && (flags & kInvestigatorBusy)))
                return false;
            const float* at = reinterpret_cast<const float*>(record + kMemberPosition);
            position[0] = at[0];
            position[1] = at[1];
            position[2] = at[2];
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
        bool heldKnown = false;
        return (ours || RoleKindOf(service, member) == 0) && !CpRadioResponse::IsHeld(static_cast<std::uint16_t>(soldier), heldKnown);
    }

    bool OutOfAction(std::uint32_t soldier)   // killed, knocked out or held up
    {
        bool heldKnown = false;
        if (CpRadioResponse::IsHeld(static_cast<std::uint16_t>(soldier), heldKnown))
            return true;
        void* noticeController = SoldierNotice_LiveController();
        if (!noticeController)
            return false;
        __try
        {
            const std::uint8_t* soldiers = *reinterpret_cast<const std::uint8_t* const*>(static_cast<std::uint8_t*>(noticeController) + kControllerSoldiers);
            const auto* life = *reinterpret_cast<const std::uint8_t* const*>(
                *reinterpret_cast<const std::uint8_t* const*>(soldiers + kSoldiersLife) + kLifeRecords) + soldier * kLifeRecordSize;
            return life[kLifeState] == kLifeStateDead || (*reinterpret_cast<const std::uint16_t*>(life + kLifeFlags) & kLifeFlagsStatus) != 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    std::uint32_t PickNearest(std::uint8_t* service, std::uint32_t cp, std::uint32_t want, const Team& goingBack, Investigation& order)
    {
        float distance[kCpInvestigationMost] = {};
        const std::uint32_t total = MemberTotal(service);
        order.count = 0;
        for (std::uint32_t member = 0; member < total; ++member)
        {
            std::uint16_t gameObjectId = 0;
            std::uint8_t memberCp = 0;
            float at[3] = {};
            if (!MemberEntry(service, member, gameObjectId, memberCp) || memberCp != cp || (gameObjectId & kTypeMask) != kSoldierType
                || !CanInvestigate(service, member, gameObjectId & kIndexMask, InTeam(goingBack, member), at)
                || CpRadioVariants_IsWatchingFinder(gameObjectId & kIndexMask))
                continue;
            const float dx = at[0] - order.spot[0];
            const float dy = at[1] - order.spot[1];
            const float dz = at[2] - order.spot[2];
            const float d = dx * dx + dy * dy + dz * dz;
            std::uint32_t slot = order.count;
            while (slot > 0 && distance[slot - 1] > d)
                --slot;
            if (slot >= want)
                continue;
            for (std::uint32_t i = (order.count < want ? order.count : want - 1); i > slot; --i)
            {
                distance[i] = distance[i - 1];
                order.members[i] = order.members[i - 1];
                order.soldiers[i] = order.soldiers[i - 1];
            }
            distance[slot] = d;
            order.members[slot] = member;
            order.soldiers[slot] = gameObjectId & kIndexMask;
            if (order.count < want)
                ++order.count;
        }
        return order.count;
    }

    std::uint32_t PickNamed(std::uint8_t* service, std::uint32_t cp, const CpInvestigationRequest& request, const Team& goingBack,
                            Investigation& order)
    {
        const std::uint32_t total = MemberTotal(service);
        order.count = 0;
        for (std::uint32_t i = 0; i < request.soldierCount && i < kCpInvestigationMost; ++i)
        {
            const std::uint32_t soldier = request.soldiers[i];
            bool taken = false;
            for (std::uint32_t member = 0; member < total && !taken; ++member)
            {
                std::uint16_t gameObjectId = 0;
                std::uint8_t memberCp = 0;
                float at[3] = {};
                if (!MemberEntry(service, member, gameObjectId, memberCp) || gameObjectId != (kSoldierType | soldier))
                    continue;
                if (memberCp != cp || !CanInvestigate(service, member, soldier, InTeam(goingBack, member), at))
                    break;
                order.members[order.count] = member;
                order.soldiers[order.count] = soldier;
                ++order.count;
                taken = true;
            }
            if (!taken)
                LogWarn("RequestInvestigation: soldier %u is not a free member of CP %u, left out", soldier, cp);
        }
        return order.count;
    }

    std::uint8_t* PointOf(std::uint8_t* service, std::uint32_t cp)
    {
        return *reinterpret_cast<std::uint8_t* const*>(service + kServicePoints) + cp * kPointSize;
    }

    std::uint16_t PointToken(std::uint32_t cp, const std::uint8_t* point)   // what the role payload and CompleteInvestigation carry
    {
        return static_cast<std::uint16_t>(((cp & 0x7F) << 7) | (*reinterpret_cast<const std::uint16_t*>(point + kPointId) & 0x7F));
    }

    bool PointBusy(std::uint8_t* service, std::uint32_t cp)   // the CP's one point already has investigators
    {
        __try
        {
            const std::uint8_t* point = PointOf(service, cp);
            return (point[kPointFlags] & kPointActive) && point[kPointInvestigators] != 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return true;
        }
    }

    bool PointIsOurs(std::uint8_t* service, std::uint32_t cp, std::uint16_t token)
    {
        __try
        {
            const std::uint8_t* point = PointOf(service, cp);
            return (point[kPointFlags] & kPointActive) && PointToken(cp, point) == token;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool ReadPointAt(std::uint8_t* service, std::uint32_t cp, float* out)
    {
        __try
        {
            const float* at = reinterpret_cast<const float*>(PointOf(service, cp));
            out[0] = at[0];
            out[1] = at[1];
            out[2] = at[2];
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool RequestPoint(std::uint8_t* service, std::uint32_t cp, const float* spot)
    {
        struct InvestigationRequest
        {
            float         x, y, z;
            std::uint16_t cause;
            std::uint8_t  kind;
            std::uint8_t  pad;
        } request{ spot[0], spot[1], spot[2], kNoCause, kNoticeInvestigation, 0 };
        __try
        {
            const auto requestInvestigation = (*reinterpret_cast<RequestInvestigation_t* const*>(service))[kRequestInvestigationSlot];
            requestInvestigation(service, cp, &request);
            return (PointOf(service, cp)[kPointFlags] & kPointActive) != 0;   // it clamps the spot into the CP's area, or drops it
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool GiveInvestigateRole(std::uint8_t* service, std::uint32_t cp, std::uint32_t member)   // as AssignInvestigation does it
    {
        __try
        {
            std::uint8_t* point = PointOf(service, cp);
            std::uint8_t payload[0x14] = {};
            *reinterpret_cast<float*>(payload + 0x0) = *reinterpret_cast<const float*>(point + 0x0);
            *reinterpret_cast<float*>(payload + 0x4) = *reinterpret_cast<const float*>(point + 0x4);
            *reinterpret_cast<float*>(payload + 0x8) = *reinterpret_cast<const float*>(point + 0x8);
            *reinterpret_cast<std::uint16_t*>(payload + 0xC) = PointToken(cp, point);
            *reinterpret_cast<std::uint16_t*>(payload + 0xE) = *reinterpret_cast<const std::uint16_t*>(point + kPointArea);
            *reinterpret_cast<std::uint16_t*>(payload + 0x10) = *reinterpret_cast<const std::uint16_t*>(point + kPointCause) & kNoCause;
            std::uint8_t* role = *reinterpret_cast<std::uint8_t* const*>(service + kServiceRoles) + member * kRoleSize;
            const std::uint16_t locator = *reinterpret_cast<const std::uint16_t*>(role);
            if ((role[kRoleKind] == 0 || role[kRoleKind] == kGuardRole) && locator != kNoLocator)
            {
                std::uint8_t* roles = *reinterpret_cast<std::uint8_t* const*>(service + kServiceRoleController);
                std::uint8_t& onLocator = (*reinterpret_cast<std::uint8_t* const*>(roles + kRoleLocators))[locator * kLocatorSize + kLocatorMembers];
                if (onLocator)
                    --onLocator;
            }
            const auto setMemberRole = (*reinterpret_cast<SetMemberRole_t* const*>(service))[kSetMemberRoleSlot];
            setMemberRole(service, member, kInvestigateRole, payload, sizeof(payload));
            if (role[kRoleKind] != kInvestigateRole)
                return false;
            ++point[kPointInvestigators];   // Caution's own assigner skips a point that has investigators
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool OnPoint(std::uint8_t* service, std::uint32_t member, std::uint16_t token)   // investigating that point: the game hasn't given him another job
    {
        __try
        {
            const std::uint8_t* role = *reinterpret_cast<std::uint8_t* const*>(service + kServiceRoles) + member * kRoleSize;
            return role[kRoleKind] == kInvestigateRole && *reinterpret_cast<const std::uint16_t*>(role + kRolePointToken) == token;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void TakeRoleBack(std::uint8_t* service, std::uint32_t member)   // StepRadioToSneak's own release
    {
        const std::uint16_t none = kNoLocator;
        __try
        {
            const auto setMemberRole = (*reinterpret_cast<SetMemberRole_t* const*>(service))[kSetMemberRoleSlot];
            setMemberRole(service, member, 0, &none, sizeof(none));
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    void ClearPoint(std::uint8_t* service, std::uint32_t cp)
    {
        __try
        {
            void* roles = *reinterpret_cast<void* const*>(service + kServiceRoleController);
            const auto clear = (*reinterpret_cast<ClearInvestigation_t* const*>(roles))[kClearInvestigationSlot];
            clear(roles, cp, kAllKinds);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    void ReleaseTeam(std::uint8_t* service, const std::uint32_t* members, std::uint32_t count, std::uint16_t token)
    {
        for (std::uint32_t i = 0; i < count; ++i)
        {
            if (OnPoint(service, members[i], token))
                TakeRoleBack(service, members[i]);
        }
    }

    void RecallSquad(std::uint8_t* service, std::uint32_t cp)   // ClearInvestigation drops the point but leaves whoever was on it with the role
    {
        std::uint16_t token = 0;
        __try
        {
            token = PointToken(cp, PointOf(service, cp));
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return;
        }
        const std::uint32_t total = MemberTotal(service);
        for (std::uint32_t member = 0; member < total; ++member)
        {
            std::uint16_t gameObjectId = 0;
            std::uint8_t memberCp = 0;
            if (MemberEntry(service, member, gameObjectId, memberCp) && memberCp == cp && OnPoint(service, member, token))
                TakeRoleBack(service, member);
        }
    }

    bool NearSpot(const float* a, const float* b, float range)
    {
        const float dx = a[0] - b[0];
        const float dy = a[1] - b[1];
        const float dz = a[2] - b[2];
        return dx * dx + dy * dy + dz * dz < range * range;
    }

    void Send(std::uint8_t* service, std::uint32_t cp, Investigation order)
    {
        Team goingBack;
        {
            std::lock_guard<std::mutex> lock(g_Mutex);
            goingBack = g_Replaced[cp];
            g_Replaced[cp] = Team{};
        }
        if (goingBack.due)
        {
            ReleaseTeam(service, goingBack.members, goingBack.count, goingBack.token);
            EmitEnd(cp, "Replaced", goingBack.count);
        }
        if (order.stage != Stage::Queued)
            return;

        std::uint32_t kept = 0;
        for (std::uint32_t i = 0; i < order.count; ++i)
        {
            float at[3] = {};
            if (!CanInvestigate(service, order.members[i], order.soldiers[i], InTeam(goingBack, order.members[i]), at))
                continue;
            order.members[kept] = order.members[i];
            order.soldiers[kept] = order.soldiers[i];
            ++kept;
        }
        order.count = kept;
        const bool busy = PointBusy(service, cp);
        const char* refused = GetTickCount64() > order.queuedUntilMs ? "the CP took too long to get to it"
            : order.count == 0 ? "nobody free any more"
            : busy && !order.takeOver ? "the CP is already investigating" : nullptr;
        if (!refused && busy)
        {
            RecallSquad(service, cp);
            ClearPoint(service, cp);
        }
        if (!refused && !RequestPoint(service, cp, order.spot))
            refused = "the CP dropped the spot";
        if (!refused)
        {
            order.token = PointToken(cp, PointOf(service, cp));
            ReadPointAt(service, cp, order.pointAt);
            kept = 0;
            for (std::uint32_t i = 0; i < order.count; ++i)
            {
                if (!GiveInvestigateRole(service, cp, order.members[i]))
                    continue;
                order.members[kept] = order.members[i];
                order.soldiers[kept] = order.soldiers[i];
                ++kept;
            }
            order.count = kept;
            if (order.count == 0)
            {
                ClearPoint(service, cp);
                refused = "nobody took the investigate role";
            }
        }

        bool stale = false;
        {
            std::lock_guard<std::mutex> lock(g_Mutex);
            Investigation& stored = g_Investigations[cp];
            stale = stored.serial != order.serial;
            if (!stale && refused)
                stored = Investigation{};
            else if (!stale)
            {
                order.stage = Stage::Out;
                order.sending = false;
                order.untilMs = GetTickCount64() + kMostOutMs;
                stored = order;
            }
        }
        if (stale)   // a takeOver came in while they were being sent: they go straight back
        {
            if (!refused)
            {
                ReleaseTeam(service, order.members, order.count, order.token);
                ClearPoint(service, cp);
            }
            EmitEnd(cp, "Replaced", refused ? 0 : order.count);
            return;
        }
        if (refused)
        {
            EmitEnd(cp, "Refused", 0);
            return;
        }
    }

    void Track(std::uint8_t* service, std::uint32_t cp, const Investigation& out)
    {
        bool anyOurs = false;
        bool allDown = out.count != 0;
        for (std::uint32_t i = 0; i < out.count; ++i)
        {
            anyOurs = OnPoint(service, out.members[i], out.token) || anyOurs;
            allDown = allDown && OutOfAction(out.soldiers[i]);
        }
        const bool pointOurs = PointIsOurs(service, cp, out.token);
        const bool late = GetTickCount64() > out.untilMs;
        if (anyOurs && pointOurs && !late && !allDown)
            return;
        {
            std::lock_guard<std::mutex> lock(g_Mutex);
            if (g_Investigations[cp].serial != out.serial)
                return;   // a takeOver replaced it meanwhile: its hand-off calls them back and reports it
            g_Investigations[cp] = Investigation{};
        }
        ReleaseTeam(service, out.members, out.count, out.token);
        float pointNow[3] = {};
        const bool unmoved = ReadPointAt(service, cp, pointNow) && NearSpot(pointNow, out.pointAt, 1.0f);
        if (pointOurs && (anyOurs || unmoved))   // overwriting a point keeps its id: moved and nobody of ours on it, it's another job now
            ClearPoint(service, cp);
        const char* reason = allDown ? "TakenOut" : !pointOurs ? "Done" : !anyOurs ? "Reassigned" : "TimeUp";
        EmitEnd(cp, reason, out.count);
    }
}

std::uint32_t CpInvestigation_Request(std::uint32_t cp, const CpInvestigationRequest& request, std::uint32_t* pickedSoldiers)
{
    if (cp >= kCpCount || !pickedSoldiers)
        return 0;
    if (!SupportedBuild())
    {
        LogWarn("RequestInvestigation: not available on %s, ignored", GetGameBuildName(gGameBuild));
        return 0;
    }
    std::uint8_t* service = g_Service.load(std::memory_order_relaxed);
    if (!service)
    {
        LogWarn("RequestInvestigation: CP %u hasn't run yet this mission, ignored", cp);
        return 0;
    }
    const std::uint64_t now = GetTickCount64();
    bool ours = false;
    Team goingBack;   // our team out there is free to go again: takeOver calls it back first
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        const Investigation& stored = g_Investigations[cp];
        if (stored.service == service)
        {
            ours = stored.stage == Stage::Out || (stored.stage == Stage::Queued && now < stored.queuedUntilMs);
            if (request.takeOver && stored.stage == Stage::Out)
                goingBack = TeamOf(stored);
        }
    }
    if (!request.takeOver && (ours || PointBusy(service, cp)))
    {
        LogWarn("RequestInvestigation: CP %u is already investigating and takeOver is false, ignored", cp);
        return 0;
    }

    Investigation order;
    order.stage = Stage::Queued;
    order.service = service;
    order.spot[0] = request.spot[0];
    order.spot[1] = request.spot[1];
    order.spot[2] = request.spot[2];
    order.endLabel = request.endLabel;
    order.silent = request.silent;
    order.takeOver = request.takeOver;
    order.queuedUntilMs = now + kQueuedMostMs;
    std::uint32_t want = request.memberCount ? request.memberCount : g_MemberCount[cp].load(std::memory_order_relaxed);
    if (want == 0)
        want = kDefaultMemberCount;
    if (want > kCpInvestigationMost)
        want = kCpInvestigationMost;
    if ((request.soldierCount ? PickNamed(service, cp, request, goingBack, order) : PickNearest(service, cp, want, goingBack, order)) == 0)
    {
        LogWarn("RequestInvestigation: CP %u has nobody free to send, ignored", cp);
        return 0;
    }

    bool queuedReplaced = false;
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        Investigation& stored = g_Investigations[cp];
        if (stored.service == service && stored.stage == Stage::Out)
            g_Replaced[cp] = TeamOf(stored);
        queuedReplaced = stored.service == service && stored.stage == Stage::Queued && !stored.sending;
        order.serial = ++g_NextSerial;
        stored = order;
    }
    if (queuedReplaced)
        EmitEnd(cp, "Replaced", 0);
    for (std::uint32_t i = 0; i < order.count; ++i)
        pickedSoldiers[i] = order.soldiers[i];
    return order.count;
}

void CpInvestigation_SetMemberCount(std::uint32_t cp, std::uint32_t count)
{
    if (cp < kCpCount)
        g_MemberCount[cp].store(count, std::memory_order_relaxed);
}

void CpInvestigation_SetEveryMemberCount(std::uint32_t count)
{
    for (auto& stored : g_MemberCount)
        stored.store(count, std::memory_order_relaxed);
}

void CpInvestigation_OnCpIdle(std::uint32_t cp, std::uint8_t* service)
{
    if (cp >= kCpCount || !service || !SupportedBuild() || MissionCodeGuard::ShouldBypassHooks())
        return;
    g_Service.store(service, std::memory_order_relaxed);
    Investigation current;
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        Investigation& stored = g_Investigations[cp];
        if (stored.stage == Stage::None && !g_Replaced[cp].due)
            return;
        if (stored.stage != Stage::None && stored.service != service)
        {
            stored = Investigation{};   // an earlier mission's
            g_Replaced[cp] = Team{};
            return;
        }
        if (stored.stage == Stage::Queued)
            stored.sending = true;
        current = stored;
    }
    if (current.stage == Stage::Out)
        Track(service, cp, current);
    else
        Send(service, cp, current);
}

bool CpInvestigation_EndCall(std::uint32_t cp, std::uint8_t radioType, std::uint16_t member, std::uint32_t& label)
{
    if (radioType != kSearchCompleteRadio || cp >= kCpCount || member == kNoMember)
        return false;
    std::lock_guard<std::mutex> lock(g_Mutex);
    const Investigation& out = g_Investigations[cp];
    if (out.stage != Stage::Out || (!out.silent && !out.endLabel))
        return false;
    for (std::uint32_t i = 0; i < out.count; ++i)
    {
        if (out.members[i] == member || (kSoldierType | out.soldiers[i]) == member)
        {
            label = out.silent ? 0 : out.endLabel;
            return true;
        }
    }
    return false;
}

void CpInvestigation_ClearForNewMission()
{
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        g_Investigations.fill(Investigation{});
        g_Replaced.fill(Team{});
    }
    for (auto& count : g_MemberCount)
        count.store(0, std::memory_order_relaxed);
    g_Service.store(nullptr, std::memory_order_relaxed);
}
