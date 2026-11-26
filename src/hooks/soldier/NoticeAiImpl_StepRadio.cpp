#include <pch.h>
#include "NoticeAiImpl_StepRadio.h"

#include <Windows.h>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

#include "AddressSet.h"
#include "CpRadioVariants.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    using Step_t     = void(__fastcall*)(void* node, std::uint32_t agent, std::int32_t proc, std::uint8_t* knowledge);
    using GetNoise_t = const std::uint8_t* (__fastcall*)(void* noticeController, std::uint32_t agent);   // GetNoticeInfo has the same shape
    using GetQuarkSystemTable_t = std::uint8_t* (__fastcall*)();
    using GetCommunication_t    = const std::int32_t* (__fastcall*)(void* vehicles, std::int32_t* result, std::uint8_t** info,
                                                                    std::uint32_t index);

    struct NodeSpec
    {
        std::uint64_t name      = 0;
        std::uint32_t knowledge = 0;
        std::uint32_t pad       = 0;
    };
    using GetNodeSpec_t = void(__fastcall*)(void* node, NodeSpec* spec);

    enum Node : std::size_t { kNoiseThreatNode, kDecoyNode, kNoiseEmergencyNode, kVehicleNode, kNodeCount };
    struct RadioStep
    {
        std::uint64_t name;        // GetAiNodeSpec: StrCode64 of "<Node>AiKnowledge", the same on every build
        std::size_t   scanBytes;
        std::size_t   radioStep;
        std::size_t   endStep;     // re::voidreturn
        NoticeRadio   radio;
        const char*   what;
    };
    constexpr RadioStep kRadioSteps[kNodeCount] = {
        { 0x5D949D15D50Aull, 0xC0, 7,   0xF,  NoticeRadio::VehicleBlast,   "NoticeNoiseThreatAi StepRadio" },   // its lea r14 sits 0x9A in
        { 0x18BCC09E2A8Dull, 0x60, 0xF, 0x14, NoticeRadio::Decoy,          "NoticeDecoyAi StepReport" },
        { 0x0D6D4E095D86ull, 0x60, 9,   0xE,  NoticeRadio::NoiseEmergency, "NoticeNoiseEmergencyAi StepRadio" },
        { 0x8CA15AF253E2ull, 0x60, 2,   3,    NoticeRadio::Vehicle,        "NoticeVehicleAi StepRadio" },
    };

    constexpr std::size_t   kNodeGetSpec      = 0;
    constexpr std::size_t   kNodePreUpdate    = 5;
    constexpr std::size_t   kStepEntrySize    = 0x10;    // step function, then its this-adjust
    constexpr std::size_t   kFingerprintBytes = 0xC00;
    constexpr std::uint8_t  kRequestRadio[]   = { 0xFF, 0x90, 0xC0, 0x01, 0x00, 0x00 };   // call [rax+0x1C0], RequestRadioByMember
    constexpr std::int32_t  kProcLeave        = 1;
    constexpr std::int32_t  kProcPreUpdate    = 3;

    constexpr std::size_t   kNodeContext      = 0x70;
    constexpr std::size_t   kWokenNodeContext = 0x50;    // the same context, from the node WakeNode hands out (0x20 above the steps' one)
    constexpr std::size_t   kContextNotice    = 0x68;    // NoticeControllerImpl
    constexpr std::size_t   kGetNoticeSlot    = 2;       // what the node woke for: Wake reads its noise at +8
    constexpr std::size_t   kGetNoiseSlot     = 3;       // gone again by the time he radios
    constexpr std::size_t   kNoticeNoise      = 0x08;
    constexpr std::uint8_t  kNoiseThreatNotice = 0x13;
    constexpr std::size_t   kNoiseType        = 0x10;
    constexpr std::uint8_t  kVehicleBlastNoise = 0x4C;   // vehicles, walker gears and the enemy heli all blow up with it
    constexpr std::size_t   kMostAgents       = 0x200;

    constexpr std::size_t   kQuarkTableApplication = 0x98;
    constexpr std::size_t   kApplicationVehicles   = 0x2A8;   // vehicle::impl::SystemImpl, as NoticeVehicleAi looks vehicles up
    constexpr std::size_t   kGetCommunicationSlot  = 3;
    constexpr std::uint32_t kVehicleSlots          = 0x200;   // checked against the vehicle count and in-use bits inside
    constexpr std::size_t   kVehiclePosition       = 0x20;
    constexpr std::size_t   kVehicleLife           = 0x70;
    constexpr std::uint32_t kVehicleWrecked        = 0x800004;   // its life ran out, or it rusted through
    constexpr float         kWreckRange            = 15.0f;      // CheckVehicleNotice's own reach for a wreck
    constexpr std::size_t   kHeardNoiseAt     = 0x10;    // UpdateNotice keeps the noise's own position here, his target's stays at +0
    constexpr std::size_t   kThreatFlags      = 0x3C;    // ORed up from every noise he heard while the node runs
    constexpr std::size_t   kThreatBits       = 0x3E;
    constexpr std::uint8_t  kSendPointBit     = 0x08;    // StepRadio files a kind-2 investigation point once his line is done

    struct Speaking
    {
        NoticeRadio   radio     = NoticeRadio::None;
        void*         node      = nullptr;
        std::uint32_t agent     = 0xFFFFFFFF;
        std::uint8_t* knowledge = nullptr;
    };
    thread_local Speaking t_Speaking{};

    std::atomic<bool>    g_Tried[kNodeCount] = {};
    std::atomic<Step_t*> g_Entry[kNodeCount] = {};
    Step_t               g_Orig[kNodeCount]  = {};
    std::array<std::atomic<std::int16_t>, kMostAgents> g_WokeForNoise{};   // the noise each soldier's NoiseThreat node last woke for, plus one

    void RunStep(std::size_t which, void* node, std::uint32_t agent, std::int32_t proc, std::uint8_t* knowledge)
    {
        const bool teamGoes = which == kNoiseThreatNode && (proc == kProcPreUpdate || proc == kProcLeave) && CpRadioVariants_TakesVehicleBlast(agent);
        if (teamGoes)
            knowledge[kThreatBits] &= static_cast<std::uint8_t>(~kSendPointBit);   // PreUpdate sets it again every frame; the team has the point
        const Speaking outer = t_Speaking;
        t_Speaking = Speaking{ kRadioSteps[which].radio, node, agent, knowledge };
        g_Orig[which](node, agent, proc, knowledge);
        t_Speaking = outer;
        if (teamGoes && proc == kProcLeave)
            CpRadioVariants_VehicleBlastReported(agent);   // his radio step is over: his next report is his own again
    }

    template <std::size_t Which>
    void __fastcall hk_RadioStep(void* node, std::uint32_t agent, std::int32_t proc, std::uint8_t* knowledge)
    {
        RunStep(Which, node, agent, proc, knowledge);
    }
    const Step_t kHooks[kNodeCount] = { &hk_RadioStep<kNoiseThreatNode>, &hk_RadioStep<kDecoyNode>, &hk_RadioStep<kNoiseEmergencyNode>,
                                        &hk_RadioStep<kVehicleNode> };

    std::size_t NodeOf(void* node)
    {
        NodeSpec spec;
        __try
        {
            const auto getSpec = reinterpret_cast<GetNodeSpec_t>((*static_cast<void***>(node))[kNodeGetSpec]);
            getSpec(node, &spec);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return kNodeCount;
        }
        for (std::size_t i = 0; i < kNodeCount; ++i)
        {
            if ((spec.name & 0xFFFFFFFFFFFFull) == kRadioSteps[i].name)
                return i;
        }
        return kNodeCount;
    }

    bool AsksForRadio(const std::uint8_t* step)
    {
        for (std::size_t i = 0; i + sizeof(kRequestRadio) <= kFingerprintBytes; ++i)
        {
            std::size_t matched = 0;
            while (matched < sizeof(kRequestRadio) && step[i + matched] == kRequestRadio[matched])
                ++matched;
            if (matched == sizeof(kRequestRadio))
                return true;
        }
        return false;
    }

    std::uint8_t* StepTableOf(void* node, const RadioStep& radioStep)
    {
        __try
        {
            const std::uint8_t* preUpdate = static_cast<const std::uint8_t*>((*static_cast<void***>(node))[kNodePreUpdate]);
            for (std::size_t i = 0; i + 7 <= radioStep.scanBytes; ++i)
            {
                if (preUpdate[i] != 0x4C || preUpdate[i + 1] != 0x8D || preUpdate[i + 2] != 0x35)   // lea r14, [rip + disp32]
                    continue;
                const std::int32_t disp = *reinterpret_cast<const std::int32_t*>(preUpdate + i + 3);
                std::uint8_t* table = const_cast<std::uint8_t*>(preUpdate + i + 7 + disp);
                const std::uint8_t* end = *reinterpret_cast<const std::uint8_t* const*>(table + radioStep.endStep * kStepEntrySize);
                const std::uint8_t* radio = *reinterpret_cast<const std::uint8_t* const*>(table + radioStep.radioStep * kStepEntrySize);
                if (!end || (end[0] != 0xC2 && end[0] != 0xC3) || !radio || radio == end || !AsksForRadio(radio))
                    return nullptr;
                return table;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
        return nullptr;
    }

    std::int32_t NoiseHeard(const Speaking& speaking)   // the noise his notice is about now, -1 for none
    {
        __try
        {
            std::uint8_t* context = *reinterpret_cast<std::uint8_t* const*>(static_cast<std::uint8_t*>(speaking.node) + kNodeContext);
            void* noticeController = *reinterpret_cast<void* const*>(context + kContextNotice);
            const auto getNoise = (*reinterpret_cast<GetNoise_t* const*>(noticeController))[kGetNoiseSlot];
            const std::uint8_t* noise = getNoise(noticeController, speaking.agent);
            return noise ? noise[kNoiseType] : -1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return -1;
        }
    }

    std::int32_t NoiseWokenFor(void* node, std::uint32_t agent)   // right after Wake, while the notice still holds the noise
    {
        __try
        {
            std::uint8_t* context = *reinterpret_cast<std::uint8_t* const*>(static_cast<std::uint8_t*>(node) + kWokenNodeContext);
            void* noticeController = *reinterpret_cast<void* const*>(context + kContextNotice);
            const auto getNotice = (*reinterpret_cast<GetNoise_t* const*>(noticeController))[kGetNoticeSlot];
            const std::uint8_t* notice = getNotice(noticeController, agent);
            if (!notice || notice[0] != kNoiseThreatNotice)
                return -1;
            const std::uint8_t* noise = *reinterpret_cast<const std::uint8_t* const*>(notice + kNoticeNoise);
            return noise ? noise[kNoiseType] : -1;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return -1;
        }
    }

    bool WreckNear(const float* spot)   // C4 or a rocket: he reports the charge going off, the vehicle only blows seconds later
    {
        const auto quarkTable = reinterpret_cast<GetQuarkSystemTable_t>(ResolveGameAddress(gAddr.Fox_GetQuarkSystemTable));
        if (!quarkTable)
            return false;
        __try
        {
            std::uint8_t* table = quarkTable();
            std::uint8_t* application = table ? *reinterpret_cast<std::uint8_t* const*>(table + kQuarkTableApplication) : nullptr;
            void* vehicles = application ? *reinterpret_cast<void* const*>(application + kApplicationVehicles) : nullptr;
            if (!vehicles)
                return false;
            const auto getCommunication = (*reinterpret_cast<GetCommunication_t* const*>(vehicles))[kGetCommunicationSlot];
            for (std::uint32_t index = 0; index < kVehicleSlots; ++index)
            {
                std::int32_t result = -1;
                std::uint8_t* info = nullptr;
                const std::int32_t* found = getCommunication(vehicles, &result, &info, index);
                if (!found || *found < 0 || !info || !(*reinterpret_cast<const std::uint32_t*>(info + kVehicleLife) & kVehicleWrecked))
                    continue;
                const float* at = reinterpret_cast<const float*>(info + kVehiclePosition);
                const float dx = at[0] - spot[0];
                const float dy = at[1] - spot[1];
                const float dz = at[2] - spot[2];
                if (dx * dx + dy * dy + dz * dz < kWreckRange * kWreckRange)
                    return true;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
        return false;
    }

    std::int32_t NoiseReported(const Speaking& speaking)
    {
        const std::int32_t heard = NoiseHeard(speaking);
        if (heard >= 0 || speaking.agent >= kMostAgents)
            return heard;
        return g_WokeForNoise[speaking.agent].load(std::memory_order_relaxed) - 1;
    }

    bool ReadSpot(const std::uint8_t* knowledge, float out[3])   // every one of these nodes keeps its target's position first
    {
        __try
        {
            const float* spot = reinterpret_cast<const float*>(knowledge);
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
}

void NoticeAiImpl_StepRadio_OnNodeWake(void* node, std::uint32_t agent)
{
    if (!node)
        return;
    const std::size_t which = NodeOf(node);
    if (which == kNoiseThreatNode && agent < kMostAgents)
        g_WokeForNoise[agent].store(static_cast<std::int16_t>(NoiseWokenFor(node, agent) + 1), std::memory_order_relaxed);
    if (which == kNodeCount || g_Tried[which].load(std::memory_order_relaxed) || g_Tried[which].exchange(true))
        return;
    const RadioStep& radioStep = kRadioSteps[which];
    std::uint8_t* table = StepTableOf(node, radioStep);
    if (!table)
    {
        LogError("CpRadioVariants: no %s behind its node, its reports bring no survey team", radioStep.what);
        return;
    }
    Step_t* entry = reinterpret_cast<Step_t*>(table + radioStep.radioStep * kStepEntrySize);
    DWORD oldProtect = 0;
    if (!VirtualProtect(entry, sizeof(Step_t), PAGE_READWRITE, &oldProtect))
    {
        LogError("CpRadioVariants: the %s step table is not writable, its reports bring no survey team", radioStep.what);
        return;
    }
    if (*entry == kHooks[which])   // already ours, saving it would make the step call itself
    {
        VirtualProtect(entry, sizeof(Step_t), oldProtect, &oldProtect);
        return;
    }
    g_Orig[which] = *entry;
    InterlockedExchangePointer(reinterpret_cast<void* volatile*>(entry), reinterpret_cast<void*>(kHooks[which]));
    VirtualProtect(entry, sizeof(Step_t), oldProtect, &oldProtect);
    g_Entry[which].store(entry, std::memory_order_release);
}

NoticeRadio NoticeAiImpl_RadioFrom(std::uint32_t soldierIndex, float spot[3])
{
    const Speaking speaking = t_Speaking;
    if (speaking.radio == NoticeRadio::None || speaking.agent != soldierIndex || !speaking.knowledge)
        return NoticeRadio::None;
    if (speaking.radio == NoticeRadio::Vehicle)
        return NoticeRadio::Vehicle;
    if (!ReadSpot(speaking.knowledge, spot)
        || (speaking.radio == NoticeRadio::VehicleBlast && NoiseReported(speaking) != kVehicleBlastNoise && !WreckNear(spot)))
        return NoticeRadio::None;
    return speaking.radio;
}

bool NoticeAiImpl_ThreatNoise(std::uint32_t soldierIndex, std::uint16_t& flags, std::int32_t& noise, bool& wreck)
{
    const Speaking speaking = t_Speaking;
    float spot[3] = {};
    if (speaking.radio != NoticeRadio::VehicleBlast || speaking.agent != soldierIndex || !speaking.knowledge)
        return false;
    flags = *reinterpret_cast<const std::uint16_t*>(speaking.knowledge + kThreatFlags);
    noise = NoiseReported(speaking);
    wreck = ReadSpot(speaking.knowledge, spot) && WreckNear(spot);
    return true;
}

bool NoticeAiImpl_HeardNoiseAt(std::uint32_t soldierIndex, float spot[3])
{
    const Speaking speaking = t_Speaking;
    if (speaking.radio != NoticeRadio::VehicleBlast || speaking.agent != soldierIndex || !speaking.knowledge)
        return false;
    return ReadSpot(speaking.knowledge + kHeardNoiseAt, spot);
}

void NoticeAiImpl_StepRadio_ClearForReload()
{
    for (auto& noise : g_WokeForNoise)
        noise.store(0, std::memory_order_relaxed);
}

void Uninstall_NoticeAiImpl_StepRadio()
{
    for (std::size_t which = 0; which < kNodeCount; ++which)
    {
        Step_t* entry = g_Entry[which].exchange(nullptr, std::memory_order_acq_rel);
        if (entry && g_Orig[which])
        {
            DWORD oldProtect = 0;
            if (!VirtualProtect(entry, sizeof(Step_t), PAGE_READWRITE, &oldProtect))
                continue;   // slot still holds our hook, keep g_Tried so it is not wrapped again
            void* const previous = InterlockedCompareExchangePointer(reinterpret_cast<void* volatile*>(entry), reinterpret_cast<void*>(g_Orig[which]), reinterpret_cast<void*>(kHooks[which]));
            VirtualProtect(entry, sizeof(Step_t), oldProtect, &oldProtect);
            if (previous != reinterpret_cast<void*>(kHooks[which]))
                continue;   // another module wrapped the slot after us, leave its wrapper alone
        }
        g_Tried[which].store(false, std::memory_order_relaxed);
    }
}
