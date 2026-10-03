#include "pch.h"
#include "CallComradeCommand.h"

#include <Windows.h>
#include <cstddef>
#include <cstdint>
#include <iterator>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"
#include "Soldier2Impl_ProcessSignal.h"

namespace
{
    using GetQuarkSystemTable_t = std::uint8_t*(__fastcall*)();
    using SendSignal_t       = void(__fastcall*)(void* signalSystem, void* error, std::uint16_t targetId,
                                                 const void* signal);
    using PlayVoice_t        = void(__fastcall*)(void* sound, std::uint16_t playerId,
                                                 std::uint32_t voiceId, std::uint32_t priority);
    using IsVoicePlaying_t   = bool(__fastcall*)(void* sound, std::uint16_t playerId);

    constexpr std::uint64_t kCallComradeLabel = 0xFB42E9FFC03Aull; // key_call_cursor_left
    constexpr std::uint32_t kCallComradeVoices[] = { 0x6BD14E8Eu, 0x6BD14E8Du, 0x6BD14E8Cu }; // player voice ids for the "call them over" lines
    constexpr std::uint32_t kVoicePriority = 3;

    constexpr std::size_t kPlayVoiceVtblIndex      = 0x50 / 8;
    constexpr std::size_t kIsVoicePlayingVtblIndex = 0x88 / 8;

    constexpr std::size_t kExecPlayerObject    = 0x08;
    constexpr std::size_t kExecPlayerIndex     = 0x20;
    constexpr std::size_t kExecHeldSoldierId   = 0x26;
    constexpr std::size_t kExecRandomState     = 0x38;

    constexpr std::size_t kBlockPlayerObject   = 0x08;
    constexpr std::size_t kBlockPlayerIndex    = 0x20;
    constexpr std::size_t kBlockState          = 0x24;
    constexpr std::size_t kBlockHeldSoldierId  = 0x26;
    constexpr std::size_t kBlockUpdate         = 0x38;

    constexpr std::uint8_t kBlockStarted  = 1;
    constexpr std::uint8_t kBlockWaiting  = 2;
    constexpr std::uint8_t kBlockFinished = 3;

    constexpr std::uint64_t kOrderDelayAfterVoiceMs = 500;
    constexpr std::uint64_t kLockoutAfterOrderMs    = 2000;
    constexpr std::uint64_t kAbandonedOrderMs       = 15000;

    constexpr std::size_t   kQuarkSignalSystem    = 0x60;
    constexpr std::size_t   kSendSignalVtblIndex  = 0x38 / 8;
    constexpr std::uint16_t kPlayerIndexMask      = 0x1FF;

    struct InterrogationSignal
    {
        std::uint64_t id;
        std::uint16_t playerIndex;
        std::uint32_t request;
    };
    static_assert(offsetof(InterrogationSignal, request) == 0x0C, "the soldier reads the request at +0xC");

    GetQuarkSystemTable_t g_GetQuarkSystemTable = nullptr;
    std::uint64_t         g_OrderLockedUntil    = 0;
    std::uint64_t         g_OrderDueTick        = 0;

    bool IsOrderInFlight()
    {
        return GetTickCount64() < g_OrderLockedUntil;
    }

    template <typename T>
    T& At(std::uint8_t* base, std::size_t offset)
    {
        return *reinterpret_cast<T*>(base + offset);
    }

    template <typename Fn>
    Fn VirtualAt(void* object, std::size_t index)
    {
        return (*reinterpret_cast<Fn**>(object))[index];
    }

    struct PlayerVoice
    {
        void*         sound    = nullptr;
        std::uint16_t playerId = 0;
    };

    PlayerVoice FindPlayerVoice(std::uint8_t* player, std::uint32_t playerIndex)
    {
        auto** soundByAvatar  = At<void**>(player, 0x50);                     // one sound controller per avatar
        auto*  avatarOfPlayer = At<std::uint8_t*>(player, 0x78);              // which avatar each player is using
        auto*  gameObjectIds  = At<std::uint16_t*>(player, 0x70);             // game object id of each player

        PlayerVoice voice;
        voice.sound    = soundByAvatar[avatarOfPlayer[playerIndex]];
        voice.playerId = gameObjectIds[playerIndex];
        return voice;
    }

    std::uint32_t NextRandom(std::uint32_t& state)
    {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 5;
        return state;
    }

