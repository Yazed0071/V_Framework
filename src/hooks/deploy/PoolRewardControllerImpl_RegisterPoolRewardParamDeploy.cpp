#include "pch.h"
#include "PoolRewardControllerImpl_RegisterPoolRewardParamDeploy.h"

#include "DeployMissionRegistry.h"

#include "log.h"

#include <atomic>
#include <cstring>
#include <mutex>

namespace
{
    using RegisterPoolRewardParamDeploy_t = void(__fastcall*)(std::uint8_t* rewardIface,
                                                               const std::uint8_t* reward);

    constexpr std::uint8_t kCaptureRewardKey = 0xFE;

    std::mutex                      g_SwapLock;
    void**                          g_Slot = nullptr;
    std::atomic<RegisterPoolRewardParamDeploy_t> g_OrigRegisterPoolRewardParamDeploy{ nullptr };

    thread_local bool         t_Capturing = false;
    thread_local bool         t_Captured = false;
    thread_local std::uint8_t t_Reward[deployMission::kRewardSize];

    void __fastcall hkRegisterPoolRewardParamDeploy(std::uint8_t* rewardIface, const std::uint8_t* reward)
    {
        std::uint8_t rewardKey = 0;
        if (t_Capturing && deployMission::ReadEngine(reward, rewardKey) && rewardKey == kCaptureRewardKey)
        {
            t_Captured = deployMission::CopyEngineMemory(t_Reward, reward, deployMission::kRewardSize);
            return;
        }
        if (const auto original = g_OrigRegisterPoolRewardParamDeploy.load(std::memory_order_acquire))
            original(rewardIface, reward);
    }
}

bool EnsureDeployRewardCaptureInstalled()
{
    std::lock_guard<std::mutex> lock(g_SwapLock);

    void** vtable = deployMission::EnginePointerAt<void*>(deployMission::RewardIface(), 0);
    if (!vtable)
        return false;

    void* current = nullptr;
    if (!deployMission::ReadEngine(vtable, current))
        return false;
    if (current == reinterpret_cast<void*>(&hkRegisterPoolRewardParamDeploy))
        return true;

    g_OrigRegisterPoolRewardParamDeploy.store(reinterpret_cast<RegisterPoolRewardParamDeploy_t>(current), std::memory_order_release);
    g_Slot = vtable;
    if (!deployMission::WriteVtableSlot(vtable, reinterpret_cast<void*>(&hkRegisterPoolRewardParamDeploy)))
    {
        g_OrigRegisterPoolRewardParamDeploy.store(nullptr, std::memory_order_release);
        g_Slot = nullptr;
        LogError("Dispatch: cannot patch the pool reward controller's RegisterPoolRewardParam slot, custom rewards cannot register");
        return false;
    }
    return true;
}

void BeginDeployRewardCapture()
{
    t_Captured = false;
    t_Capturing = true;
}

bool EndDeployRewardCapture(std::uint8_t* reward)
{
    t_Capturing = false;
    if (!t_Captured)
        return false;
    t_Captured = false;
    std::memcpy(reward, t_Reward, deployMission::kRewardSize);
    return true;
}

void Uninstall_PoolRewardControllerImpl_RegisterPoolRewardParamDeploy_Capture()
{
    std::lock_guard<std::mutex> lock(g_SwapLock);
    void* current = nullptr;
    const auto original = g_OrigRegisterPoolRewardParamDeploy.load(std::memory_order_acquire);
    if (g_Slot && original && deployMission::ReadEngine(g_Slot, current)
        && current == reinterpret_cast<void*>(&hkRegisterPoolRewardParamDeploy))
        deployMission::WriteVtableSlot(g_Slot, reinterpret_cast<void*>(original));
    g_Slot = nullptr;
}
