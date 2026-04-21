#include "pch.h"

#include <Windows.h>
#include <cstdint>

#include "HookUtils.h"
#include "log.h"
#include "VIPSoundRecoveryHook.h"
#include "MissionCodeGuard.h"
#include "AddressSet.h"

namespace
{
    // Hook type for NoticeActionImpl::State_StandEnterRecoverySleepFaintHoldupComradeBySound.
    // Params: self, actorId, proc, evt
    using State_SoundRecovery_t =
        void(__fastcall*)(void* self, std::uint32_t actorId, int proc, void* evt);

    // Event hash the vanilla path uses to dispatch the voice notice.
    static constexpr std::uint32_t HASH_EVENT_VOICE_NOTICE = 0x1077DB8Du;

    // Reaction category used by the game's notice reaction manager.
    static constexpr std::uint32_t HASH_REACTION_CATEGORY_NOTICE = 0x95EA16B0u;

    // Custom reaction hash for sleep/faint wake. Shared with the Touch/Kick
    // hook so all three recovery paths collapse to a single Lua handler.
    // Replaces the vanilla 0x9CD0A89E selected when entry[+0x57] != 0x0A.
    static constexpr std::uint32_t HASH_SLEEP_WAKE_OFFICER = 0x9CD0A89Cu;

    // Custom reaction hash for holdup recovery. Shared with the Holdup hook.
    // Replaces the vanilla 0x92D098DF selected when entry[+0x57] == 0x0A.
    static constexpr std::uint32_t HASH_HOLDUP_RECOVERY_VIP = 0x92D098DEu;

    // Original function pointer captured at install time.
    static State_SoundRecovery_t g_OrigState_SoundRecovery = nullptr;
}

