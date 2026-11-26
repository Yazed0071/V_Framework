#include <pch.h>
#include "AkSoundEngine_PostEvent.h"

#include <Windows.h>
#include <atomic>
#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    using PostEvent_t = std::uint32_t(__fastcall*)(std::uint32_t eventId, std::uint64_t gameObject, std::uint32_t flags, void* callback,
                                                   void* cookie, std::uint32_t externalCount, void* externals, std::uint32_t playingId);

    const std::uint8_t kPostEventEntry[] = { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18,
                                             0x4C, 0x89, 0x74, 0x24, 0x20, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x60 };

    constexpr std::uint32_t kHorseSlap = 0x34A42757u;   // a slap on the horse: the spur motions and the mounted pat idle snakadsn_idl_3
    constexpr std::uint64_t kFreshMs   = 1000;
#ifdef _DEBUG
    // TEMP probe: the Battle Gear's rail-gun events (bank se_e_btlg) as Wwise takes them
    constexpr std::uint32_t kBattleGearRailEvents[] = { 0x4E9523C3u, 0xEAA89C62u, 0x5DA0B062u, 0xA438C8EAu, 0x45B5A695u, 0xF799CBCDu };
#endif

    PostEvent_t                g_Orig   = nullptr;
    void*                      g_Target = nullptr;
    std::atomic<std::uint64_t> g_HorseSlapMs{ 0 };

    std::uint32_t __fastcall hkPostEvent(std::uint32_t eventId, std::uint64_t gameObject, std::uint32_t flags, void* callback, void* cookie,
                                         std::uint32_t externalCount, void* externals, std::uint32_t playingId)
    {
        if (eventId == kHorseSlap)
            g_HorseSlapMs.store(GetTickCount64(), std::memory_order_relaxed);
        const std::uint32_t playing = g_Orig(eventId, gameObject, flags, callback, cookie, externalCount, externals, playingId);
#ifdef _DEBUG
        for (const std::uint32_t railEvent : kBattleGearRailEvents)
            if (eventId == railEvent)
                LogDebug("BattleGear sound: Wwise took rail event 0x%08X on object 0x%llX: playing id %u%s", eventId,
                         static_cast<unsigned long long>(gameObject), playing,
                         playing ? "" : " (0: refused, as it refuses an event whose bank is not loaded)");   // TEMP probe
#endif
        return playing;
    }
}

bool TakeHorseSlapSound()
{
    const std::uint64_t at = g_HorseSlapMs.exchange(0, std::memory_order_relaxed);
    return at != 0 && GetTickCount64() - at < kFreshMs;
}

bool Install_AkSoundEngine_PostEvent_Hook()
{
    if (g_Target)
        return true;
    if (!gAddr.AK_PostEvent)
    {
        LogWarn("HorseVoice: AK::SoundEngine::PostEvent unknown for %s, no line when patting D-Horse", GetGameBuildName(gGameBuild));
        return true;
    }

    void* target = ResolveGameAddress(gAddr.AK_PostEvent);
    const HookEntry state = CheckHookEntry(target, kPostEventEntry, sizeof(kPostEventEntry));
    if (state == HookEntry::Unknown)
    {
        LogError("HorseVoice: AK::SoundEngine::PostEvent differs on %s, no line when patting D-Horse", GetGameBuildName(gGameBuild));
        return false;
    }
    if (state == HookEntry::ForeignHook)
        LogHookedOnTop("HorseVoice: AK::SoundEngine::PostEvent", target);

    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hkPostEvent), reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("HorseVoice: AK::SoundEngine::PostEvent hook failed at %p, no line when patting D-Horse", target);
        g_Orig = nullptr;
        return false;
    }
    g_Target = target;
    return true;
}

void Uninstall_AkSoundEngine_PostEvent_Hook()
{
    if (g_Target)
        DisableAndRemoveHook(g_Target);
    g_Target = nullptr;
    g_Orig   = nullptr;
    g_HorseSlapMs.store(0, std::memory_order_relaxed);
}
