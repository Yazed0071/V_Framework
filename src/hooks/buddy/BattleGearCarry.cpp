#include <pch.h>

#include "BattleGearCarry.h"

#include <Windows.h>
#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "BattleGearGunMount.h"
#include "BattleGearWeapons.h"
#include "BuddyCodeCave.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    using UpdateAttachments_t = void(__fastcall*)(std::uint8_t* action, std::uint32_t walker);
    using GetComm_t           = std::uint8_t*(__fastcall*)(void* walkers, std::uint16_t walker);

    constexpr std::size_t   kActionWalkers = 0xC0;
    constexpr std::size_t   kGetCommSlot   = 0xF8 / sizeof(void*);
    constexpr std::size_t   kCommActions   = 0x560;
    constexpr std::size_t   kCommHoldPoint = 0xC0;   // the matrix the carried body follows
    constexpr std::uint32_t kInteracting   = 0x4000000;
    constexpr std::size_t   kCommArm       = 0x568;
    constexpr std::uint32_t kCarryAllowed  = 0x8000;
    constexpr std::size_t   kHoldPointSize = 16 * sizeof(float);

    // test al, al / je / or dword [rbx+0x568], 0x8000: the walker's arm can carry
    constexpr std::uint8_t kCarryAllowedSite[] = { 0x84, 0xC0, 0x74, 0x0C, 0x81, 0x8B, 0x68, 0x05, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00 };
    constexpr std::size_t  kCarryNotAllowed    = 0x10;   // and dword [rbx+0x568], ~0x8000
    constexpr std::size_t  kCarryAllowedDone   = 0x1A;
    // movaps [rbp-0x60], xmm6 (the search box centre) / mov word [rbp-0x1e], ax / mov word [rbp-0xe], di
    constexpr std::uint8_t kSearchCentreSite[] = { 0x0F, 0x29, 0x75, 0xA0, 0x66, 0x89, 0x45, 0xE2, 0x66, 0x89, 0x7D, 0xF2 };
    // call CheckEnableResult: the body must lie ahead of the rider with a clear line to it
    constexpr std::uint8_t kCheckResultSite[]  = { 0xE8, 0x12, 0xF7, 0xFF, 0xFF };
    // mov edx, [rcx+rax+0x10] / shr edx, 8 / test dl, 1 (rdx = the rider's walker comm): no carry prompt while HOLD is held, and the
    // Battle Gear only backs up while zooming
    constexpr std::uint8_t kAimGateSite[]      = { 0x8B, 0x54, 0x01, 0x10, 0xC1, 0xEA, 0x08, 0xF6, 0xC2, 0x01 };
    const std::uint8_t kUpdateAttachmentsEntry[] = { 0x4C, 0x8B, 0xDC, 0x55, 0x53, 0x57, 0x41, 0x56, 0x41, 0x57 };

    BuddyCodeCave       g_CarryAllowed{ "Battle Gear carry allowed" };
    BuddyCodeCave       g_SearchCentre{ "Battle Gear carry search box" };
    BuddyCodeCave       g_CheckResult{ "Battle Gear carry check" };
    BuddyCodeCave       g_AimGate{ "Battle Gear carry while zooming" };
    UpdateAttachments_t g_OrigUpdateAttachments = nullptr;
    void*               g_UpdateAttachmentsTarget = nullptr;
#ifdef _DEBUG
    long                g_PromptChecks = 0;   // TEMP probe: the caves count them with lock inc
    long                g_BoxSearches = 0;
    long                g_BodiesFound = 0;
    ULONGLONG           g_NextCarryLog = 0;

    void EmitCount(CaveCode& c, long* counter)   // rax is free at every counting point
    {
        c.Emit({ 0x48, 0xB8 });                                              // mov rax, &counter
        c.Imm64(reinterpret_cast<std::uint64_t>(counter));
        c.Emit({ 0xF0, 0xFF, 0x00 });                                        // lock inc dword [rax]
    }
