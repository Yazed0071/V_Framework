#include <pch.h>
#include "InvestigateSituationOperatorImpl_GetSituationWithRequest.h"

#include <Windows.h>
#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "log.h"

// Every investigator on one point joins the same InvestigateSituation (the role token picks it), and a situation seats a
// leader and three followers: JoinSituation turns a fifth away with role 0 and he idles in the investigate node. Vanilla
// never sends more than four. Here a full squad is passed over, so the next one opens a squad of its own on the same point.

namespace
{
    using GetSituationWithRequest_t = std::uint32_t(__fastcall*)(void* op, const std::uint8_t* infos, const std::uint8_t* situations,
                                                                 std::uint32_t count, const std::uint8_t* request);

    constexpr std::uint16_t kInvestigateSituation = 0x17;
    constexpr std::size_t   kInfoSize             = 6;      // u16 type first
    constexpr std::size_t   kSituationSize        = 0x80;
    constexpr std::size_t   kSituationToken       = 0x12;   // the role payload's point token, request +0x10
    constexpr std::size_t   kRequestToken         = 0x10;
    constexpr std::size_t   kSeats[]              = { 0x28, 0x38, 0x48, 0x58 };   // leader, then followers: member id, 0xFFFF empty
    constexpr std::uint16_t kEmptySeat            = 0xFFFF;

    const std::uint8_t kPrologue[] = { 0x33, 0xC9, 0x45, 0x85, 0xC9, 0x74, 0x29, 0x4C, 0x8B, 0x54, 0x24, 0x28, 0x49, 0x83, 0xC0, 0x12,
                                       0x66, 0x83, 0x3A, 0x17 };

    GetSituationWithRequest_t g_Orig   = nullptr;
    void*                     g_Target = nullptr;

    std::uintptr_t AddressForBuild()
    {
        return gAddr.InvestigateSituationOperatorImpl_GetSituationWithRequest;
    }

    bool SquadFullSEH(const std::uint8_t* situation)
    {
        __try
        {
            for (const std::size_t seat : kSeats)
            {
                if (*reinterpret_cast<const std::uint16_t*>(situation + seat) == kEmptySeat)
                    return false;
            }
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    std::uint32_t NextSquadSEH(const std::uint8_t* infos, const std::uint8_t* situations, std::uint32_t count, const std::uint8_t* request,
                               std::uint32_t from)   // the original's own match, from a later index
    {
        __try
        {
            const std::uint16_t token = *reinterpret_cast<const std::uint16_t*>(request + kRequestToken);
            for (std::uint32_t i = from; i < count; ++i)
            {
                if (*reinterpret_cast<const std::uint16_t*>(infos + i * kInfoSize) == kInvestigateSituation
                    && *reinterpret_cast<const std::uint16_t*>(situations + i * kSituationSize + kSituationToken) == token)
                    return i;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
        return count;
    }

    std::uint32_t __fastcall hk_GetSituationWithRequest(void* op, const std::uint8_t* infos, const std::uint8_t* situations, std::uint32_t count,
                                                        const std::uint8_t* request)
    {
        MISSION_GUARD_ORIGINAL_RET(g_Orig, op, infos, situations, count, request);
        std::uint32_t found = g_Orig(op, infos, situations, count, request);
        while (found < count && SquadFullSEH(situations + found * kSituationSize))
            found = NextSquadSEH(infos, situations, count, request, found + 1);
        return found;   // count: DoRequestSituationRole opens a new situation and he leads it
    }
}

bool Install_InvestigateSituationOperatorImpl_GetSituationWithRequest_Hook()
{
    if (g_Target)
        return true;
    const std::uintptr_t address = AddressForBuild();
    if (!address)
        return true;
    auto* target = static_cast<std::uint8_t*>(ResolveGameAddress(address));
    const HookEntry entry = CheckHookEntry(target, kPrologue, sizeof(kPrologue));
    if (entry == HookEntry::Unknown)
    {
        LogError("InvestigationSquads: InvestigateSituationOperatorImpl::GetSituationWithRequest does not match %s, investigations stay at four soldiers",
            GetGameBuildName(gGameBuild));
        return false;
    }
    if (entry == HookEntry::ForeignHook)
        LogHookedOnTop("InvestigationSquads: InvestigateSituationOperatorImpl::GetSituationWithRequest", target);
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hk_GetSituationWithRequest), reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("InvestigationSquads: InvestigateSituationOperatorImpl::GetSituationWithRequest hook failed, investigations stay at four soldiers");
        return false;
    }
    g_Target = target;
    return true;
}

bool Uninstall_InvestigateSituationOperatorImpl_GetSituationWithRequest_Hook()
{
    if (g_Target)
    {
        DisableAndRemoveHook(g_Target);
        g_Target = nullptr;
        g_Orig = nullptr;
    }
    return true;
}