    void TellHeldSoldierToCallComrade(std::uint8_t* block)
    {
        if (!g_GetQuarkSystemTable)
            return;

        InterrogationSignal signal{};
        signal.id          = CallComrade::kInterrogationSignal;
        signal.playerIndex = static_cast<std::uint16_t>(At<std::uint32_t>(block, kBlockPlayerIndex) & kPlayerIndexMask);
        signal.request     = CallComrade::kCallComradeRequest;

        void* signalSystem = At<void*>(g_GetQuarkSystemTable(), kQuarkSignalSystem);
        std::uint64_t error = 0;
        VirtualAt<SendSignal_t>(signalSystem, kSendSignalVtblIndex)(signalSystem, &error,
                                                                    At<std::uint16_t>(block, kBlockHeldSoldierId), &signal);
    }

    void __fastcall UpdateCommandFollowCall(std::uint8_t* block)
    {
        CallComrade::UpdatePendingCall();
        At<std::uint8_t>(block, kBlockState) = CallComrade::IsCallPending() ? kBlockWaiting : kBlockFinished;
    }

    void __fastcall UpdateCommandAwaitOrderDelay(std::uint8_t* block)
    {
        const std::uint64_t now = GetTickCount64();
        if (now < g_OrderDueTick)
        {
            At<std::uint8_t>(block, kBlockState) = kBlockWaiting;
            return;
        }

        TellHeldSoldierToCallComrade(block);
        g_OrderLockedUntil = now + kLockoutAfterOrderMs;

        At<void*>(block, kBlockUpdate) = reinterpret_cast<void*>(&UpdateCommandFollowCall);
        UpdateCommandFollowCall(block);
    }

    void __fastcall UpdateCommandCallComrade(std::uint8_t* block)
    {
        if (At<std::uint8_t>(block, kBlockState) == kBlockStarted)
        {
            At<std::uint8_t>(block, kBlockState) = kBlockWaiting;
            return;
        }

        if (CallComradeCommand::IsPlayerLinePlaying(block))
        {
            At<std::uint8_t>(block, kBlockState) = kBlockWaiting;
            return;
        }

        g_OrderDueTick = GetTickCount64() + kOrderDelayAfterVoiceMs;
        At<void*>(block, kBlockUpdate) = reinterpret_cast<void*>(&UpdateCommandAwaitOrderDelay);
        At<std::uint8_t>(block, kBlockState) = kBlockWaiting;
    }

    void __fastcall UpdateCommandIgnored(std::uint8_t* block)
    {
        At<std::uint8_t>(block, kBlockState) = kBlockFinished;
    }

}

void CallComradeCommand::PlayRandomPlayerLine(std::uint8_t* execParam, const std::uint32_t* voices, std::size_t count)
{
    if (!voices || count == 0)
        return;

    const std::uint32_t roll = NextRandom(*At<std::uint32_t*>(execParam, kExecRandomState));
    const std::uint32_t voiceId = voices[roll % count];

    const PlayerVoice voice = FindPlayerVoice(At<std::uint8_t*>(execParam, kExecPlayerObject),
                                              At<std::uint32_t>(execParam, kExecPlayerIndex));
    if (voice.sound)
        VirtualAt<PlayVoice_t>(voice.sound, kPlayVoiceVtblIndex)(voice.sound, voice.playerId, voiceId, kVoicePriority);
}

bool CallComradeCommand::IsPlayerLinePlaying(std::uint8_t* block)
{
    const PlayerVoice voice = FindPlayerVoice(At<std::uint8_t*>(block, kBlockPlayerObject),
                                              At<std::uint32_t>(block, kBlockPlayerIndex));
    return voice.sound && VirtualAt<IsVoicePlaying_t>(voice.sound, kIsVoicePlayingVtblIndex)(voice.sound, voice.playerId);
}

bool CallComradeCommand::Exec(std::uint8_t* execParam, std::uint8_t* block)
{
    if (IsOrderInFlight())
    {
        At<void*>(block, kBlockUpdate) = reinterpret_cast<void*>(&UpdateCommandIgnored);
        return true;
    }

    g_OrderLockedUntil = GetTickCount64() + kAbandonedOrderMs;
    PlayRandomPlayerLine(execParam, kCallComradeVoices, std::size(kCallComradeVoices));

    At<void*>(block, kBlockUpdate) = reinterpret_cast<void*>(&UpdateCommandCallComrade);
    At<std::uint16_t>(block, kBlockHeldSoldierId) = At<std::uint16_t>(execParam, kExecHeldSoldierId);
    return true;
}

std::uint64_t CallComradeCommand::Label()
{
    return kCallComradeLabel;
}

void CallComradeCommand::Initialize()
{
    if (gAddr.GetQuarkSystemTable)
        g_GetQuarkSystemTable = reinterpret_cast<GetQuarkSystemTable_t>(ResolveGameAddress(gAddr.GetQuarkSystemTable));
    else
        LogError("CallComradeCommand: GetQuarkSystemTable missing for %s, held soldiers will not call comrades",
                 GetGameBuildName(gGameBuild));
}
