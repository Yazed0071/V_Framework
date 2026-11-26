#include <pch.h>
#include "IdroidVoiceLines.h"

#include <Windows.h>
#include <cstddef>
#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"
#include "Player2Impl_CallVoice.h"
#include "../ui/ResourceManagementCallbackImpl_UpdateState.h"

namespace
{
    using AssignSectionCheckBoxStaff_t = std::uint32_t(__fastcall*)(void* staffController, std::uint8_t section);
    using FireCheckedStaff_t           = std::uint64_t(__fastcall*)(void* staffList);
    using NuclearRequest_t             = std::uint64_t(__fastcall*)(void* synchronize, std::uint32_t count);
    using GetQuarkSystemTable_t        = std::uint8_t*(__fastcall*)();

    const std::uint8_t kAssignSectionEntry[] = { 0x40, 0x53, 0x55, 0x57, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x08,
                                                 0x0F, 0xB7, 0xB9, 0x6C, 0x9E, 0x00, 0x00 };          // movzx edi,[rcx+0x9e6c] = staff count
    const std::uint8_t kFireCheckedEntry[]   = { 0x40, 0x53, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x28,
                                                 0x48, 0x8B, 0x41, 0x50 };                             // mov rax,[rcx+0x50] = StaffController
    const std::uint8_t kNuclearEntry[]       = { 0x40, 0x57, 0x48, 0x83, 0xEC, 0x40, 0x48, 0xC7, 0x44, 0x24, 0x30,
                                                 0xFE, 0xFF, 0xFF, 0xFF };

    constexpr std::size_t   kQuarkVarsRoot          = 0x98;
    constexpr std::size_t   kVarsBlock              = 0x10;
    constexpr std::size_t   kNuclearDeveloping      = 0x12B24;       // vars.mbmIsNuclearDeveloping
    constexpr std::size_t   kNuclearDevelopMaxTime  = 0x12B2C;       // vars.mbmNuclearDevelopMaxTime
    constexpr std::uint32_t kNoDevelopTime          = 0xFFFFFFFFu;   // the server keeps the time: online order, status sync
    constexpr std::size_t   kResourceMenuState      = 0x254;
    constexpr std::uint8_t  kOnlineOrderAccepted    = 0x18;          // the server took the order; its network-error branch goes to 0x17

    constexpr std::uint8_t  kSectionSeparation = 10;            // SECTION_SEPARATION, the quarantine
    constexpr std::uint32_t kFired             = 0x252DF3ABu;   // "You're fired."
    constexpr std::uint32_t kSeeYouAround      = 0x252DF3A8u;   // "See you around."
    constexpr std::uint32_t kQuarantine        = 0x10B15E3Cu;   // "We need to quarantine you."
    constexpr std::uint32_t kReportQuarantine  = 0x10B15E3Fu;   // "Report to quarantine..."
    constexpr std::uint32_t kDoIt              = 0xD2F67496u;   // "Do it."
    constexpr std::uint32_t kCommenceNukes     = 0xD2F67495u;   // "Commence development of nuclear weapons."
    constexpr std::uint32_t kDecommissionNukes = 0x4C457C67u;   // "Decommission the nukes."
    constexpr std::uint32_t kDecommissionThem  = 0x4C457C64u;   // "Decommission them."
    constexpr std::uint32_t kPaybackTime       = 0x43FCEA6Du;   // "Payback time."
    constexpr std::uint32_t kNotGettingAway    = 0x43FCEA6Eu;   // "They're not getting away with that."

    AssignSectionCheckBoxStaff_t g_OrigAssignSection         = nullptr;
    FireCheckedStaff_t           g_OrigFireChecked           = nullptr;
    NuclearRequest_t             g_OrigDestructNuclear       = nullptr;
    NuclearRequest_t             g_OrigDestructOnlineNuclear = nullptr;
    void*                        g_AssignSectionTarget       = nullptr;
    void*                        g_FireCheckedTarget         = nullptr;
    bool                         g_HoldsResourceStep         = false;
    void*                        g_DestructNuclearTarget     = nullptr;
    void*                        g_DestructOnlineTarget      = nullptr;