#endif

    std::uintptr_t SiteOf(std::uintptr_t siteAbs, const char* what)
    {
        const auto at = reinterpret_cast<std::uintptr_t>(siteAbs ? ResolveGameAddress(siteAbs) : nullptr);
        if (!at)
            LogError("BattleGear: %s address missing on this build", what);
        return at;
    }

    bool InstallCarryAllowed()   // the Battle Gear's tail carries outside fire posture
    {
        const std::uintptr_t siteAbs = gAddr.WalkerGear_UpdateCarryAllowedFlag_Site;
        const std::uintptr_t base = SiteOf(siteAbs, g_CarryAllowed.name);
        if (!base)
            return false;
        CaveCode c;
        c.Emit({ 0x48, 0xB9 });                                              // mov rcx, &comm slot
        c.Imm64(reinterpret_cast<std::uint64_t>(battleGearWeapons::BattleGearCommSlot()));
        c.Emit({ 0x48, 0x3B, 0x19 });                                        // cmp rbx, [rcx]
        const std::size_t other = c.Jcc8(0x75);                              // jne
        c.Emit({ 0x48, 0xB9 });                                              // mov rcx, &fire mode
        c.Imm64(reinterpret_cast<std::uint64_t>(battleGearWeapons::FireModeSlot()));
        c.Emit({ 0x80, 0x39, 0x00 });                                        // cmp byte [rcx], 0
        c.Emit({ 0x0F, 0x94, 0xC0 });                                        // sete al
        c.Bind8(other);
        c.Emit({ 0x84, 0xC0 });                                              // test al, al
        const std::size_t notAllowed = c.Jcc8(0x74);                         // je
        c.Emit({ 0x81, 0x8B, 0x68, 0x05, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00 });   // or dword [rbx+0x568], 0x8000
        c.JmpAbs(base + kCarryAllowedDone);
        c.Bind8(notAllowed);
        c.JmpAbs(base + kCarryNotAllowed);
        return InstallBuddyCodeCave(g_CarryAllowed, siteAbs, kCarryAllowedSite, sizeof(kCarryAllowedSite), c);
    }

    bool InstallSearchCentre()   // riding the Battle Gear, the box the prompt searches is the one round its hull
    {
        const std::uintptr_t siteAbs = gAddr.RideWalkerGearActionImpl_ExecActionButtonCarry_SearchCentre;
        const std::uintptr_t base = SiteOf(siteAbs, g_SearchCentre.name);
        if (!base)
            return false;
        CaveCode c;
        c.Emit({ 0x66, 0x89, 0x45, 0xE2 });                                  // mov word [rbp-0x1e], ax
        c.Emit({ 0x66, 0x89, 0x7D, 0xF2 });                                  // mov word [rbp-0xe], di
        c.Emit({ 0x48, 0xB8 });                                              // mov rax, &ridden
        c.Imm64(reinterpret_cast<std::uint64_t>(battleGearWeapons::RiddenSlot()));
        c.Emit({ 0x80, 0x38, 0x00 });                                        // cmp byte [rax], 0
        const std::size_t notRidden = c.Jcc8(0x74);                          // je
#ifdef _DEBUG
        EmitCount(c, &g_BoxSearches);
#endif
        c.Emit({ 0x48, 0xB8 });                                              // mov rax, &carry zone
        c.Imm64(reinterpret_cast<std::uint64_t>(battleGearWeapons::CarryZoneSlot()));
        c.Emit({ 0x0F, 0x28, 0x30 });                                        // movaps xmm6, [rax]
        c.Emit({ 0x0F, 0x28, 0x40, 0x10 });                                  // movaps xmm0, [rax+0x10]
        c.Emit({ 0x0F, 0x29, 0x45, 0xB0 });                                  // movaps [rbp-0x50], xmm0 (the half sizes)
        c.Bind8(notRidden);
        c.Emit({ 0x0F, 0x29, 0x75, 0xA0 });                                  // movaps [rbp-0x60], xmm6
        c.JmpAbs(base + sizeof(kSearchCentreSite));
        return InstallBuddyCodeCave(g_SearchCentre, siteAbs, kSearchCentreSite, sizeof(kSearchCentreSite), c);
    }

    // riding the Battle Gear, a body in the box round it needs no line of sight from the rider; where it lies is kept for the tail
    bool InstallCheckResult()
    {
        const std::uintptr_t siteAbs = gAddr.RideWalkerGearActionImpl_ExecActionButtonCarry_CheckResult;
        const std::uintptr_t base = SiteOf(siteAbs, g_CheckResult.name);
        if (!base)
            return false;
        std::int32_t rel = 0;
        std::memcpy(&rel, kCheckResultSite + 1, sizeof(rel));
        const std::uintptr_t checkEnableResult = base + sizeof(kCheckResultSite) + static_cast<std::intptr_t>(rel);
        CaveCode c;
        c.Emit({ 0x48, 0xB8 });                                              // mov rax, &ridden
        c.Imm64(reinterpret_cast<std::uint64_t>(battleGearWeapons::RiddenSlot()));
        c.Emit({ 0x80, 0x38, 0x00 });                                        // cmp byte [rax], 0
        const std::size_t notRidden = c.Jcc8(0x74);                          // je
#ifdef _DEBUG
        EmitCount(c, &g_BodiesFound);
#endif
        c.Emit({ 0x0F, 0x10, 0x03 });                                        // movups xmm0, [rbx] (the found body's position)
        c.Emit({ 0x48, 0xB8 });                                              // mov rax, &carry find
        c.Imm64(reinterpret_cast<std::uint64_t>(battleGearWeapons::CarryFindSlot()));
        c.Emit({ 0x0F, 0x29, 0x00 });                                        // movaps [rax], xmm0
        c.Emit({ 0xC6, 0x40, 0x10, 0x01 });                                  // mov byte [rax+0x10], 1
        c.Emit({ 0xB0, 0x01 });                                              // mov al, 1
        c.JmpAbs(base + sizeof(kCheckResultSite));
        c.Bind8(notRidden);
        c.Emit({ 0x48, 0xB8 });                                              // mov rax, CheckEnableResult
        c.Imm64(checkEnableResult);
        c.Emit({ 0xFF, 0xD0 });                                              // call rax
        c.JmpAbs(base + sizeof(kCheckResultSite));
        return InstallBuddyCodeCave(g_CheckResult, siteAbs, kCheckResultSite, sizeof(kCheckResultSite), c);
    }

    bool InstallAimGate()   // riding the Battle Gear, the carry prompt also shows while zooming
    {
        const std::uintptr_t siteAbs = gAddr.RideWalkerGearActionImpl_ExecActionButtonCarry_AimGate;
        const std::uintptr_t base = SiteOf(siteAbs, g_AimGate.name);
        if (!base)
            return false;
        CaveCode c;
        c.Emit({ 0x49, 0xB8 });                                              // mov r8, &comm slot
        c.Imm64(reinterpret_cast<std::uint64_t>(battleGearWeapons::BattleGearCommSlot()));
        c.Emit({ 0x49, 0x3B, 0x10 });                                        // cmp rdx, [r8]
        const std::size_t other = c.Jcc8(0x75);                              // jne
#ifdef _DEBUG
        EmitCount(c, &g_PromptChecks);
#endif
        c.Emit({ 0x31, 0xD2 });                                              // xor edx, edx
        c.Emit({ 0xF6, 0xC2, 0x01 });                                        // test dl, 1
        c.JmpAbs(base + sizeof(kAimGateSite));
        c.Bind8(other);
        c.bytes.insert(c.bytes.end(), kAimGateSite, kAimGateSite + sizeof(kAimGateSite));   // the original read, shift and test
        c.JmpAbs(base + sizeof(kAimGateSite));
        return InstallBuddyCodeCave(g_AimGate, siteAbs, kAimGateSite, sizeof(kAimGateSite), c);
    }

    std::uint8_t* CommOfSEH(std::uint8_t* action, std::uint32_t walker)
    {
        __try
        {
            void* walkers = *reinterpret_cast<void**>(action + kActionWalkers);
            if (!walkers)
                return nullptr;
            const auto getComm = reinterpret_cast<GetComm_t>((*static_cast<void***>(walkers))[kGetCommSlot]);
            return getComm(walkers, static_cast<std::uint16_t>(walker));
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    void WriteHoldPointSEH(std::uint8_t* comm, const float* matrix)
    {
        __try
        {
            if (*reinterpret_cast<const std::uint32_t*>(comm + kCommActions) & kInteracting)
                std::memcpy(comm + kCommHoldPoint, matrix, kHoldPointSize);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

#ifdef _DEBUG
    bool ReadCarryBitsSEH(const std::uint8_t* comm, std::uint32_t& actions, std::uint32_t& arm)
    {
        __try
        {
            actions = *reinterpret_cast<const std::uint32_t*>(comm + kCommActions);
            arm = *reinterpret_cast<const std::uint32_t*>(comm + kCommArm);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void LogCarryPrompt(const std::uint8_t* comm)   // TEMP probe: which step of the rider's carry prompt the Battle Gear reaches
    {
        g_NextCarryLog = GetTickCount64() + 1000;
        std::uint32_t actions = 0;
        std::uint32_t arm = 0;
        if (!ReadCarryBitsSEH(comm, actions, arm))
            return;
        LogDebug("BattleGear carry: interacting %u, carry allowed %u; %ld prompt checks, %ld box searches, %ld found a body since the last line",
                 (actions & kInteracting) ? 1u : 0u, (arm & kCarryAllowed) ? 1u : 0u, InterlockedExchange(&g_PromptChecks, 0),
                 InterlockedExchange(&g_BoxSearches, 0), InterlockedExchange(&g_BodiesFound, 0));
    }
#endif

    // after the walker placed its parts and set the hold point from its arm: the Battle Gear's tail hand holds the body instead
    void __fastcall hkUpdateAttachments(std::uint8_t* action, std::uint32_t walker)
    {
        g_OrigUpdateAttachments(action, walker);
        battleGearGunMount::AfterPartsPlaced(action);   // a registered main gun's own mount
        if (!battleGearWeapons::OnField())
            return;
        const bool carrying = battleGearWeapons::Carry() != battleGearWeapons::CarryPhase::None;
#ifdef _DEBUG
        const bool logDue = battleGearWeapons::Ridden() && GetTickCount64() >= g_NextCarryLog;
#else
        const bool logDue = false;
#endif
        if (!carrying && !logDue)
            return;
        std::uint8_t* comm = CommOfSEH(action, walker);
        if (!battleGearWeapons::IsBattleGearComm(comm))
            return;
#ifdef _DEBUG
        if (logDue)
            LogCarryPrompt(comm);
#endif
        if (!carrying)
            return;
        float matrix[16];
        if (battleGearWeapons::HoldPoint(matrix))
            WriteHoldPointSEH(comm, matrix);
    }

    bool InstallHoldPointHook()
    {
        void* target = gAddr.WalkerGear_UpdateAttachmentsAndHoldPoint ? ResolveGameAddress(gAddr.WalkerGear_UpdateAttachmentsAndHoldPoint)
                                                                       : nullptr;
        if (!target)
        {
            LogError("BattleGear: walker part-mount update address missing on this build");
            return false;
        }
        const HookEntry state = CheckHookEntry(target, kUpdateAttachmentsEntry, sizeof(kUpdateAttachmentsEntry));
        if (state == HookEntry::Unknown)
        {
            char desc[160] = {};
            DescribeFunctionEntry(target, desc, sizeof(desc));
            LogError("BattleGear: walker part-mount update differs on this build (%s)", desc);
            return false;
        }
        if (state == HookEntry::ForeignHook)
            LogHookedOnTop("BattleGear: walker part-mount update", target);
        if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hkUpdateAttachments), reinterpret_cast<void**>(&g_OrigUpdateAttachments)))
        {
            LogError("BattleGear: walker part-mount update hook failed");
            return false;
        }
        g_UpdateAttachmentsTarget = target;
        return true;
    }
}

bool Install_BattleGearCarry_Patches()
{
    const bool ok = InstallHoldPointHook() && InstallCarryAllowed() && InstallSearchCentre() && InstallCheckResult() && InstallAimGate();
    if (!ok)
    {
        Uninstall_BattleGearCarry_Patches();
        LogError("BattleGear: the tail hand does not carry bodies on this build");
    }
    return ok;
}

void Uninstall_BattleGearCarry_Patches()
{
    UninstallBuddyCodeCave(g_AimGate);
    UninstallBuddyCodeCave(g_CheckResult);
    UninstallBuddyCodeCave(g_SearchCentre);
    UninstallBuddyCodeCave(g_CarryAllowed);
    if (g_UpdateAttachmentsTarget)
    {
        DisableAndRemoveHook(g_UpdateAttachmentsTarget);
        g_UpdateAttachmentsTarget = nullptr;
        g_OrigUpdateAttachments = nullptr;
    }
}
