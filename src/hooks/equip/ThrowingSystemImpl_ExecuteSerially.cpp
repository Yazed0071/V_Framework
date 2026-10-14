#include "pch.h"
#include "ThrowingSystemImpl_ExecuteSerially.h"

#include <Windows.h>
#include <mutex>
#include <vector>

#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "ThrowingImpl_AddReboundNoise.h"
#include "ThrowingImpl_UpdateAction.h"
#include "ThrowingSystemLayout.h"
#include "log.h"

namespace
{
    using ExecuteSerially_t   = void(__fastcall*)(void* execution);
    using OverwriteVelocity_t = void(__fastcall*)(void* throwingSystem, std::uint8_t handle, const float* target, float flightTime);
    using ItemAtPosition_t    = void(__fastcall*)(void* impl, std::uint32_t localIndex, const float* position);
    using EffectLanding_t     = void(__fastcall*)(void* internalSystem, const float* position, const float* rotation);
    using EffectDropInWater_t = void(__fastcall*)(void* internalSystem, const float* position);

    constexpr std::size_t kMaxQueuedRequests           = 64;
    constexpr std::size_t kOverwriteVelocitySlot       = 0x30 / sizeof(void*);
    constexpr std::size_t kCreateEffectDropInWaterSlot = 0x30 / sizeof(void*);
    constexpr std::size_t kCreateEffectLandingSlot     = 0x38 / sizeof(void*);

    ExecuteSerially_t g_Orig   = nullptr;
    void*             g_Target = nullptr;

    std::mutex                   g_Mutex;
    std::vector<ThrowingRequest> g_Queue;

    void CreateEffectAt(std::uint8_t* system, const ThrowingRequest& request)
    {
        using namespace ThrowingSystemLayout;
        auto* internalSystem = Read<std::uint8_t*>(system, kInternalSystem);
        if (!internalSystem)
            return;
        void** vtable = *reinterpret_cast<void***>(internalSystem);
        alignas(16) const float position[4] = { request.values[0], request.values[1], request.values[2], 0.0f };
        if (request.kind == ThrowingRequestKind::CreateEffectLanding)
        {
            alignas(16) const float rotation[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
            reinterpret_cast<EffectLanding_t>(vtable[kCreateEffectLandingSlot])(internalSystem, position, rotation);
        }
        else
        {
            reinterpret_cast<EffectDropInWater_t>(vtable[kCreateEffectDropInWaterSlot])(internalSystem, position);
        }
    }

    void Apply(std::uint8_t* system, const ThrowingRequest& request)
    {
        using namespace ThrowingSystemLayout;
        if (request.kind == ThrowingRequestKind::CreateEffectLanding
            || request.kind == ThrowingRequestKind::CreateEffectDropInWater)
        {
            CreateEffectAt(system, request);
            return;
        }
        if (request.handle >= Capacity(system))
            return;
        std::uint8_t* impl = nullptr;
        std::uint32_t local = 0;
        if (!ResolveImpl(system, request.handle, impl, local))
            return;
        std::uint8_t* record = ImplRecord(impl, local);
        auto* flags = reinterpret_cast<std::uint32_t*>(record + kRecordFlags);
        if ((*flags & kFlagAlive) == 0 || ImplRealEquipId(impl, local) != request.equipId)
            return;

        switch (request.kind)
        {
        case ThrowingRequestKind::Activate:
            if ((*flags & kFlagActivated) == 0)
                *flags |= kFlagActivateNow;
            break;
        case ThrowingRequestKind::Deactivate:
            *flags |= kFlagDone;
            break;
        case ThrowingRequestKind::SetLifeLeft:
            *reinterpret_cast<float*>(record + kRecordLifeLeft) = request.values[0];
            break;
        case ThrowingRequestKind::Stop:
        {
            *flags |= kFlagsStopped;
            float* velocity = Read<float*>(impl, kImplVelocities) + local * 4;
            velocity[0] = velocity[1] = velocity[2] = velocity[3] = 0.0f;
            break;
        }
        case ThrowingRequestKind::OverwriteVelocity:
        {
            auto overwrite = reinterpret_cast<OverwriteVelocity_t>((*reinterpret_cast<void***>(system))[kOverwriteVelocitySlot]);
            overwrite(system, static_cast<std::uint8_t>(request.handle), request.values, request.values[3]);
            break;
        }
        case ThrowingRequestKind::AddReboundNoise:
            ThrowingImpl_AddReboundNoiseQuiet(impl, local, Position(system, request.handle));
            break;
        case ThrowingRequestKind::PostReboundSound:
        {
            const std::uintptr_t address = request.kind == ThrowingRequestKind::AddReboundNoise
                                               ? gAddr.ThrowingImpl_AddReboundNoise
                                               : gAddr.ThrowingImpl_PostReboundSound;
            auto call = reinterpret_cast<ItemAtPosition_t>(ResolveGameAddress(address));
            if (call)
                call(impl, local, Position(system, request.handle));
            break;
        }
        default:
            ThrowingImpl_RunUpdateAction(request.kind, impl, local, ImplEquipId(impl, local), request.option);
            break;
        }
    }

    bool ApplySafely(std::uint8_t* system, const ThrowingRequest& request)
    {
        __try
        {
            Apply(system, request);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void ApplyQueuedRequests(std::uint8_t* system)
    {
        std::vector<ThrowingRequest> requests;
        {
            std::lock_guard<std::mutex> lock(g_Mutex);
            if (g_Queue.empty())
                return;
            requests.swap(g_Queue);
        }
        if (MissionCodeGuard::ShouldBypassHooks())
            return;
        for (const ThrowingRequest& request : requests)
        {
            if (!ApplySafely(system, request))
                LogWarn("ThrowingSystem: request %d on handle %u faulted, skipped",
                    static_cast<int>(request.kind), request.handle);
        }
    }

    void __fastcall hkExecuteSerially(void* execution)
    {
        g_Orig(execution);
        if (std::uint8_t* system = ThrowingSystemLayout::ThrowingSystem())
            ApplyQueuedRequests(system);
    }
}

bool ThrowingSystem_QueueRequest(const ThrowingRequest& request)
{
    if (!g_Target)
        return false;
    std::lock_guard<std::mutex> lock(g_Mutex);
    if (g_Queue.size() >= kMaxQueuedRequests)
        return false;
    g_Queue.push_back(request);
    return true;
}

bool Install_ThrowingSystemImpl_ExecuteSerially_Hook()
{
    g_Target = ResolveGameAddress(gAddr.ThrowingSystemImpl_ExecuteSerially);
    if (!g_Target)
    {
        LogError("ThrowingSystem: ExecuteSerially address missing for this build, V_ThrowingImpl actions do nothing");
        return true;
    }
    if (!CreateAndEnableHook(g_Target, reinterpret_cast<void*>(&hkExecuteSerially),
                             reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("ThrowingSystem: ExecuteSerially hook failed, V_ThrowingImpl actions do nothing");
        g_Target = nullptr;
        return false;
    }
    return true;
}

void Uninstall_ThrowingSystemImpl_ExecuteSerially_Hook()
{
    if (g_Target)
        DisableAndRemoveHook(g_Target);
    g_Target = nullptr;
    g_Orig   = nullptr;
    std::lock_guard<std::mutex> lock(g_Mutex);
    g_Queue.clear();
}
