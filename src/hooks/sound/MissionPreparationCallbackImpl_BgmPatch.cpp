#include "pch.h"
#include "MissionPreparationCallbackImpl_BgmPatch.h"

#include <Windows.h>
#include <atomic>
#include <cstring>
#include <mutex>
#include <unordered_map>

#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "log.h"

namespace
{
    struct BgmEvents
    {
        std::uint32_t play = 0;
        std::uint32_t stop = 0;
    };

    constexpr BgmEvents kVanilla{ 0x9FC06F1Au, 0xCA72782Cu };

    constexpr std::size_t kCallbackSystem    = 0x28;
    constexpr std::size_t kSystemMissionName = 0x2D8;
    constexpr std::size_t kMissionNameKind   = 0x00;
    constexpr std::size_t kMissionNameCode   = 0x04;
    constexpr std::uint8_t kMissionNameStory = 1;

    constexpr std::size_t kStartSceneMusic   = 0;

    constexpr std::uint32_t kLowestMissionCode  = 1000u;
    constexpr std::uint32_t kHighestMissionCode = 59999u;

    using Start_t           = void(__fastcall*)(void* self);
    using Update_t          = void(__fastcall*)(void* self);
    using StartSceneMusic_t = void(__fastcall*)(void* sound, std::uint32_t eventId);

    Start_t  g_OrigStart  = nullptr;
    Update_t g_OrigUpdate = nullptr;

    std::mutex g_lock;
    BgmEvents g_global;
    std::unordered_map<std::uint32_t, BgmEvents> g_perMission;
    std::atomic<std::uint32_t> g_configEpoch{ 0 };

    std::atomic<bool> g_updateHooked{ false };
    std::atomic<bool> g_prepActive{ false };
    std::uint32_t g_appliedEpoch = 0;
    BgmEvents g_applied = kVanilla;
    std::uint32_t g_preparedMission = 0;
    bool g_soundSystemReported = false;

    BgmEvents Resolve(std::uint32_t prepared, std::uint32_t current, const char** matchedOut)
    {
        std::lock_guard<std::mutex> guard(g_lock);

        if (prepared != 0)
        {
            const auto it = g_perMission.find(prepared);
            if (it != g_perMission.end())
            {
                *matchedOut = "the mission being prepared";
                return it->second;
            }
        }

        if (current != 0)
        {
            const auto it = g_perMission.find(current);
            if (it != g_perMission.end())
            {
                *matchedOut = "the mission the player is standing in";
                return it->second;
            }
        }

        if (g_global.play != 0 && g_global.stop != 0)
        {
            *matchedOut = "the mission-independent setting";
            return g_global;
        }

        *matchedOut = "vanilla";
        return kVanilla;
    }

    bool HasPerMissionEntries()
    {
        std::lock_guard<std::mutex> guard(g_lock);
        return !g_perMission.empty();
    }

    bool InMissionCodeRange(std::uint32_t code)
    {
        return code >= kLowestMissionCode && code <= kHighestMissionCode;
    }

