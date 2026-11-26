#include <pch.h>
#include "CqcActionPluginImpl_CompleteContinueCqcTarget.h"

#include <Windows.h>
#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"
#include "MissionCodeGuard.h"
#include "Player2Impl_CallVoice.h"

namespace
{
    using CompleteContinueCqcTarget_t = void(__fastcall*)(void* work, std::uint16_t target);

    const std::uint8_t kEntry[] = { 0x45, 0x33, 0xC0, 0x44, 0x38, 0x81, 0x90, 0x05, 0x00, 0x00 }; // xor r8d,r8d / cmp [rcx+0x590],r8b

    constexpr std::uint32_t kCqcCombo = 0xA904C8DBu;   // "Consecutive! C! Q! C!"
    constexpr std::uint64_t kChainMs  = 5000;

    CompleteContinueCqcTarget_t g_Orig       = nullptr;
    void*                       g_HookTarget = nullptr;
    std::uint16_t               g_LastTarget = 0xFFFF;
    std::uint64_t               g_LastMs     = 0;
    bool                        g_Said       = false;

    void __fastcall hk_CompleteContinueCqcTarget(void* work, std::uint16_t target)
    {
        g_Orig(work, target);
        if (MissionCodeGuard::ShouldBypassHooks())
            return;

        const std::uint64_t now = GetTickCount64();
        if (now - g_LastMs > kChainMs)
            g_Said = false;
        else if (target != g_LastTarget && !g_Said)
        {
            g_Said = true;
            SayPlayerLine(kCqcCombo);
        }
        g_LastTarget = target;
        g_LastMs     = now;
    }
}

bool Install_CqcActionPluginImpl_CompleteContinueCqcTarget_Hook()
{
    if (g_HookTarget)
        return true;

    const std::uintptr_t address = gAddr.CqcActionPluginImpl_CompleteContinueCqcTarget;
    if (!address)
    {
        LogWarn("CqcComboVoice: CqcActionPluginImpl::Work::CompleteContinueCqcTarget unknown for %s, no CQC combo line",
                GetGameBuildName(gGameBuild));
        return true;
    }

    void* target = ResolveGameAddress(address);
    const HookEntry entry = CheckHookEntry(target, kEntry, sizeof(kEntry));
    if (entry == HookEntry::Unknown)
    {
        LogError("CqcComboVoice: CompleteContinueCqcTarget entry mismatch on %s, no CQC combo line", GetGameBuildName(gGameBuild));
        return false;
    }
    if (entry == HookEntry::ForeignHook)
        LogHookedOnTop("CqcComboVoice: CompleteContinueCqcTarget", target);

    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hk_CompleteContinueCqcTarget), reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("CqcComboVoice: CompleteContinueCqcTarget hook failed at %p, no CQC combo line", target);
        return false;
    }
    g_HookTarget = target;
    return true;
}

void Uninstall_CqcActionPluginImpl_CompleteContinueCqcTarget_Hook()
{
    if (g_HookTarget)
    {
        DisableAndRemoveHook(g_HookTarget);
        g_HookTarget = nullptr;
        g_Orig       = nullptr;
    }
}
