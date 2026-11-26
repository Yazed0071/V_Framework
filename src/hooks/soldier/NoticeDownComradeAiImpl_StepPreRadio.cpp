#include <pch.h>
#include "NoticeDownComradeAiImpl_StepPreRadio.h"

#include <Windows.h>
#include <atomic>
#include <cmath>
#include <cstdint>

#include "CpRadioVariants.h"
#include "MissionCodeGuard.h"
#include "log.h"

namespace
{
    using Step_t = void(__fastcall*)(void* node, std::uint32_t agent, std::int32_t proc, std::uint8_t* knowledge);

    struct NodeSpec
    {
        std::uint64_t name      = 0;
        std::uint32_t knowledge = 0;
        std::uint32_t pad       = 0;
    };
    using GetNodeSpec_t = void(__fastcall*)(void* node, NodeSpec* spec);

    constexpr std::uint64_t kDownComradeNode  = 0x45871F23A01ull;   // NoticeDownComradeAi
    constexpr std::size_t   kNodeGetSpec      = 0;
    constexpr std::size_t   kNodePreUpdate    = 5;      // GetSpec, ShouldWake, ShouldSleep, Wake, Sleep, PreUpdate
    constexpr std::size_t   kScanBytes        = 0x60;   // its "lea r14, [step table]" sits 0x42 in
    constexpr std::size_t   kStepEntrySize    = 0x10;   // step function, then its this-adjust
    constexpr std::size_t   kPreRadioStep     = 2;
    constexpr std::size_t   kMoveStep         = 5;      // walks him to the body; only the check's leader goes on to wake or check him
    constexpr std::size_t   kEndStep          = 0xF;    // re::voidreturn in every node's table
    constexpr std::size_t   kKnowledgeComrade = 0x26;   // the downed comrade's GameObjectId
    constexpr std::size_t   kKnowledgeRole    = 0x30;   // Aware caches his seat in the check here, 1 leads it
    constexpr std::size_t   kKnowledgeStep    = 0x31;
    constexpr std::size_t   kKnowledgeNotice  = 0x32;
    constexpr std::size_t   kKnowledgeCalled  = 0x33;   // bit 0: his 0x0C call was asked for
    constexpr std::uint8_t  kMovePauseStep    = 4;      // vanilla's next step once his 0x0C call is done: on to the body
    constexpr std::uint8_t  kWaitStep         = 0xE;    // vanilla's own stand-and-watch
    constexpr std::uint8_t  kDoneStep         = 0xF;    // ShouldSleep ends the node on it: back to his route
    constexpr std::uint8_t  kFirstSight       = 0x1E;
    constexpr std::uint8_t  kLeader           = 1;
    constexpr std::int32_t  kProcUpdate       = 3;

    enum Hooked : std::size_t { kHookPreRadio, kHookMove, kHookWait, kHookCount };
    constexpr std::size_t kHookedSteps[kHookCount] = { kPreRadioStep, kMoveStep, kWaitStep };

    std::atomic<bool>    g_Tried{ false };
    std::atomic<Step_t*> g_Entry[kHookCount] = {};
    Step_t               g_Orig[kHookCount]  = {};
    thread_local std::uint8_t* t_Knowledge = nullptr;   // while his PreRadio runs: the 0x0C request comes from inside it
    thread_local std::uint32_t t_Agent     = 0xFFFFFFFF;

    void __fastcall hk_StepPreRadio(void* node, std::uint32_t agent, std::int32_t proc, std::uint8_t* knowledge)
    {
        const bool update = proc == kProcUpdate && !MissionCodeGuard::ShouldBypassHooks();
        if (update && knowledge[kKnowledgeNotice] == kFirstSight && CpRadioVariants_IsWaker(agent))
            knowledge[kKnowledgeCalled] |= 1;   // the survey team's man: no "Got something worth checking", straight to the body
        std::uint8_t* const outerKnowledge = t_Knowledge;
        const std::uint32_t outerAgent = t_Agent;
        t_Knowledge = knowledge;
        t_Agent = agent;
        g_Orig[kHookPreRadio](node, agent, proc, knowledge);
        t_Knowledge = outerKnowledge;
        t_Agent = outerAgent;

        if (!update)
            return;
        if (knowledge[kKnowledgeStep] == kMovePauseStep && knowledge[kKnowledgeNotice] == kFirstSight && CpRadioVariants_HoldFinder(agent))
        {
            knowledge[kKnowledgeStep] = kWaitStep;   // PreUpdate sees the change: Leave(PreRadio), Enter(Wait)
        }
    }

    void __fastcall hk_StepMove(void* node, std::uint32_t agent, std::int32_t proc, std::uint8_t* knowledge)
    {
        if (proc == kProcUpdate && !MissionCodeGuard::ShouldBypassHooks() && CpRadioVariants_IsWaker(agent))
            knowledge[kKnowledgeRole] = kLeader;   // user: the team wakes or checks him, the finder who holds the lead only watches
        g_Orig[kHookMove](node, agent, proc, knowledge);
    }

    void __fastcall hk_StepWait(void* node, std::uint32_t agent, std::int32_t proc, std::uint8_t* knowledge)
    {
        if (proc == kProcUpdate && !MissionCodeGuard::ShouldBypassHooks())
        {
            const FinderWait wait = CpRadioVariants_FinderWait(agent);
            if (wait == FinderWait::Hold)
                return;   // none of Wait's own ways out: 60 s, his comrade getting up, a noise
            if (wait == FinderWait::Release)
            {
                knowledge[kKnowledgeStep] = kDoneStep;
                return;
            }
        }
        g_Orig[kHookWait](node, agent, proc, knowledge);
    }