    struct NuclearDevelop
    {
        std::uint8_t  developing = 0;
        std::uint32_t maxTime    = 0;
    };

    struct BuildSites
    {
        std::uintptr_t assignSection;     // StaffControllerImpl::AssignSectionCheckBoxStaff
        std::uintptr_t fireChecked;       // staff list dismiss
        std::uintptr_t destructNuclear;   // ServerSynchronize::DestructNuclear
        std::uintptr_t destructOnline;    // FOB nuclear disposal request
    };


    const BuildSites* SitesFor()
    {
        static const BuildSites sites = { gAddr.StaffControllerImpl_AssignSectionCheckBoxStaff, gAddr.StaffList_FireCheckedStaff, gAddr.ServerSynchronize_DestructNuclear, gAddr.Fob_DestructNuclearRequest };
        return &sites;
    }

    std::uint32_t __fastcall hkAssignSectionCheckBoxStaff(void* staffController, std::uint8_t section)
    {
        const std::uint32_t moved = g_OrigAssignSection(staffController, section);
        if (moved != 0 && section == kSectionSeparation)
        {
            SayEitherMinorPlayerLine(kQuarantine, kReportQuarantine);
        }
        return moved;
    }

    std::uint64_t __fastcall hkFireCheckedStaff(void* staffList)
    {
        const std::uint64_t fired = g_OrigFireChecked(staffList);
        if (static_cast<std::uint8_t>(fired) != 0)
        {
            SayEitherMinorPlayerLine(kFired, kSeeYouAround);
        }
        return fired;
    }

