#include "pch.h"

#include <Windows.h>
#include <cstdint>

#include "AddressSet.h"
#include "CoreAiImpl_SetSpecialActionParam.h"
#include "HookUtils.h"
#include "HostageGender.h"
#include "MissionCodeGuard.h"
#include "SpecialActionImpl_State_SpecialIdle.h"
#include "log.h"

namespace
{
    struct SpecialActionParam
    {
        std::uint64_t value;
        std::uint32_t action;
        std::uint32_t commandId;
        float         interpFrames;
        float         startPosition[3];
        float         startAngleY;
        std::uint32_t serial;
        std::uint32_t flags;
        std::uint32_t pad;
    };
    static_assert(sizeof(SpecialActionParam) == 0x30, "the game copies the param as 0x30 bytes");

    using SetSpecialActionParam_t = void(__fastcall*)(void* coreAi, std::uint32_t index, const SpecialActionParam* param);

    struct Pending
    {
        bool          armed;
        std::uint32_t gameObjectId;
        bool          struggle;
        bool          ever;
    };

    constexpr std::uint32_t kAutoFinish        = 1u << 1;
    constexpr std::uint32_t kFinishInterrupted = 1u << 17; // enableFinishInterrupted: carry or damage ends the action
    constexpr std::uint32_t kIndexMask         = 0x1FFu;
    constexpr int           kTypeShift         = 9;
    constexpr std::uint32_t kTppHostage2       = 3; // the only hostage type whose motion graph has the struggle states
    constexpr std::size_t   kWorkArray         = 0x40;
    constexpr std::size_t   kWorkStride        = 0x80;
    constexpr std::size_t   kWorkParam         = 0x28;

    SetSpecialActionParam_t g_Orig = nullptr;
    void*                   g_Target = nullptr;
    Pending                 g_Pending{};

    SpecialActionParam* LiveStruggleParam(void* coreAi, std::uint32_t index)
    {
        __try
        {
            const std::uintptr_t work = *reinterpret_cast<std::uintptr_t*>(reinterpret_cast<std::uintptr_t>(coreAi) + kWorkArray);
            if (!work)
                return nullptr;
            auto* param = reinterpret_cast<SpecialActionParam*>(work + static_cast<std::uintptr_t>(index) * kWorkStride + kWorkParam);
            if (param->action != HostageStruggleTags::kPlayState || (param->value & HostageStruggleTags::kMask) != HostageStruggleTags::kStart)
                return nullptr;
            return param;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    void __fastcall hk_SetSpecialActionParam(void* coreAi, std::uint32_t index, const SpecialActionParam* param)
    {
        if (!g_Pending.armed || !param || param->action != HostageStruggleTags::kPlayState || index != (g_Pending.gameObjectId & kIndexMask))
        {
            g_Orig(coreAi, index, param);
            return;
        }
        g_Pending.armed = false;
        if (SpecialActionParam* live = LiveStruggleParam(coreAi, index))
        {
            if (g_Pending.struggle)
                live->flags = g_Pending.ever ? (live->flags & ~kFinishInterrupted) : (live->flags | kFinishInterrupted);
            else
            {
                SpecialActionParam end = *param;
                end.value = HostageStruggleTags::kEnd;
                end.flags |= kAutoFinish | kFinishInterrupted;
                g_Orig(coreAi, index, &end);
            }
            return;
        }
        if (!g_Pending.struggle)
            return;
        SpecialActionParam start = *param;
        start.value = HostageStruggleTags::kStart;
        start.flags &= ~(kAutoFinish | kFinishInterrupted);
        if (!g_Pending.ever)
            start.flags |= kFinishInterrupted;
        g_Orig(coreAi, index, &start);
    }
}

bool Arm_HostageStruggle(std::uint32_t gameObjectId, bool struggle, bool ever)
{
    if (!g_Target || !Is_SpecialActionImpl_State_SpecialIdle_Hooked() || MissionCodeGuard::ShouldBypassHooks())
        return false;
    if ((gameObjectId >> kTypeShift) != kTppHostage2)
    {
        LogWarn("SetForceStruggle: 0x%X is not a TppHostage2", gameObjectId);
        return false;
    }
    if (HostageGender::Read(gameObjectId) == HostageGender::kChild)
    {
        LogWarn("SetForceStruggle: hostage 0x%X is a child, the struggle motions are adult only", gameObjectId);
        return false;
    }
    g_Pending = { true, gameObjectId, struggle, ever };
    return true;
}

void Disarm_HostageStruggle()
{
    g_Pending.armed = false;
}

bool Install_CoreAiImpl_SetSpecialActionParam_Hook()
{
    if (g_Target)
        return true;
    if (!gAddr.HostageCoreAiImpl_SetSpecialActionParam)
    {
        LogError("HostageStruggle: CoreAiImpl::SetSpecialActionParam address missing for this build, SetForceStruggle off");
        return true;
    }
    void* target = ResolveGameAddress(gAddr.HostageCoreAiImpl_SetSpecialActionParam);
    if (!target || !CreateAndEnableHook(target, reinterpret_cast<void*>(&hk_SetSpecialActionParam), reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("HostageStruggle: SetSpecialActionParam hook failed, SetForceStruggle off");
        return true;
    }
    g_Target = target;
    return true;
}

bool Uninstall_CoreAiImpl_SetSpecialActionParam_Hook()
{
    if (g_Target)
        DisableAndRemoveHook(g_Target);
    g_Target = nullptr;
    g_Orig = nullptr;
    g_Pending.armed = false;
    return true;
}
