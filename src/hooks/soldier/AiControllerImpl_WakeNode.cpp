#include "pch.h"
#include "AiControllerImpl_WakeNode.h"

#include <Windows.h>
#include <cstddef>
#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"
#include "RadioControllerImpl_StepRadioWaitResponse.h"
#include "Soldier2SoundControllerImpl_CallVoiceWithoutSync.h"
#include "SoldierStinkVoice.h"

namespace
{
    using SoldierNode = CpRadioResponse::SoldierNode;

    struct NodeSpec
    {
        std::uint64_t name      = 0;
        std::uint32_t knowledge = 0;
        std::uint32_t pad       = 0;
    };

    using WakeNode_t    = void(__fastcall*)(std::uint8_t* controller, std::uint32_t agent, std::int32_t level, std::uint32_t node);
    using SleepNode_t   = void(__fastcall*)(std::uint8_t* controller, std::uint32_t agent, std::int32_t level, std::int32_t reason);
    using GetNodeSpec_t = void(__fastcall*)(void* node, NodeSpec* spec);

    constexpr std::size_t kControllerNodes   = 0x90;   // the AI node of each node index
    constexpr std::size_t kControllerLevels  = 0x80;   // node levels per agent
    constexpr std::size_t kControllerCurrent = 0xA8;   // node index per agent and level, 0xFFFF when empty
    constexpr std::size_t kNodeGetSpec       = 0;      // AiNodeInterface vtable slot
    constexpr std::uint16_t kNoNode          = 0xFFFF;
    constexpr std::uint32_t kMostAgents      = 0x200;  // soldier indices

    struct WatchedNode
    {
        std::uint64_t name;
        std::uint32_t knowledge;   // other controllers reuse some names with their own knowledge size
        SoldierNode   kind;
    };

    constexpr WatchedNode kWatched[] = {
        { 0x4A5EBCF8D172ull, 0x100040, SoldierNode::Check },   // NoticeLightOffAi: the power check
        { 0x872F44D9BF23ull, 0x100040, SoldierNode::Check },   // NoticeCameraAi
        { 0x31A59906E993ull, 0x100040, SoldierNode::Check },   // soldier NoticeNoiseAi
        { 0xE13359FA3752ull, 0x100040, SoldierNode::Check },   // NoticeIndisAi: something half-seen
        { 0xCD47A6DAE772ull, 0x80028,  SoldierNode::Held },    // CqcedAi
        { 0xCD47A6DAE772ull, 0x80030,  SoldierNode::Held },    // HoldupAi
    };

    WakeNode_t  g_OrigWakeNode    = nullptr;
    SleepNode_t g_OrigSleepNode   = nullptr;
    void*       g_WakeNodeTarget  = nullptr;
    void*       g_SleepNodeTarget = nullptr;

    template <typename T>
    T& At(std::uint8_t* base, std::size_t offset)
    {
        return *reinterpret_cast<T*>(base + offset);
    }

    bool KindOf(void* node, SoldierNode& kind)
    {
        if (!node)
            return false;
        NodeSpec spec;
        __try
        {
            const auto getSpec = reinterpret_cast<GetNodeSpec_t>((*reinterpret_cast<void***>(node))[kNodeGetSpec]);
            getSpec(node, &spec);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
        for (const WatchedNode& watched : kWatched)
        {
            if (watched.name == (spec.name & 0xFFFFFFFFFFFFull) && watched.knowledge == spec.knowledge)
            {
                kind = watched.kind;
                return true;
            }
        }
        return false;
    }

    void* NodeAt(std::uint8_t* controller, std::uint32_t node)
    {
        return At<void**>(controller, kControllerNodes)[node];
    }

    void __fastcall hk_WakeNode(std::uint8_t* controller, std::uint32_t agent, std::int32_t level, std::uint32_t node)
    {
        g_OrigWakeNode(controller, agent, level, node);

        SoldierNode kind;
        if (agent < kMostAgents && node != kNoNode && KindOf(NodeAt(controller, node), kind))
        {
            CpRadioResponse::OnSoldierNode(static_cast<std::uint16_t>(agent), kind, true);
            if (kind == SoldierNode::Held)
            {
                SoldierVoiceSwap_OnHeld(static_cast<std::uint16_t>(agent), true);
                SoldierStinkVoice_OnHeld(static_cast<std::uint16_t>(agent));
            }
        }
    }

    void __fastcall hk_SleepNode(std::uint8_t* controller, std::uint32_t agent, std::int32_t level, std::int32_t reason)
    {
        const std::uint32_t levels = At<std::uint32_t>(controller, kControllerLevels);
        const std::uint16_t node = At<std::uint16_t*>(controller, kControllerCurrent)[agent * levels + level];   // gone once it sleeps
        SoldierNode kind;
        const bool watched = agent < kMostAgents && node != kNoNode && KindOf(NodeAt(controller, node), kind);

        g_OrigSleepNode(controller, agent, level, reason);

        if (watched)
        {
            CpRadioResponse::OnSoldierNode(static_cast<std::uint16_t>(agent), kind, false);
            if (kind == SoldierNode::Held)
                SoldierVoiceSwap_OnHeld(static_cast<std::uint16_t>(agent), false);
        }
    }

    bool Hook(std::uintptr_t address, void* detour, void** original, void*& target, const char* name)
    {
        void* resolved = ResolveGameAddress(address);
        if (!CreateAndEnableHook(resolved, detour, original))
        {
            LogError("CpRadioResponse: %s hook failed at %p, no SITREP for soldiers taken out on a check", name, resolved);
            return false;
        }
        target = resolved;
        return true;
    }

    void RemoveHook(void*& target)
    {
        if (target)
            DisableAndRemoveHook(target);
        target = nullptr;
    }
}

bool Install_AiControllerImpl_WakeNode_Hooks()
{
    if (g_WakeNodeTarget)
        return true;
    if (!gAddr.AiControllerImpl_WakeNode || !gAddr.AiControllerImpl_SleepNode)
    {
        LogError("CpRadioResponse: AI node addresses missing for %s, no SITREP or Lie to them for soldiers on a check",
                 GetGameBuildName(gGameBuild));
        return false;
    }

    if (!Hook(gAddr.AiControllerImpl_SleepNode, reinterpret_cast<void*>(&hk_SleepNode),
              reinterpret_cast<void**>(&g_OrigSleepNode), g_SleepNodeTarget, "AiControllerImpl::SleepNode")
        || !Hook(gAddr.AiControllerImpl_WakeNode, reinterpret_cast<void*>(&hk_WakeNode),
                 reinterpret_cast<void**>(&g_OrigWakeNode), g_WakeNodeTarget, "AiControllerImpl::WakeNode"))
    {
        RemoveHook(g_SleepNodeTarget);
        return false;
    }
    return true;
}

bool AiControllerImpl_WakeNode_IsWatching()
{
    return g_WakeNodeTarget != nullptr && g_SleepNodeTarget != nullptr;
}

void Uninstall_AiControllerImpl_WakeNode_Hooks()
{
    RemoveHook(g_WakeNodeTarget);
    RemoveHook(g_SleepNodeTarget);
}
