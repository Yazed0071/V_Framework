#include <pch.h>
#include "Block_Reload.h"

#include <intrin.h>
#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"
#include "MissionStateReset.h"

namespace
{
    using Reload_t = std::uintptr_t(__fastcall*)(void* block, void* out, void* arg3, void* arg4);

    struct ReloadSites
    {
        std::uintptr_t reload;        // fox::Block::Reload, four callers
        std::uintptr_t missionCall;   // SimpleMissionBlockController::Update, checkpoint branch: call fox::Block::Reload
    };

    constexpr std::size_t kCallSize = 5;

    const std::uint8_t kReloadEntry[] = { 0x40, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83,
                                          0xEC, 0x30, 0x48, 0xC7, 0x44, 0x24, 0x20, 0xFE, 0xFF, 0xFF, 0xFF };

    Reload_t g_Orig          = nullptr;
    void*    g_Target        = nullptr;
    void*    g_MissionReturn = nullptr;

    const ReloadSites* SitesFor()
    {
        static const ReloadSites sites = { gAddr.FoxBlock_Reload, gAddr.SimpleMissionBlockController_Update_ReloadCall };
        return &sites;
    }

    bool CallsReload(const std::uint8_t* call, const void* reload)
    {
        std::int32_t rel = 0;
        std::memcpy(&rel, call + 1, sizeof(rel));
        return call[0] == 0xE8 && call + kCallSize + rel == reload;
    }

    std::uintptr_t __fastcall hkReload(void* block, void* out, void* arg3, void* arg4)
    {
        if (_ReturnAddress() == g_MissionReturn)
            MissionStateReset::OnCheckpointReload();
        return g_Orig(block, out, arg3, arg4);
    }
}

bool Install_Block_Reload_Hook()
{
    if (g_Target)
        return true;
    const ReloadSites* sites = SitesFor();
    if (!sites)
    {
        LogWarn("MissionStateReset: checkpoint reload unknown for %s, queued radio calls and soldier hand-offs survive a checkpoint",
                GetGameBuildName(gGameBuild));
        return true;
    }

    void* target = ResolveGameAddress(sites->reload);
    const HookEntry state = CheckHookEntry(target, kReloadEntry, sizeof(kReloadEntry));
    const std::uint8_t* call = static_cast<const std::uint8_t*>(ResolveGameAddress(sites->missionCall));
    if (state == HookEntry::Unknown || !call || !CallsReload(call, target))
    {
        LogError("MissionStateReset: fox::Block::Reload or its mission call differs on %s, queued radio calls and soldier hand-offs survive a checkpoint",
                 GetGameBuildName(gGameBuild));
        return false;
    }
    if (state == HookEntry::ForeignHook)
        LogHookedOnTop("fox::Block::Reload", target);

    g_MissionReturn = const_cast<std::uint8_t*>(call + kCallSize);
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hkReload), reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("MissionStateReset: fox::Block::Reload hook failed at %p, queued radio calls and soldier hand-offs survive a checkpoint", target);
        g_Orig = nullptr;
        return false;
    }
    g_Target = target;
    return true;
}

void Uninstall_Block_Reload_Hook()
{
    if (g_Target)
        DisableAndRemoveHook(g_Target);
    g_Target        = nullptr;
    g_Orig          = nullptr;
    g_MissionReturn = nullptr;
}