    const Step_t kHooks[kHookCount] = { &hk_StepPreRadio, &hk_StepMove, &hk_StepWait };

    bool IsDownComradeNode(void* node)
    {
        NodeSpec spec;
        __try
        {
            const auto getSpec = reinterpret_cast<GetNodeSpec_t>((*static_cast<void***>(node))[kNodeGetSpec]);
            getSpec(node, &spec);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
        return (spec.name & 0xFFFFFFFFFFFFull) == kDownComradeNode;
    }

    std::uint8_t* StepTableOf(void* node)
    {
        __try
        {
            const std::uint8_t* preUpdate = static_cast<const std::uint8_t*>((*static_cast<void***>(node))[kNodePreUpdate]);
            for (std::size_t i = 0; i + 7 <= kScanBytes; ++i)
            {
                if (preUpdate[i] == 0x4C && preUpdate[i + 1] == 0x8D && preUpdate[i + 2] == 0x35)   // lea r14, [rip + disp32]
                {
                    const std::int32_t disp = *reinterpret_cast<const std::int32_t*>(preUpdate + i + 3);
                    std::uint8_t* table = const_cast<std::uint8_t*>(preUpdate + i + 7 + disp);
                    const std::uint8_t* end = *reinterpret_cast<const std::uint8_t* const*>(table + kEndStep * kStepEntrySize);
                    if (!end || (end[0] != 0xC2 && end[0] != 0xC3))
                        return nullptr;
                    for (const std::size_t step : kHookedSteps)
                    {
                        const void* entry = *reinterpret_cast<void* const*>(table + step * kStepEntrySize);
                        if (!entry || entry == end)
                            return nullptr;
                    }
                    return table;
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
        return nullptr;
    }

    bool SwapEntry(Step_t* entry, Step_t hook, Step_t& original)
    {
        DWORD oldProtect = 0;
        if (!VirtualProtect(entry, sizeof(Step_t), PAGE_READWRITE, &oldProtect))
            return false;
        original = *entry;
        InterlockedExchangePointer(reinterpret_cast<void* volatile*>(entry), reinterpret_cast<void*>(hook));
        VirtualProtect(entry, sizeof(Step_t), oldProtect, &oldProtect);
        return true;
    }
}

void NoticeDownComradeAiImpl_StepPreRadio_OnNodeWake(void* node, std::uint32_t agent)
{
    if (!node || !IsDownComradeNode(node))
        return;
    CpRadioVariants_OnDownComradeNode(agent, true);
    if (g_Tried.load(std::memory_order_relaxed) || g_Tried.exchange(true))
        return;
    std::uint8_t* table = StepTableOf(node);
    if (!table)
    {
        LogError("CpRadioVariants: no step table behind the downed-comrade node, the finder won't hold and the team won't see to the body first");
        return;
    }
    for (std::size_t hooked = 0; hooked < kHookCount; ++hooked)
    {
        Step_t* entry = reinterpret_cast<Step_t*>(table + kHookedSteps[hooked] * kStepEntrySize);
        if (!SwapEntry(entry, kHooks[hooked], g_Orig[hooked]))
        {
            LogError("CpRadioVariants: the downed-comrade step table is not writable, the finder won't hold and the team won't see to the body first");
            return;
        }
        g_Entry[hooked].store(entry, std::memory_order_release);
    }
}

void NoticeDownComradeAiImpl_StepPreRadio_OnNodeSleep(void* node, std::uint32_t agent)
{
    if (node && IsDownComradeNode(node))
        CpRadioVariants_OnDownComradeNode(agent, false);
}

bool NoticeDownComradeAiImpl_FoundSpot(std::uint32_t soldierIndex, float out[3])
{
    if (!t_Knowledge || t_Agent != soldierIndex)
        return false;
    __try
    {
        const float* spot = reinterpret_cast<const float*>(t_Knowledge);
        out[0] = spot[0];
        out[1] = spot[1];
        out[2] = spot[2];
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
    return std::isfinite(out[0]) && std::isfinite(out[1]) && std::isfinite(out[2]);
}

std::uint16_t NoticeDownComradeAiImpl_FoundComrade(std::uint32_t soldierIndex)
{
    if (!t_Knowledge || t_Agent != soldierIndex)
        return 0xFFFF;
    __try
    {
        return *reinterpret_cast<const std::uint16_t*>(t_Knowledge + kKnowledgeComrade);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return 0xFFFF;
    }
}

void Uninstall_NoticeDownComradeAiImpl_StepPreRadio()
{
    for (std::size_t hooked = 0; hooked < kHookCount; ++hooked)
    {
        Step_t* entry = g_Entry[hooked].exchange(nullptr, std::memory_order_acq_rel);
        if (!entry || !g_Orig[hooked])
            continue;
        DWORD oldProtect = 0;
        if (VirtualProtect(entry, sizeof(Step_t), PAGE_READWRITE, &oldProtect))
        {
            void* const previous = InterlockedCompareExchangePointer(reinterpret_cast<void* volatile*>(entry), reinterpret_cast<void*>(g_Orig[hooked]), reinterpret_cast<void*>(kHooks[hooked]));
            if (previous != reinterpret_cast<void*>(kHooks[hooked]))
                LogWarn("CpRadioVariants: the downed-comrade step table entry %zu was re-pointed by another module, left as it is", hooked);
            VirtualProtect(entry, sizeof(Step_t), oldProtect, &oldProtect);
        }
    }
    g_Tried.store(false, std::memory_order_relaxed);
}
