#include <pch.h>
#include "HostageRideVehicleActionImpl_State_GetInAction.h"

#include <Windows.h>
#include <cstdint>
#include <cstdio>

#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "SoldierCombatVoice.h"
#include "log.h"
#include "../fox/gm/GameObjectPosition.h"

namespace
{
    using State_t = void(__fastcall*)(void* self, std::uint32_t actionIndex, std::uint32_t proc, void* data);

    constexpr std::uint32_t kProcEnter        = 1;
    constexpr std::size_t   kWorks            = 0xB0;   // 0x28 per prisoner in this action, from the index at +0xC0
    constexpr std::size_t   kFirstWorkIndex   = 0xC0;
    constexpr std::size_t   kWorkSize         = 0x28;
    constexpr std::size_t   kWorkVehicle      = 0x18;   // the vehicle he gets into or out of
    constexpr std::uint16_t kVehicleType      = 0x32;   // TppVehicle2
    constexpr std::uint16_t kTypeShift        = 9;
    constexpr std::uint32_t kGetInLine        = 0x35751797u;   // EVSP040 "Get in."
    constexpr std::uint32_t kGetOutLine       = 0x34751624u;   // EVSP050 "Get out."

    constexpr std::uint8_t kGetInEntry[] = {
        0x41, 0x83, 0xF8, 0x05, 0x0F, 0x87, 0xFB, 0x01, 0x00, 0x00, 0x53, 0x56, 0x57, 0x48, 0x83, 0xEC,
        0x70, 0x8B, 0xC2, 0x48, 0x8B, 0xF9, 0x8B, 0xF2, 0x2B, 0x81, 0xC0, 0x00, 0x00, 0x00, 0x4C, 0x8D,
    };
    constexpr std::uint8_t kGetOutEntry[] = {
        0x41, 0x83, 0xF8, 0x05, 0x0F, 0x87, 0x08, 0x01, 0x00, 0x00, 0x48, 0x89, 0x5C, 0x24, 0x18, 0x48,
        0x89, 0x74, 0x24, 0x20, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x8B, 0xC2, 0x48, 0x8B, 0xF9, 0x8B, 0xF2,
    };

    State_t g_OrigGetIn    = nullptr;
    State_t g_OrigGetOut   = nullptr;
    void*   g_GetInTarget  = nullptr;
    void*   g_GetOutTarget = nullptr;

    std::uint16_t VehicleOf(const void* self, std::uint32_t actionIndex)
    {
        __try
        {
            const auto* action = static_cast<const std::uint8_t*>(self);
            const std::uint8_t* works = *reinterpret_cast<const std::uint8_t* const*>(action + kWorks);
            const std::uint32_t first = *reinterpret_cast<const std::uint32_t*>(action + kFirstWorkIndex);
            return *reinterpret_cast<const std::uint16_t*>(works + (actionIndex - first) * kWorkSize + kWorkVehicle);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    void OrderPrisoner(const void* self, std::uint32_t actionIndex, std::uint32_t line)
    {
        if (MissionCodeGuard::ShouldBypassHooks())
            return;
        const std::uint16_t vehicle = VehicleOf(self, actionIndex);
        if ((vehicle >> kTypeShift) != kVehicleType)
            return;
        float position[3] = {};
        if (GameObjectPosition::Read(vehicle, position))
            SoldierCombatVoice_SayPrisonerOrder(position, line);
    }

    void __fastcall hk_StateGetIn(void* self, std::uint32_t actionIndex, std::uint32_t proc, void* data)
    {
        g_OrigGetIn(self, actionIndex, proc, data);
        if (proc == kProcEnter)
            OrderPrisoner(self, actionIndex, kGetInLine);
    }

    void __fastcall hk_StateGetOut(void* self, std::uint32_t actionIndex, std::uint32_t proc, void* data)
    {
        g_OrigGetOut(self, actionIndex, proc, data);
        if (proc == kProcEnter)
            OrderPrisoner(self, actionIndex, kGetOutLine);
    }

    bool HookState(std::uintptr_t address, const std::uint8_t* entry, std::size_t entrySize, void* detour, void** original,
                   void** target, const char* what)
    {
        void* fn = ResolveGameAddress(address);
        const HookEntry state = CheckHookEntry(fn, entry, entrySize);
        if (state == HookEntry::Unknown)
        {
            char bytes[160] = {};
            DescribeFunctionEntry(fn, bytes, sizeof(bytes));
            LogError("PrisonerOrders: the prisoner's %s state differs on %s (%s), soldiers stay quiet there", what,
                     GetGameBuildName(gGameBuild), bytes);
            return false;
        }
        if (state == HookEntry::ForeignHook)
        {
            char label[96] = {};
            std::snprintf(label, sizeof(label), "PrisonerOrders: the prisoner's %s state", what);
            LogHookedOnTop(label, fn);
        }
        if (!CreateAndEnableHook(fn, detour, original))
        {
            LogError("PrisonerOrders: the prisoner's %s state hook failed at %p, soldiers stay quiet there", what, fn);
            *original = nullptr;
            return false;
        }
        *target = fn;
        return true;
    }
}

bool Install_HostageRideVehicleActionImpl_State_GetInAction_Hooks()
{
    if (!gAddr.HostageRideVehicleActionImpl_State_GetInAction || !gAddr.HostageRideVehicleActionImpl_State_GetOutAction)
    {
        LogWarn("PrisonerOrders: prisoner vehicle states unknown for %s, soldiers don't order prisoners in and out", GetGameBuildName(gGameBuild));
        return true;
    }
    bool ok = true;
    if (!g_GetInTarget)
        ok &= HookState(gAddr.HostageRideVehicleActionImpl_State_GetInAction, kGetInEntry, sizeof(kGetInEntry),
                        reinterpret_cast<void*>(&hk_StateGetIn), reinterpret_cast<void**>(&g_OrigGetIn), &g_GetInTarget, "get-in");
    if (!g_GetOutTarget)
        ok &= HookState(gAddr.HostageRideVehicleActionImpl_State_GetOutAction, kGetOutEntry, sizeof(kGetOutEntry),
                        reinterpret_cast<void*>(&hk_StateGetOut), reinterpret_cast<void**>(&g_OrigGetOut), &g_GetOutTarget, "get-out");
    return ok;
}

void Uninstall_HostageRideVehicleActionImpl_State_GetInAction_Hooks()
{
    if (g_GetInTarget)
        DisableAndRemoveHook(g_GetInTarget);
    if (g_GetOutTarget)
        DisableAndRemoveHook(g_GetOutTarget);
    g_GetInTarget  = nullptr;
    g_GetOutTarget = nullptr;
    g_OrigGetIn    = nullptr;
    g_OrigGetOut   = nullptr;
}