    bool TryReadMissionNameBlock(void* self, std::uint8_t* kindOut, std::uint32_t* rawOut)
    {
        if (!self)
            return false;

        __try
        {
            std::uint8_t* system = *reinterpret_cast<std::uint8_t**>(
                reinterpret_cast<std::uint8_t*>(self) + kCallbackSystem);
            if (!system)
                return false;

            std::uint8_t* block = system + kSystemMissionName;
            const std::uint8_t kind = *(block + kMissionNameKind);

            *kindOut = kind;
            *rawOut = *reinterpret_cast<std::uint16_t*>(block + kMissionNameCode);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    std::uint32_t ReadPreparedMissionCode(void* self, std::uint8_t* kindOut, std::uint32_t* rawOut)
    {
        std::uint8_t kind = 0;
        std::uint32_t raw = 0;

        const bool read = TryReadMissionNameBlock(self, &kind, &raw);
        if (kindOut) *kindOut = kind;
        if (rawOut)  *rawOut = raw;

        if (!read || kind != kMissionNameStory)
            return 0;

        return InMissionCodeRange(raw) ? raw : 0u;
    }

    std::uint8_t* ReadSoundSystem()
    {
        void* global = ResolveGameAddress(gAddr.g_SoundSystem);
        if (!global)
            return nullptr;

        __try
        {
            return *reinterpret_cast<std::uint8_t**>(global);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    bool HasSoundSystemVtable(std::uint8_t* sound)
    {
        void* expected = ResolveGameAddress(gAddr.CassettePlayerVtable);
        if (!sound || !expected)
            return false;

        __try
        {
            return *reinterpret_cast<void**>(sound) == expected;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    std::uint8_t* GetPrepSoundSystem()
    {
        std::uint8_t* sound = ReadSoundSystem();
        if (HasSoundSystemVtable(sound))
            return sound;

        if (!g_soundSystemReported)
        {
            g_soundSystemReported = true;
            Log("[MissionPreparationBgm] the menu sound system did not carry the expected vtable, so the music cannot be swapped while the screen is open; the setting still takes effect the next time the screen opens\n");
        }
        return nullptr;
    }

    bool StartScenePrepMusic(std::uint32_t eventId)
    {
        std::uint8_t* sound = GetPrepSoundSystem();
        if (!sound || eventId == 0)
            return false;

        __try
        {
            void** vtbl = *reinterpret_cast<void***>(sound);
            reinterpret_cast<StartSceneMusic_t>(vtbl[kStartSceneMusic])(sound, eventId);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool WriteEventOperand(uintptr_t site, std::uint32_t eventId)
    {
        if (!site)
        {
            Log("[MissionPreparationBgm] an event operand site is unset for this build; the sortie-preparation music stays vanilla\n");
            return false;
        }

        void* target = ResolveGameAddress(site);
        if (!target)
        {
            Log("[MissionPreparationBgm] operand site 0x%llx did not resolve; the sortie-preparation music stays vanilla\n",
                static_cast<unsigned long long>(site));
            return false;
        }

        DWORD oldProtect = 0;
        if (!VirtualProtect(target, sizeof(eventId), PAGE_EXECUTE_READWRITE, &oldProtect))
        {
            Log("[MissionPreparationBgm] VirtualProtect failed at 0x%llx (err=%lu); the operand was left untouched\n",
                static_cast<unsigned long long>(site), GetLastError());
            return false;
        }

        std::memcpy(target, &eventId, sizeof(eventId));

        DWORD restored = 0;
        VirtualProtect(target, sizeof(eventId), oldProtect, &restored);
        FlushInstructionCache(GetCurrentProcess(), target, sizeof(eventId));
        return true;
    }

    bool ApplyEvents(const BgmEvents& events)
    {
        const bool okPlay  = WriteEventOperand(gAddr.MissionPreparationCallbackImpl_Start_PlayBgmEvent, events.play);
        const bool okStop  = WriteEventOperand(gAddr.MissionPreparationCallbackImpl_Update_StopBgmEvent, events.stop);
        const bool okStop2 = WriteEventOperand(gAddr.MissionPreparationCallbackImpl_Update_StopBgmEvent2, events.stop);
        return okPlay && okStop && okStop2;
    }

    void __fastcall hkStart(void* self)
    {
        std::uint8_t kind = 0;
        std::uint32_t raw = 0;
        g_preparedMission = ReadPreparedMissionCode(self, &kind, &raw);
        const std::uint32_t current = MissionCodeGuard::GetCurrentMissionCode();

        const char* matched = "vanilla";
        const BgmEvents events = Resolve(g_preparedMission, current, &matched);

        ApplyEvents(events);
        g_applied = events;
        g_appliedEpoch = g_configEpoch.load(std::memory_order_relaxed);
        g_prepActive.store(g_updateHooked.load(std::memory_order_relaxed), std::memory_order_relaxed);

        LogDebug("[MissionPreparationBgm] sortie preparation opened: mission being prepared %u (name block kind %u, raw %u), mission the player is standing in %u, using the entry keyed to %s\n",
            g_preparedMission, kind, raw, current, matched);

        if (g_preparedMission == 0 && HasPerMissionEntries())
        {
            Log("[MissionPreparationBgm] the mission being prepared did not read back as a mission code (name block kind %u, raw %u), so a per-mission entry can only match the mission the player is standing in (%u); key the entry to that code or set a mission-independent one\n",
                kind, raw, current);
        }

        g_OrigStart(self);
    }

    void __fastcall hkUpdate(void* self)
    {
        if (g_prepActive.load(std::memory_order_relaxed))
        {
            const std::uint32_t epoch = g_configEpoch.load(std::memory_order_relaxed);
            const std::uint32_t prepared = ReadPreparedMissionCode(self, nullptr, nullptr);

            if (epoch != g_appliedEpoch || prepared != g_preparedMission)
            {
                g_appliedEpoch = epoch;
                g_preparedMission = prepared;

                const char* matched = "vanilla";
                const BgmEvents events =
                    Resolve(prepared, MissionCodeGuard::GetCurrentMissionCode(), &matched);

                if (events.play != g_applied.play || events.stop != g_applied.stop)
                {
                    const bool restart = events.play != g_applied.play;
                    ApplyEvents(events);
                    g_applied = events;

                    if (restart && StartScenePrepMusic(events.play))
                    {
                        LogDebug("[MissionPreparationBgm] music swapped while the screen was open: now using the entry keyed to %s\n",
                            matched);
                    }
                }
            }
        }

        g_OrigUpdate(self);
    }
}

bool SetMissionPreparationMusic(std::uint32_t playEventHash,
                                std::uint32_t stopEventHash,
                                std::uint32_t missionCode)
{
    if (playEventHash == 0 || stopEventHash == 0)
        return false;

    {
        std::lock_guard<std::mutex> guard(g_lock);
        const BgmEvents entry{ playEventHash, stopEventHash };
        if (missionCode == 0)
            g_global = entry;
        else
            g_perMission[missionCode] = entry;
    }

    g_configEpoch.fetch_add(1, std::memory_order_relaxed);

    if (g_prepActive.load(std::memory_order_relaxed) && g_updateHooked.load(std::memory_order_relaxed))
        return true;

    const char* matched = "vanilla";
    return ApplyEvents(Resolve(0, MissionCodeGuard::GetCurrentMissionCode(), &matched));
}

bool Install_MissionPreparationCallbackImpl_Start_Hook()
{
    void* start = ResolveGameAddress(gAddr.MissionPreparationCallbackImpl_Start);
    if (!start)
    {
        Log("[Hook] MissionPreparationCallbackImpl_Start: address unset for this build; per-mission sortie-preparation music is unavailable and only a mission-independent setting applies\n");
        return false;
    }

    if (!CreateAndEnableHook(start,
                             reinterpret_cast<void*>(&hkStart),
                             reinterpret_cast<void**>(&g_OrigStart)))
    {
        Log("[Hook] MissionPreparationCallbackImpl_Start: FAIL; per-mission sortie-preparation music is unavailable and only a mission-independent setting applies\n");
        return false;
    }

    void* update = ResolveGameAddress(gAddr.MissionPreparationCallbackImpl_Update);
    if (update && CreateAndEnableHook(update,
                                      reinterpret_cast<void*>(&hkUpdate),
                                      reinterpret_cast<void**>(&g_OrigUpdate)))
    {
        g_updateHooked.store(true, std::memory_order_relaxed);
    }
    else
    {
        Log("[Hook] MissionPreparationCallbackImpl_Update: FAIL; the music can still be chosen before the screen opens but changing it while the screen is open will not take effect until the next open\n");
    }

    return true;
}

bool Uninstall_MissionPreparationCallbackImpl_Start_Hook()
{
    ApplyEvents(kVanilla);
    DisableAndRemoveHook(ResolveGameAddress(gAddr.MissionPreparationCallbackImpl_Update));
    DisableAndRemoveHook(ResolveGameAddress(gAddr.MissionPreparationCallbackImpl_Start));
    g_OrigUpdate = nullptr;
    g_OrigStart = nullptr;
    return true;
}