// Safely reads one byte from memory.
// Params: addr, outValue
static bool SafeReadByte(std::uintptr_t addr, std::uint8_t& outValue)
{
    if (!addr)
        return false;

    __try
    {
        outValue = *reinterpret_cast<const std::uint8_t*>(addr);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

// Resolves one NoticeAction state entry using the game's table math.
// Mirrors the vanilla pattern: (actorId - self[+0x98]) * 0x68 + self[+0x90].
// Params: self, actorId
static std::uintptr_t GetNoticeActionEntry(void* self, std::uint32_t actorId)
{
    if (!self)
        return 0;

    const std::uintptr_t selfAddr = reinterpret_cast<std::uintptr_t>(self);

    __try
    {
        const std::uint32_t baseIndex =
            *reinterpret_cast<const std::uint32_t*>(selfAddr + 0x98ull);

        const std::uint64_t tableBase =
            *reinterpret_cast<const std::uint64_t*>(selfAddr + 0x90ull);

        const std::uint32_t slot = actorId - baseIndex;
        return static_cast<std::uintptr_t>(
            tableBase + (static_cast<std::uint64_t>(slot) * 0x68ull));
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return 0;
    }
}

// Reads the event hash by calling the first virtual function on the event object.
// Params: evt
static std::uint32_t GetEventHash(void* evt)
{
    if (!evt)
        return 0;

    __try
    {
        const auto objectAddr = reinterpret_cast<std::uintptr_t>(evt);
        const auto vtbl = *reinterpret_cast<const std::uintptr_t*>(objectAddr);
        if (!vtbl)
            return 0;

        const auto fnAddr = *reinterpret_cast<const std::uintptr_t*>(vtbl + 0x0ull);
        if (!fnAddr)
            return 0;

        using GetHashFn_t = std::uint32_t(__fastcall*)(void*);
        auto fn = reinterpret_cast<GetHashFn_t>(fnAddr);
        return fn(evt);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return 0;
    }
}

// Dispatches one custom notice reaction through the game's reaction manager.
// Mirrors the vanilla call chain: self[+0x78][+0xA8]->vtable[+0x20](...).
// Params: noticeSelf, actorId, reactionHash
static void DispatchNoticeReaction(void* noticeSelf, std::uint32_t actorId, std::uint32_t reactionHash)
{
    if (!noticeSelf)
        return;

    __try
    {
        const std::uintptr_t selfAddr = reinterpret_cast<std::uintptr_t>(noticeSelf);

        const std::uint64_t soldierActionRoot =
            *reinterpret_cast<const std::uint64_t*>(selfAddr + 0x78ull);
        if (!soldierActionRoot)
            return;

        const std::uint64_t reactionMgr =
            *reinterpret_cast<const std::uint64_t*>(
                static_cast<std::uintptr_t>(soldierActionRoot) + 0xA8ull);
        if (!reactionMgr)
            return;

        const std::uint64_t vtbl =
            *reinterpret_cast<const std::uint64_t*>(static_cast<std::uintptr_t>(reactionMgr));
        if (!vtbl)
            return;

        const std::uint64_t fnAddr =
            *reinterpret_cast<const std::uint64_t*>(static_cast<std::uintptr_t>(vtbl) + 0x20ull);
        if (!fnAddr)
            return;

        using DispatchFn_t =
            void(__fastcall*)(void* mgr,
                std::uint32_t actorId,
                std::uint32_t categoryHash,
                int arg4,
                std::uint32_t reactionHash,
                float delaySeconds);

        auto fn = reinterpret_cast<DispatchFn_t>(fnAddr);
        fn(
            reinterpret_cast<void*>(reactionMgr),
            actorId,
            HASH_REACTION_CATEGORY_NOTICE,
            1,
            reactionHash,
            1.0f);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        Log("[SoundRecovery] DispatchNoticeReaction exception\n");
    }
}

// Hooked State_StandEnterRecoverySleepFaintHoldupComradeBySound.
// On proc=6 with the voice-notice event, swaps the vanilla reaction hash
// (0x9CD0A89E or 0x92D098DF, picked by entry[+0x57] == 0x0A) for the custom
// hash used by the other recovery hooks, then skips the original so the game
// does not also dispatch the vanilla reaction. All other procs and events
// pass through unchanged.
// Params: self, actorId, proc, evt
static void __fastcall hkState_SoundRecovery(
    void* self,
    std::uint32_t actorId,
    int proc,
    void* evt)
{
    MISSION_GUARD_ORIGINAL_VOID(g_OrigState_SoundRecovery, self, actorId, proc, evt);

    if (proc == 6 && evt)
    {
        const std::uint32_t eventHash = GetEventHash(evt);

        if (eventHash == HASH_EVENT_VOICE_NOTICE)
        {
            const std::uintptr_t entry = GetNoticeActionEntry(self, actorId);

            if (entry)
            {
                std::uint8_t b57 = 0;
                if (SafeReadByte(entry + 0x57ull, b57))
                {
                    const std::uint32_t replacementHash =
                        (b57 == 0x0Au)
                            ? HASH_HOLDUP_RECOVERY_VIP
                            : HASH_SLEEP_WAKE_OFFICER;

                    Log("[SoundRecovery] actor=%u b57=0x%02X dispatch=0x%08X\n",
                        actorId,
                        static_cast<unsigned>(b57),
                        replacementHash);

                    DispatchNoticeReaction(self, actorId, replacementHash);
                    return;
                }
            }

            Log("[SoundRecovery] actor=%u voice-notice: entry/byte read failed, vanilla fallback\n",
                actorId);
        }
    }

    if (g_OrigState_SoundRecovery)
        g_OrigState_SoundRecovery(self, actorId, proc, evt);
}

// Installs the sound-recovery hook.
// Params: none
bool Install_VIPSoundRecovery_Hook()
{
    void* target = ResolveGameAddress(
        gAddr.State_StandEnterRecoverySleepFaintHoldupComradeBySound);

    if (!target)
    {
        Log("[SoundRecovery] Install failed: address not resolved\n");
        return false;
    }

    const bool ok = CreateAndEnableHook(
        target,
        reinterpret_cast<void*>(&hkState_SoundRecovery),
        reinterpret_cast<void**>(&g_OrigState_SoundRecovery));

    Log("[SoundRecovery] Install State_StandEnterRecoverySleepFaintHoldupComradeBySound: %s\n",
        ok ? "OK" : "FAIL");

    return ok;
}

// Removes the sound-recovery hook.
// Params: none
bool Uninstall_VIPSoundRecovery_Hook()
{
    DisableAndRemoveHook(
        ResolveGameAddress(gAddr.State_StandEnterRecoverySleepFaintHoldupComradeBySound));

    g_OrigState_SoundRecovery = nullptr;

    Log("[SoundRecovery] Hook removed\n");
    return true;
}
