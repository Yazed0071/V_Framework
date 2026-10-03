#include "pch.h"

#include <Windows.h>
#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"
#include "PhaseSneakAiImpl_PreUpdate.h"

namespace
{
    using PreUpdate_t = void(__fastcall*)(void* self,
                                          std::uint32_t slot,
                                          std::int64_t* aiContext);

    using StepFunc_t = void(__fastcall*)(void* self,
                                         std::uint32_t slot,
                                         std::uint32_t stepProc,
                                         void* knowledge,
                                         void* unused);

    static PreUpdate_t g_OrigPreUpdate = nullptr;

    static constexpr std::uint32_t kStepProcEnter = 0;
    static constexpr std::uint32_t kStepProcExit  = 1;
    static constexpr std::size_t   kStepEntrySize = 16;
    static constexpr std::size_t   kStepFuncOffset   = 0;
    static constexpr std::size_t   kStepClassOffset  = 8;
    static constexpr std::int64_t  kBaseAdjustment   = -0x20;

    static bool DispatchStepProc(void* selfPreUpdateArg,
                                 std::uint32_t slot,
                                 std::uint32_t stepProc,
                                 void* knowledge,
                                 std::uint8_t phase,
                                 const char* label)
    {
        auto tableBase = reinterpret_cast<std::uint8_t*>(
            ResolveGameAddress(gAddr.Sahelan_PhaseSneakAiImpl_StepFuncsTable));
        if (!tableBase)
        {
            LogError("Sahelan: StepFuncsTable address missing, %s skipped for phase %u",
                label, static_cast<unsigned>(phase));
            return false;
        }

        const std::size_t entryOffset = static_cast<std::size_t>(phase) * kStepEntrySize;
        auto stepFunc = *reinterpret_cast<StepFunc_t*>(tableBase + entryOffset + kStepFuncOffset);
        const std::int32_t classOffset = *reinterpret_cast<std::int32_t*>(
            tableBase + entryOffset + kStepClassOffset);

        if (!stepFunc)
        {
            LogWarn("Sahelan: phase %u has no step func, %s skipped",
                static_cast<unsigned>(phase), label);
            return false;
        }

        auto baseThis = reinterpret_cast<std::uint8_t*>(selfPreUpdateArg) + kBaseAdjustment;
        auto stepThis = baseThis + classOffset;

        __try
        {
            stepFunc(stepThis, slot, stepProc, knowledge, nullptr);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            LogWarn("Sahelan: %s faulted for phase %u, phase change incomplete",
                label, static_cast<unsigned>(phase));
            return false;
        }
    }

    static void __fastcall hk_PreUpdate(void* self,
                                        std::uint32_t slot,
                                        std::int64_t* aiContext)
    {
        if (g_OrigPreUpdate)
            g_OrigPreUpdate(self, slot, aiContext);

        if (!aiContext || !*aiContext)
            return;

        auto knowledge = reinterpret_cast<std::uint8_t*>(*aiContext);

        __try
        {
            SahelanPhaseForce::g_CurrentPhase.store(
                static_cast<std::int32_t>(knowledge[4]),
                std::memory_order_relaxed);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return;
        }

        const std::int32_t forced =
            SahelanPhaseForce::g_ForcedPhase.load(std::memory_order_relaxed);

        if (forced < 0 || forced > 0xFF)
            return;

        const std::uint8_t forcedByte = static_cast<std::uint8_t>(forced);
        const std::uint8_t currentByte = knowledge[4];

        if (currentByte == forcedByte)
        {
            SahelanPhaseForce::g_ForcedPhase.store(-1, std::memory_order_relaxed);
            return;
        }

        DispatchStepProc(self, slot, kStepProcExit,
                         knowledge, currentByte, "Phase Exit");

        __try
        {
            knowledge[4] = forcedByte;
            knowledge[5] = currentByte;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            LogWarn("Sahelan: phase write faulted, forced phase %u dropped",
                static_cast<unsigned>(forcedByte));
            SahelanPhaseForce::g_ForcedPhase.store(-1, std::memory_order_relaxed);
            return;
        }

        DispatchStepProc(self, slot, kStepProcEnter,
                         knowledge, forcedByte, "Phase Enter");

        SahelanPhaseForce::g_ForcedPhase.store(-1, std::memory_order_relaxed);
    }
}

bool Install_PhaseSneakAiImpl_PreUpdate_Hook()
{
    void* target = ResolveGameAddress(gAddr.Sahelan_PhaseSneakAiImpl_PreUpdate);
    const bool ok = target && CreateAndEnableHook(target,
        reinterpret_cast<void*>(&hk_PreUpdate),
        reinterpret_cast<void**>(&g_OrigPreUpdate));

    if (!target)
        LogError("Sahelan: PhaseSneakAiImpl::PreUpdate address missing, SetSahelanPhase off");
    else if (!ok)
        LogError("Sahelan: PhaseSneakAiImpl::PreUpdate hook failed, SetSahelanPhase off");
    return ok;
}

bool Uninstall_PhaseSneakAiImpl_PreUpdate_Hook()
{
    DisableAndRemoveHook(ResolveGameAddress(gAddr.Sahelan_PhaseSneakAiImpl_PreUpdate));
    g_OrigPreUpdate = nullptr;
    SahelanPhaseForce::g_ForcedPhase.store(-1, std::memory_order_relaxed);
    SahelanPhaseForce::g_CurrentPhase.store(-1, std::memory_order_relaxed);
    return true;
}

void Set_SahelanForcePhase(std::int32_t phase)
{
    if (phase < 0 || phase > 0xFF)
    {
        LogWarn("SetSahelanPhase: phase %d out of range 0..255, ignored", phase);
        return;
    }
    SahelanPhaseForce::g_ForcedPhase.store(phase, std::memory_order_relaxed);
}

std::int32_t Get_SahelanCurrentPhase()
{
    return SahelanPhaseForce::g_CurrentPhase.load(std::memory_order_relaxed);
}