    bool ReadNuclearDevelop_SEH(NuclearDevelop& out)
    {
        const auto getQuark = reinterpret_cast<GetQuarkSystemTable_t>(ResolveGameAddress(gAddr.GetQuarkSystemTable));
        if (!getQuark)
            return false;
        __try
        {
            const std::uint8_t* quark = getQuark();
            const std::uint8_t* root  = quark ? *reinterpret_cast<std::uint8_t* const*>(quark + kQuarkVarsRoot) : nullptr;
            const std::uint8_t* vars  = root ? *reinterpret_cast<std::uint8_t* const*>(root + kVarsBlock) : nullptr;
            if (!vars)
                return false;
            out.developing = vars[kNuclearDeveloping];
            out.maxTime    = *reinterpret_cast<const std::uint32_t*>(vars + kNuclearDevelopMaxTime);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    std::uint8_t ReadMenuState_SEH(const void* menu)
    {
        __try
        {
            return static_cast<const std::uint8_t*>(menu)[kResourceMenuState];
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    std::uint64_t __fastcall hkDestructNuclear(void* synchronize, std::uint32_t count)
    {
        const std::uint64_t sent = g_OrigDestructNuclear(synchronize, count);
        if (static_cast<std::uint8_t>(sent) != 0)
        {
            SayEitherMinorPlayerLine(kDecommissionNukes, kDecommissionThem);
        }
        return sent;
    }

    std::uint64_t __fastcall hkDestructOnlineNuclear(void* synchronize, std::uint32_t count)
    {
        const std::uint64_t sent = g_OrigDestructOnlineNuclear(synchronize, count);
        if (static_cast<std::uint8_t>(sent) != 0)
        {
            SayEitherMinorPlayerLine(kDecommissionNukes, kDecommissionThem);
        }
        return sent;
    }

    void* InstallOne(const char* what, std::uintptr_t address, const std::uint8_t* entry, std::size_t entrySize,
                     void* detour, void** original)
    {
        void* target = ResolveGameAddress(address);
        const HookEntry state = CheckHookEntry(target, entry, entrySize);
        if (state == HookEntry::Unknown)
        {
            LogError("IdroidVoice: %s entry mismatch on %s, its line is off", what, GetGameBuildName(gGameBuild));
            return nullptr;
        }
        if (state == HookEntry::ForeignHook)
            LogHookedOnTop(what, target);
        if (!CreateAndEnableHook(target, detour, original))
        {
            LogError("IdroidVoice: %s hook failed at %p, its line is off", what, target);
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

bool Install_IdroidVoiceLines_Hooks()
{
    const BuildSites* sites = SitesFor();
    if (!sites)
    {
        LogWarn("IdroidVoice: addresses unknown for %s, no iDroid voice lines", GetGameBuildName(gGameBuild));
        return true;
    }

    if (!g_AssignSectionTarget)
        g_AssignSectionTarget = InstallOne("StaffControllerImpl::AssignSectionCheckBoxStaff", sites->assignSection, kAssignSectionEntry,
                                           sizeof(kAssignSectionEntry), reinterpret_cast<void*>(&hkAssignSectionCheckBoxStaff),
                                           reinterpret_cast<void**>(&g_OrigAssignSection));
    if (!g_FireCheckedTarget)
        g_FireCheckedTarget = InstallOne("staff list dismiss", sites->fireChecked, kFireCheckedEntry, sizeof(kFireCheckedEntry),
                                         reinterpret_cast<void*>(&hkFireCheckedStaff), reinterpret_cast<void**>(&g_OrigFireChecked));
    if (!g_HoldsResourceStep)
    {
        g_HoldsResourceStep = Acquire_ResourceManagementCallbackImpl_UpdateState_Hook();
        if (!g_HoldsResourceStep)
            LogError("IdroidVoice: iDroid Resources menu step hook failed, its line is off");
    }
    if (!g_DestructNuclearTarget)
        g_DestructNuclearTarget = InstallOne("ServerSynchronize::DestructNuclear", sites->destructNuclear, kNuclearEntry, sizeof(kNuclearEntry),
                                             reinterpret_cast<void*>(&hkDestructNuclear), reinterpret_cast<void**>(&g_OrigDestructNuclear));
    if (!g_DestructOnlineTarget)
        g_DestructOnlineTarget = InstallOne("FOB nuclear disposal request", sites->destructOnline, kNuclearEntry, sizeof(kNuclearEntry),
                                            reinterpret_cast<void*>(&hkDestructOnlineNuclear),
                                            reinterpret_cast<void**>(&g_OrigDestructOnlineNuclear));
    return true;
}

void Uninstall_IdroidVoiceLines_Hooks()
{
    RemoveOne(g_DestructOnlineTarget);
    RemoveOne(g_DestructNuclearTarget);
    if (g_HoldsResourceStep)
        Release_ResourceManagementCallbackImpl_UpdateState_Hook();
    g_HoldsResourceStep = false;
    RemoveOne(g_FireCheckedTarget);
    RemoveOne(g_AssignSectionTarget);
}

IdroidVoiceResourceStep IdroidVoice_BeforeResourceStep()
{
    IdroidVoiceResourceStep step;
    NuclearDevelop before;
    if (g_HoldsResourceStep && ReadNuclearDevelop_SEH(before))
    {
        step.known = true;
        step.developing = before.developing;
    }
    return step;
}

// the order happens in this iDroid Resources menu step: offline it starts a local timer, online the server keeps the time
// and the step moves to kOnlineOrderAccepted. ServerSynchronize::CreateNuclear runs for the finished weapon
void IdroidVoice_AfterResourceStep(void* menu, const IdroidVoiceResourceStep& before)
{
    NuclearDevelop after;
    if (before.known && !before.developing && ReadNuclearDevelop_SEH(after) && after.developing
        && (after.maxTime != kNoDevelopTime || ReadMenuState_SEH(menu) == kOnlineOrderAccepted))
    {
        SayEitherMinorPlayerLine(kDoIt, kCommenceNukes);
    }
}

bool SayRevengeLine()
{
    return SayEitherPlayerLine(kPaybackTime, kNotGettingAway);
}
