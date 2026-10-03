#include "pch.h"

#include <Windows.h>
#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "SpecialActionImpl_State_SpecialIdle.h"
#include "log.h"

namespace
{
    using StateFn_t         = void(__fastcall*)(void* self, std::uint32_t index, std::uint32_t proc, bool* restart);
    using CurrentStateTag_t = void(__fastcall*)(void* motion, std::uint64_t* tag, std::uint32_t index, std::uint32_t layer);
    using IsStateEnd_t      = bool(__fastcall*)(void* motion, std::uint32_t index, std::uint32_t layer);

    constexpr std::uint32_t kProcCheck = 0; // the per-frame pass that decides whether the state restarts
    constexpr std::size_t kMotion      = 0x80;
    constexpr std::size_t kWorkArray   = 0xA0;
    constexpr std::size_t kWorkBase    = 0xB0; // index of the first hostage in the work array
    constexpr std::size_t kWorkStride  = 0x58;
    constexpr std::size_t kWorkTag     = 0x10;
    constexpr std::size_t kWorkAction  = 0x18;
    constexpr std::size_t kCurrentStateTagSlot = 0x208 / 8;
    constexpr std::size_t kIsStateEndSlot = 0x228 / 8;

    StateFn_t g_Orig = nullptr;
    void*     g_Target = nullptr;

    bool StartJustEnded(void* self, std::uint32_t index, std::uint64_t*& workTag)
    {
        __try
        {
            const auto base = reinterpret_cast<std::uintptr_t>(self);
            const std::uint32_t first = *reinterpret_cast<std::uint32_t*>(base + kWorkBase);
            const std::uintptr_t work = *reinterpret_cast<std::uintptr_t*>(base + kWorkArray) +
                static_cast<std::uintptr_t>(index - first) * kWorkStride;
            if (*reinterpret_cast<std::uint32_t*>(work + kWorkAction) != HostageStruggleTags::kPlayState)
                return false;
            workTag = reinterpret_cast<std::uint64_t*>(work + kWorkTag);
            if ((*workTag & HostageStruggleTags::kMask) != HostageStruggleTags::kStart)
                return false;
            void* motion = *reinterpret_cast<void**>(base + kMotion);
            if (!motion)
                return false;
            void** vtable = *reinterpret_cast<void***>(motion);
            std::uint64_t current = 0;
            reinterpret_cast<CurrentStateTag_t>(vtable[kCurrentStateTagSlot])(motion, &current, index, 0);
            if ((current & HostageStruggleTags::kMask) != HostageStruggleTags::kStart) // the graph switches a frame after the request
                return false;
            return reinterpret_cast<IsStateEnd_t>(vtable[kIsStateEndSlot])(motion, index, 0);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void __fastcall hk_State_SpecialIdle(void* self, std::uint32_t index, std::uint32_t proc, bool* restart)
    {
        g_Orig(self, index, proc, restart);
        if (proc != kProcCheck || !restart || *restart || MissionCodeGuard::ShouldBypassHooks())
            return;
        std::uint64_t* workTag = nullptr;
        if (!StartJustEnded(self, index, workTag))
            return;
        *workTag = HostageStruggleTags::kLoop;
        *restart = true;
    }
}

bool Install_SpecialActionImpl_State_SpecialIdle_Hook()
{
    if (g_Target)
        return true;
    if (!gAddr.HostageSpecialActionImpl_State_SpecialIdle)
    {
        LogError("HostageStruggle: SpecialActionImpl::State_SpecialIdle address missing for this build, SetForceStruggle off");
        return true;
    }
    void* target = ResolveGameAddress(gAddr.HostageSpecialActionImpl_State_SpecialIdle);
    if (!target || !CreateAndEnableHook(target, reinterpret_cast<void*>(&hk_State_SpecialIdle), reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("HostageStruggle: State_SpecialIdle hook failed, SetForceStruggle off");
        return true;
    }
    g_Target = target;
    return true;
}

bool Uninstall_SpecialActionImpl_State_SpecialIdle_Hook()
{
    if (g_Target)
        DisableAndRemoveHook(g_Target);
    g_Target = nullptr;
    g_Orig = nullptr;
    return true;
}

bool Is_SpecialActionImpl_State_SpecialIdle_Hooked()
{
    return g_Target != nullptr;
}
