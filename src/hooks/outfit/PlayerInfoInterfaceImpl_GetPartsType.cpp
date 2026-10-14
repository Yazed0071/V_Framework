#include "pch.h"

#include "PlayerInfoInterfaceImpl_GetPartsType.h"
#include "OutfitAbilities.h"
#include "OutfitRegistry.h"
#include "BlockControllerImpl_LoadPartsNew.h"
#include "MissionCodeGuard.h"

#include <intrin.h>
#include <cstdint>
#include <cstring>
#include <iterator>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    constexpr std::size_t   kMaxReturnSites = 8;
    constexpr std::uint32_t kPartsRaiden    = 6;
    constexpr std::uint32_t kNeedSpark      = 1u << 30;

    constexpr std::uint8_t kPrologue_GetPartsType[] = { 0x48, 0x83, 0xEC, 0x28, 0xE8 };
    constexpr std::uint8_t kEpilogue_GetPartsType[] =
        { 0x0F, 0xB7, 0x80, 0x8A, 0x00, 0x00, 0x00 };

    struct ReturnSite
    {
        std::uintptr_t addr;
        std::uint32_t  need;
    };

    struct GetterSites
    {
        std::uintptr_t    getter;
        const ReturnSite* returns;
        std::size_t       returnCount;
    };

    constexpr ReturnSite kLocalReturns_En154[] =
    {
        { 0x14093027Dull, outfit::kSprintRaidenRun },
        { 0x14098D41Aull, outfit::kSprintDashSound },
    };
    constexpr ReturnSite kIndexReturns_En154[] =
    {
        { 0x140743509ull, kNeedSpark }, { 0x140743519ull, kNeedSpark }, { 0x140743558ull, kNeedSpark },
        { 0x140743616ull, kNeedSpark }, { 0x140743626ull, kNeedSpark }, { 0x1407448C5ull, kNeedSpark },
        { 0x1407448D5ull, kNeedSpark },
    };

    constexpr GetterSites kLocal_En154 =
        { 0x141E02D50ull, kLocalReturns_En154, std::size(kLocalReturns_En154) };
    constexpr GetterSites kIndex_En154 =
        { 0x141E02B80ull, kIndexReturns_En154, std::size(kIndexReturns_En154) };

    constexpr std::uintptr_t kDashMove_En154   = 0x1410B52F0ull;
    constexpr std::uintptr_t kDashAttack_En154 = 0x1410A0660ull;
    constexpr std::uint8_t kDashMoveCode[] =
        { 0x40, 0x53, 0x55, 0x56, 0x57, 0x48, 0x81, 0xEC, 0x88, 0x00, 0x00, 0x00 };
    constexpr std::uint8_t kDashAttackCode[] =
        { 0x41, 0xF6, 0x80, 0xAC, 0x02, 0x00, 0x00, 0x01, 0x74, 0x24 };
    constexpr std::size_t   kActionWorkStride = 0x390;
    constexpr std::size_t   kWork_DashFlags   = 0x2AC;
    constexpr std::size_t   kWork_RunStyle    = 0x328;   // motion graph variable 0xf8fbbf38316a: 1 = Raiden's run, 2 = Quiet's
    constexpr std::uint32_t kMaxActionPlayers = 4;
    constexpr std::size_t   kAction_DashAttackId  = 0x238;   // u16 attack id in the dash hit's info at +0x230
    constexpr std::uint16_t kVanillaDashAttackId  = 0x55;    // ATK_CBoxBodyAttack, what AllocateWork puts there

    using GetPartsTypeLocal_t = std::uint32_t (__fastcall*)(void* self);
    using GetPartsType_t      = std::uint32_t (__fastcall*)(std::uint32_t playerIndex);
    using DashMove_t          = void (__fastcall*)(void* self, std::uint32_t player, std::uint32_t process, void* arg);
    using DashAttack_t        = bool (__fastcall*)(void* self, std::uint32_t player, void* work);

    struct HookState
    {
        std::uintptr_t target = 0;
        ReturnSite     returns[kMaxReturnSites] = {};
        std::size_t    returnCount = 0;
        bool           installed = false;
    };

    static HookState           g_Local;
    static HookState           g_Index;
    static GetPartsTypeLocal_t g_OrigLocal = nullptr;
    static GetPartsType_t      g_OrigIndex = nullptr;

    static std::uintptr_t g_DashMoveTarget   = 0;
    static std::uintptr_t g_DashAttackTarget = 0;
    static DashMove_t     g_OrigDashMove     = nullptr;
    static DashAttack_t   g_OrigDashAttack   = nullptr;

    static std::uint32_t ListedNeed(const HookState& h, const void* ret)
    {
        const auto r = reinterpret_cast<std::uintptr_t>(ret);
        for (std::size_t i = 0; i < h.returnCount; ++i)
            if (h.returns[i].addr == r) return h.returns[i].need;
        return 0;
    }

    static std::uint32_t LiveSprint()
    {
        if (MissionCodeGuard::ShouldBypassHooks())
            return 0;
        std::uint32_t live = outfit::SprintLive() & ~outfit::kSprintSpeedMask;
        if (outfit::SprintSparkLive() != 0)
            live |= kNeedSpark;
        return live;
    }

    static std::uint32_t __fastcall hkGetPartsTypeLocal(void* self)
    {
        const std::uint32_t need = ListedNeed(g_Local, _ReturnAddress());
        if (need && (LiveSprint() & need))
            return kPartsRaiden;
        return g_OrigLocal(self);
    }

    static std::uint32_t __fastcall hkGetPartsType(std::uint32_t playerIndex)
    {
        if (playerIndex == outfit::GetLocalPartsSlot())
        {
            const std::uint32_t need = ListedNeed(g_Index, _ReturnAddress());
            if (need && (LiveSprint() & need))
                return kPartsRaiden;
        }
        return g_OrigIndex(playerIndex);
    }

    static bool SetRaidenRunSEH(std::uint8_t* self, std::uint32_t player)
    {
        __try
        {
            const auto first = *reinterpret_cast<const std::uint32_t*>(
                *reinterpret_cast<std::uint8_t* const*>(self + 0x38) + 0x24);
            auto* works = *reinterpret_cast<std::uint8_t**>(self + 0x1A0);
            if (!works || player < first || player - first >= kMaxActionPlayers)
                return false;
            auto* style = reinterpret_cast<std::uint32_t*>(
                works + static_cast<std::size_t>(player - first) * kActionWorkStride + kWork_RunStyle);
            if (*style != 0)
                return false;
            *style = 1;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    static bool DashReadySEH(const void* work)
    {
        __try { return (static_cast<const std::uint8_t*>(work)[kWork_DashFlags] & 1) != 0; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    static void SetDashAttackIdSEH(std::uint8_t* self, std::uint16_t id)
    {
        __try { *reinterpret_cast<std::uint16_t*>(self + kAction_DashAttackId) = id; }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    static void __fastcall hkDashMove(void* self, std::uint32_t player, std::uint32_t process, void* arg)
    {
        g_OrigDashMove(self, player, process, arg);
        static bool s_logged = false;
        if (self && process <= 1 && player == outfit::GetLocalPartsSlot()
            && (LiveSprint() & outfit::kSprintRaidenRun)
            && SetRaidenRunSEH(static_cast<std::uint8_t*>(self), player) && !s_logged)
        {
            s_logged = true;
            LogInfo("OutfitSprint: raidenRun on the sprint");
        }
    }

    static bool __fastcall hkDashAttack(void* self, std::uint32_t player, void* work)
    {
        bool on = g_OrigDashAttack(self, player, work);
        const std::uint32_t live = player == outfit::GetLocalPartsSlot() ? LiveSprint() : 0;
        const bool ours = (live & outfit::kSprintDashAttack) != 0;
        if (!on && ours && work && DashReadySEH(work))
            on = true;
        if (on && self)
        {
            const auto id = static_cast<std::uint16_t>(ours ? (live & outfit::kSprintAttackMask) >> outfit::kSprintAttackShift : 0);
            const std::uint16_t use = id ? id : kVanillaDashAttackId;
            SetDashAttackIdSEH(static_cast<std::uint8_t*>(self), use);
            static std::uint32_t s_lastLogged = 0;
            if (ours && s_lastLogged != use + 1u)
            {
                s_lastLogged = use + 1u;
                LogInfo("OutfitSprint: dashAttack armed with attack 0x%X", static_cast<unsigned>(use));
            }
        }
        return on;
    }

    static bool ReadCode_SEH(std::uintptr_t addr, void* out, std::size_t n)
    {
        __try
        {
            std::memcpy(out, reinterpret_cast<const void*>(addr), n);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    static bool CodeIs(std::uintptr_t addr, const std::uint8_t* expect, std::size_t n)
    {
        std::uint8_t b[16] = {};
        return n <= sizeof(b) && ReadCode_SEH(addr, b, n) && std::memcmp(b, expect, n) == 0;
    }

    static bool GetterMatches(std::uintptr_t fn)
    {
        std::uint8_t b[16] = {};
        if (!ReadCode_SEH(fn, b, sizeof(b))) return false;
        return std::memcmp(b, kPrologue_GetPartsType, sizeof(kPrologue_GetPartsType)) == 0
            && std::memcmp(b + 9, kEpilogue_GetPartsType, sizeof(kEpilogue_GetPartsType)) == 0;
    }

    static bool CallsGetter(std::uintptr_t ret, std::uintptr_t fn)
    {
        std::uint8_t b[5] = {};
        if (!ReadCode_SEH(ret - 5, b, sizeof(b)) || b[0] != 0xE8) return false;
        std::int32_t rel = 0;
        std::memcpy(&rel, b + 1, sizeof(rel));
        return ret + static_cast<std::intptr_t>(rel) == fn;
    }

    static bool Prepare(const char* what, const GetterSites& sites, HookState& h)
    {
        const auto fn = reinterpret_cast<std::uintptr_t>(ResolveGameAddress(sites.getter));
        if (!fn || !GetterMatches(fn))
        {
            LogError("OutfitSprint: %s signature mismatch, sprint sound and sparks off", what);
            return false;
        }
        h.returnCount = 0;
        for (std::size_t i = 0; i < sites.returnCount && i < kMaxReturnSites; ++i)
        {
            const auto ret = reinterpret_cast<std::uintptr_t>(
                ResolveGameAddress(sites.returns[i].addr));
            if (!ret || !CallsGetter(ret, fn))
            {
                LogError("OutfitSprint: %s call site 0x%llX mismatch, sprint sound and sparks off",
                    what, static_cast<unsigned long long>(sites.returns[i].addr));
                return false;
            }
            h.returns[h.returnCount++] = { ret, sites.returns[i].need };
        }
        h.target = fn;
        return true;
    }

    static std::uintptr_t InstallChecked(const char* what, std::uintptr_t addr, const std::uint8_t* code,
                                         std::size_t n, void* detour, void** orig)
    {
        const auto fn = reinterpret_cast<std::uintptr_t>(ResolveGameAddress(addr));
        if (!fn || !CodeIs(fn, code, n))
        {
            LogError("OutfitSprint: %s is not the known code, %s off", what, what);
            return 0;
        }
        if (!CreateAndEnableHook(reinterpret_cast<void*>(fn), detour, orig))
        {
            LogError("OutfitSprint: %s hook failed, %s off", what, what);
            return 0;
        }
        return fn;
    }

    static void Remove(HookState& h)
    {
        if (!h.installed) return;
        DisableAndRemoveHook(reinterpret_cast<void*>(h.target));
        h.installed = false;
        h.target    = 0;
    }
}

namespace outfit
{
    bool Install_PlayerInfoGetPartsType_Hooks()
    {
        if (g_Local.installed || g_Index.installed) return true;

        if (!IsEn154Family(gGameBuild))
        {
            LogError("OutfitSprint: no addresses for build %s, sprint sound, sparks, raidenRun and dashAttack off",
                GetGameBuildName(gGameBuild));
            return false;
        }

        if (!g_DashMoveTarget)
            g_DashMoveTarget = InstallChecked("raidenRun", kDashMove_En154, kDashMoveCode, sizeof(kDashMoveCode),
                reinterpret_cast<void*>(&hkDashMove), reinterpret_cast<void**>(&g_OrigDashMove));
        if (!g_DashAttackTarget)
            g_DashAttackTarget = InstallChecked("dashAttack", kDashAttack_En154, kDashAttackCode,
                sizeof(kDashAttackCode), reinterpret_cast<void*>(&hkDashAttack),
                reinterpret_cast<void**>(&g_OrigDashAttack));

        if (!Prepare("GetPartsTypeLocal", kLocal_En154, g_Local)
            || !Prepare("GetPartsType", kIndex_En154, g_Index))
            return false;

        g_Local.installed = CreateAndEnableHook(
            reinterpret_cast<void*>(g_Local.target),
            reinterpret_cast<void*>(&hkGetPartsTypeLocal),
            reinterpret_cast<void**>(&g_OrigLocal));
        g_Index.installed = CreateAndEnableHook(
            reinterpret_cast<void*>(g_Index.target),
            reinterpret_cast<void*>(&hkGetPartsType),
            reinterpret_cast<void**>(&g_OrigIndex));

        if (!g_Local.installed || !g_Index.installed)
        {
            LogError("OutfitSprint: GetPartsType hook failed, sprint sound and sparks off");
            Remove(g_Index);
            Remove(g_Local);
            g_OrigIndex = nullptr;
            g_OrigLocal = nullptr;
            return false;
        }
        return true;
    }

    void Uninstall_PlayerInfoGetPartsType_Hooks()
    {
        Remove(g_Index);
        Remove(g_Local);
        g_OrigIndex = nullptr;
        g_OrigLocal = nullptr;
        if (g_DashAttackTarget)
            DisableAndRemoveHook(reinterpret_cast<void*>(g_DashAttackTarget));
        if (g_DashMoveTarget)
            DisableAndRemoveHook(reinterpret_cast<void*>(g_DashMoveTarget));
        g_DashAttackTarget = 0;
        g_DashMoveTarget = 0;
        g_OrigDashAttack = nullptr;
        g_OrigDashMove = nullptr;
    }
}
