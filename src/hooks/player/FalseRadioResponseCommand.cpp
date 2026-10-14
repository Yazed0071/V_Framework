#include "pch.h"
#include "FalseRadioResponseCommand.h"

#include <Windows.h>
#include <cstddef>
#include <cstdint>
#include <iterator>

#include "CallComradeCommand.h"
#include "RadioControllerImpl_StepRadioWaitResponse.h"
#include "Soldier2Impl_ProcessSignal.h"

namespace
{
    constexpr std::uint64_t kCallBackLabel = 0xC1DBDF4F6C56ull; // V_voice_cmd_res
    constexpr std::uint64_t kSitrepLabel   = 0xB1737C5E49Full;  // V_voice_cmd_cheat
    constexpr std::uint64_t kCallBackHelp  = 0xE5117C8FCFC3ull; // V_voice_cmd_res_help
    constexpr std::uint64_t kSitrepHelp    = 0xC6C6142BB9A9ull; // V_voice_cmd_cheat_help
    constexpr std::uint32_t kFalseRadioResponseVoices[] = { 0xEAD084A8u, 0xEAD084ABu, 0xEAD084AAu }; // PLA0020_001, PLA0020_002, PLA0020_003

    constexpr std::size_t  kExecPlayerIndex    = 0x20;
    constexpr std::size_t  kExecHeldSoldierId  = 0x26;
    constexpr std::size_t  kBlockPlayerIndex   = 0x20;
    constexpr std::size_t  kBlockState         = 0x24;
    constexpr std::size_t  kBlockHeldSoldierId = 0x26;
    constexpr std::size_t  kBlockUpdate        = 0x38;
    constexpr std::uint8_t kBlockStarted      = 1;
    constexpr std::uint8_t kBlockWaiting      = 2;
    constexpr std::uint8_t kBlockFinished     = 3;

    constexpr std::uint64_t kAnswerDelayAfterVoiceMs = 500;
    constexpr std::uint64_t kAbandonedOrderMs        = 15000;

    std::uint64_t g_AnswerDueTick  = 0;
    std::uint64_t g_OrderBusyUntil = 0;   // one lie order at a time, like call em
    std::uint32_t g_Order          = 0;
    std::uint32_t g_Excuse         = 0;

    template <typename T>
    T& At(std::uint8_t* base, std::size_t offset)
    {
        return *reinterpret_cast<T*>(base + offset);
    }

    void __fastcall UpdateCommandFinished(std::uint8_t* block)
    {
        At<std::uint8_t>(block, kBlockState) = kBlockFinished;
    }

    void __fastcall UpdateCommandAwaitAnswerDelay(std::uint8_t* block)
    {
        if (GetTickCount64() < g_AnswerDueTick)
        {
            At<std::uint8_t>(block, kBlockState) = kBlockWaiting;
            return;
        }

        CpRadioResponse::LetSoldierAnswer(g_Order);
        g_OrderBusyUntil = 0;
        At<std::uint8_t>(block, kBlockState) = kBlockFinished;
    }

    void __fastcall UpdateCommandAwaitOrder(std::uint8_t* block)
    {
        if (At<std::uint8_t>(block, kBlockState) == kBlockStarted || CallComradeCommand::IsPlayerLinePlaying(block))
        {
            At<std::uint8_t>(block, kBlockState) = kBlockWaiting;
            return;
        }

        g_AnswerDueTick = GetTickCount64() + kAnswerDelayAfterVoiceMs;
        At<void*>(block, kBlockUpdate) = reinterpret_cast<void*>(&UpdateCommandAwaitAnswerDelay);
        At<std::uint8_t>(block, kBlockState) = kBlockWaiting;
    }

    void __fastcall UpdateCommandAwaitExcuse(std::uint8_t* block)   // he won't lie for Snake: a "Huh?" or a refusal instead
    {
        if (At<std::uint8_t>(block, kBlockState) == kBlockStarted || CallComradeCommand::IsPlayerLinePlaying(block))
        {
            At<std::uint8_t>(block, kBlockState) = kBlockWaiting;
            return;
        }

        CallComradeCommand::SendHeldSoldierRequest(At<std::uint16_t>(block, kBlockHeldSoldierId),
                                                   At<std::uint32_t>(block, kBlockPlayerIndex), g_Excuse);
        g_OrderBusyUntil = 0;
        At<std::uint8_t>(block, kBlockState) = kBlockFinished;
    }
}

bool FalseRadioResponseCommand::Exec(std::uint8_t* execParam, std::uint8_t* block)
{
    const std::uint64_t now = GetTickCount64();
    const std::uint16_t held = At<std::uint16_t>(execParam, kExecHeldSoldierId);
    if (now < g_OrderBusyUntil)
    {
        At<void*>(block, kBlockUpdate) = reinterpret_cast<void*>(&UpdateCommandFinished);
        return true;
    }

    g_Excuse = CallComradeCommand::ExcuseOf(held, At<std::uint32_t>(execParam, kExecPlayerIndex));
    if (g_Excuse)
    {
        g_OrderBusyUntil = now + kAbandonedOrderMs;
        CallComradeCommand::PlayRandomPlayerLine(execParam, kFalseRadioResponseVoices, std::size(kFalseRadioResponseVoices));
        At<std::uint16_t>(block, kBlockHeldSoldierId) = held;
        At<void*>(block, kBlockUpdate) = reinterpret_cast<void*>(&UpdateCommandAwaitExcuse);
        return true;
    }

    const std::uint32_t order = CpRadioResponse::OrderFalseResponse(held);
    if (order == 0)
    {
        At<void*>(block, kBlockUpdate) = reinterpret_cast<void*>(&UpdateCommandFinished);
        return true;
    }

    g_Order          = order;
    g_OrderBusyUntil = now + kAbandonedOrderMs;
    CallComradeCommand::PlayRandomPlayerLine(execParam, kFalseRadioResponseVoices, std::size(kFalseRadioResponseVoices));
    At<void*>(block, kBlockUpdate) = reinterpret_cast<void*>(&UpdateCommandAwaitOrder);
    return true;
}

std::uint64_t FalseRadioResponseCommand::Label(bool sitrep)
{
    return sitrep ? kSitrepLabel : kCallBackLabel;
}

std::uint64_t FalseRadioResponseCommand::Help(bool sitrep)
{
    return sitrep ? kSitrepHelp : kCallBackHelp;
}
