#include <pch.h>
#include "UiDepend_ActHudChecking.h"

#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "TextIcons.h"
#include "log.h"

namespace
{
    using ActHudChecking_t = void(__fastcall*)(void* uiDepend, void* hudCommonData, void* uiCommonData);

    constexpr std::uint8_t kActHudCheckingEntry[] =
    {
        0x40, 0x53, 0x55, 0x41, 0x54, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x28, 0x4D, 0x8B, 0xE0, 0x4C, 0x8B,
        0xFA, 0x48, 0x8B, 0xE9,
    };

    ActHudChecking_t g_Orig       = nullptr;
    void*            g_HookTarget = nullptr;

    // runs every frame in the UI depend job, where the game itself rewrites its icon table when the control type changes
    void __fastcall hk_ActHudChecking(void* uiDepend, void* hudCommonData, void* uiCommonData)
    {
        if (!MissionCodeGuard::ShouldBypassHooks())
            TextIcons::WritePending();
        g_Orig(uiDepend, hudCommonData, uiCommonData);
    }
}

bool Install_UiDepend_ActHudChecking_Hook()
{
    if (g_HookTarget)
        return true;

    void* target = ResolveGameAddress(gAddr.UiDepend_ActHudChecking);
    const HookEntry entry = CheckHookEntry(target, kActHudCheckingEntry, sizeof(kActHudCheckingEntry));
    if (entry == HookEntry::Unknown)
    {
        char found[160] = {};
        DescribeFunctionEntry(target, found, sizeof(found));
        LogError("TextIcons: UiDepend::ActHudChecking is not the known code on %s (%s), no text icons can be made",
                 GetGameBuildName(gGameBuild), found);
        return false;
    }
    if (entry == HookEntry::ForeignHook)
        LogHookedOnTop("TextIcons: UiDepend::ActHudChecking", target);
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hk_ActHudChecking), reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("TextIcons: UiDepend::ActHudChecking hook failed at %p, no text icons can be made", target);
        return false;
    }
    g_HookTarget = target;
    return true;
}

void Uninstall_UiDepend_ActHudChecking_Hook()
{
    if (g_HookTarget)
    {
        DisableAndRemoveHook(g_HookTarget);
        g_HookTarget = nullptr;
        g_Orig       = nullptr;
    }
}
