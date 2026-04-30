#include "pch.h"

#include <Windows.h>
#include <cstdint>
#include <unordered_map>
#include <mutex>

#include "HookUtils.h"
#include "log.h"
#include "FoxHashes.h"
#include "PlayerVoiceFpkHook.h"
#include "MissionCodeGuard.h"
#include "AddressSet.h"

namespace
{
    using LoadPlayerVoiceFpk_t = void* (__fastcall*)(void* fileSlotPath, std::uint32_t playerType, std::uint32_t playerFaceId);
    using FoxPath_Path_t = void* (__fastcall*)(void* outPath, std::uint64_t pathCode64Ext);


    static LoadPlayerVoiceFpk_t g_OrigLoadPlayerVoiceFpk = nullptr;
    static FoxPath_Path_t g_FoxPath_Path = nullptr;

    struct VoiceOverrideSlot
    {
        bool enabled = false;
        std::uint64_t pathCode64Ext = 0;
    };

    static std::unordered_map<std::uint32_t, VoiceOverrideSlot> g_TypeOverrides;
    static VoiceOverrideSlot g_FallbackOverride;
    static std::mutex g_OverrideMutex;
}


static bool ResolvePlayerVoiceApi()
{
    if (!g_FoxPath_Path)
    {
        g_FoxPath_Path = reinterpret_cast<FoxPath_Path_t>(
            ResolveGameAddress(gAddr.FoxPath_Path)
            );
    }

    return g_FoxPath_Path != nullptr;
}


static bool IsExplicitType(std::uint32_t playerType)
{
    return playerType == 1 || playerType == 2;
}


static VoiceOverrideSlot GetEffectiveOverrideForType(std::uint32_t playerType)
{
    std::lock_guard<std::mutex> lock(g_OverrideMutex);

    if (IsExplicitType(playerType))
    {
        const auto it = g_TypeOverrides.find(playerType);
        if (it != g_TypeOverrides.end())
            return it->second;

        return {};
    }

    return g_FallbackOverride;
}


static void* WritePlayerVoicePath(void* outPath, std::uint64_t pathCode64Ext)
{
    if (!ResolvePlayerVoiceApi() || !outPath || pathCode64Ext == 0)
        return outPath;

    return g_FoxPath_Path(outPath, pathCode64Ext);
}

static void* __fastcall hkLoadPlayerVoiceFpk(void* fileSlotPath, std::uint32_t playerType, std::uint32_t playerFaceId)
{
    if (MissionCodeGuard::ShouldBypassHooks())
    {
        return g_OrigLoadPlayerVoiceFpk(fileSlotPath, playerType, playerFaceId);
    }

    const VoiceOverrideSlot slot = GetEffectiveOverrideForType(playerType);
    if (!slot.enabled || slot.pathCode64Ext == 0)
    {
        return g_OrigLoadPlayerVoiceFpk(fileSlotPath, playerType, playerFaceId);
    }

    Log(
        "[PlayerVoiceFpk] Override hit: playerType=%u faceId=%u pathCode64Ext=0x%016llX\n",
        playerType,
        playerFaceId,
        static_cast<unsigned long long>(slot.pathCode64Ext)
    );

    WritePlayerVoicePath(fileSlotPath, slot.pathCode64Ext);
    return fileSlotPath;
}


bool Install_PlayerVoiceFpk_Hook()
{
    ResolvePlayerVoiceApi();

    void* target = ResolveGameAddress(gAddr.LoadPlayerVoiceFpk);
    if (!target)
    {
        Log("[Hook] PlayerVoiceFpk: target resolve failed\n");
        return false;
    }

    const bool ok = CreateAndEnableHook(
        target,
        reinterpret_cast<void*>(&hkLoadPlayerVoiceFpk),
        reinterpret_cast<void**>(&g_OrigLoadPlayerVoiceFpk)
    );

    Log("[Hook] PlayerVoiceFpk: %s\n", ok ? "OK" : "FAIL");
    return ok;
}


bool Uninstall_PlayerVoiceFpk_Hook()
{
    DisableAndRemoveHook(ResolveGameAddress(gAddr.LoadPlayerVoiceFpk));
    g_OrigLoadPlayerVoiceFpk = nullptr;
    g_FoxPath_Path = nullptr;

    {
        std::lock_guard<std::mutex> lock(g_OverrideMutex);
        g_TypeOverrides.clear();
        g_FallbackOverride = {};
    }

    Log("[Hook] PlayerVoiceFpk: removed\n");
    return true;
}


void Set_PlayerVoiceFpkPathForType(std::uint32_t playerType, const char* rawPath)
{
    if (!rawPath || !*rawPath)
        return;

    const std::uint64_t pathCode64Ext = FoxHashes::PathCode64Ext(rawPath);

    std::lock_guard<std::mutex> lock(g_OverrideMutex);

    if (IsExplicitType(playerType))
    {
        VoiceOverrideSlot& slot = g_TypeOverrides[playerType];
        slot.enabled = true;
        slot.pathCode64Ext = pathCode64Ext;
    }
    else
    {
        g_FallbackOverride.enabled = true;
        g_FallbackOverride.pathCode64Ext = pathCode64Ext;
    }

    Log(
        "[PlayerVoiceFpk] Type override set: playerType=%u path=%s -> 0x%016llX\n",
        playerType,
        rawPath,
        static_cast<unsigned long long>(pathCode64Ext)
    );
}


void Clear_PlayerVoiceFpkPathForType(std::uint32_t playerType)
{
    std::lock_guard<std::mutex> lock(g_OverrideMutex);

    if (IsExplicitType(playerType))
    {
        g_TypeOverrides.erase(playerType);
    }
    else
    {
        g_FallbackOverride = {};
    }

    Log("[PlayerVoiceFpk] Type override cleared: playerType=%u\n", playerType);
}


void Clear_AllPlayerVoiceFpkOverrides()
{
    std::lock_guard<std::mutex> lock(g_OverrideMutex);

    g_TypeOverrides.clear();
    g_FallbackOverride = {};

    Log("[PlayerVoiceFpk] All overrides cleared\n");
}