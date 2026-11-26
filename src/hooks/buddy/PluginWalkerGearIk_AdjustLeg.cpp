#include <pch.h>

#include "PluginWalkerGearIk_AdjustLeg.h"

#include <atomic>
#include <cstdint>

#include "AddressSet.h"
#include "BattleGearBuddy.h"
#include "HookUtils.h"
#include "log.h"

#ifdef _DEBUG
namespace
{
    using AdjustLeg_t = void(__fastcall*)(void* plugin, void* work, bool mirror);

    constexpr std::size_t kWorkRigDef    = 0x40;
    constexpr std::size_t kRigDefUnitNum = 0x0C;   // frig header: unit count

    const std::uint8_t kAdjustLegEntry[] = {
        0x40, 0x55, 0x56, 0x57, 0x41, 0x56, 0x41, 0x57, 0x48, 0x81, 0xEC, 0x90, 0x03, 0x00, 0x00, 0x48,
        0xC7, 0x44, 0x24, 0x40, 0xFE, 0xFF, 0xFF, 0xFF
    };

    AdjustLeg_t       g_Orig = nullptr;
    void*             g_Target = nullptr;
    std::atomic<bool> g_SkipLogged{ false };

    std::uint32_t RigUnitCountSEH(const void* work)
    {
        __try
        {
            const auto* rigDef = *reinterpret_cast<const std::uint8_t* const*>(
                static_cast<const std::uint8_t*>(work) + kWorkRigDef);
            return rigDef ? *reinterpret_cast<const std::uint32_t*>(rigDef + kRigDefUnitNum) : 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    void __fastcall hkAdjustLeg(void* plugin, void* work, bool mirror)
    {
        if (!battleGear::IsBattleGearOut())
        {
            g_Orig(plugin, work, mirror);
            return;
        }
        if (!g_SkipLogged.exchange(true))
            LogDebug("BattleGear: walker leg IK skipped while Battle Gear is out (rig units %u)", RigUnitCountSEH(work));
    }
}
#endif

bool Install_PluginWalkerGearIk_AdjustLeg_Hook()
{
#ifndef _DEBUG
    return true;
#else
    if (g_Target)
        return true;
    void* target = ResolveGameAddress(gAddr.PluginWalkerGearIk_AdjustLeg);
    if (!target)
        return false;
    const HookEntry state = CheckHookEntry(target, kAdjustLegEntry, sizeof(kAdjustLegEntry));
    if (state == HookEntry::Unknown)
    {
        char desc[160] = {};
        DescribeFunctionEntry(target, desc, sizeof(desc));
        LogWarn("BattleGear: walker leg IK code differs on this build (%s), IK test off", desc);
        return false;
    }
    if (state == HookEntry::ForeignHook)
        LogHookedOnTop("BattleGear: AdjustLeg", target);
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hkAdjustLeg), reinterpret_cast<void**>(&g_Orig)))
    {
        LogWarn("BattleGear: walker leg IK hook failed, IK test off");
        return false;
    }
    g_Target = target;
    return true;
#endif
}

void Uninstall_PluginWalkerGearIk_AdjustLeg_Hook()
{
#ifdef _DEBUG
    if (!g_Target)
        return;
    DisableAndRemoveHook(g_Target);
    g_Target = nullptr;
    g_Orig = nullptr;
#endif
}
