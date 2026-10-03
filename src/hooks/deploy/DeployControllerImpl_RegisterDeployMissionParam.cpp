#include "pch.h"
#include "DeployControllerImpl_RegisterDeployMissionParam.h"

#include "DeployMissionRegistry.h"

#include "log.h"

#include <atomic>
#include <cstring>
#include <mutex>

namespace
{
    using RegisterDeployMissionParam_t = void(__fastcall*)(std::uint8_t* iface, std::uint16_t id,
                                                            const std::uint8_t* record);

    constexpr std::uint16_t kCaptureMissionId = 1;

    std::mutex                   g_SwapLock;
    void**                       g_Slot = nullptr;
    std::atomic<RegisterDeployMissionParam_t> g_OrigRegisterDeployMissionParam{ nullptr };

    thread_local bool         t_Capturing = false;
    thread_local bool         t_Captured = false;
    thread_local std::uint8_t t_Record[deployMission::kRecordSize];

    void __fastcall hkRegisterDeployMissionParam(std::uint8_t* iface, std::uint16_t id,
                                                 const std::uint8_t* record)
    {
        if (t_Capturing && id == kCaptureMissionId)
        {
            t_Captured = deployMission::CopyEngineMemory(t_Record, record, deployMission::kRecordSize);
            return;
        }
        if (const auto original = g_OrigRegisterDeployMissionParam.load(std::memory_order_acquire))
            original(iface, id, record);
    }
}

bool EnsureDeployMissionCaptureInstalled()
{
    std::lock_guard<std::mutex> lock(g_SwapLock);

    void** vtable = deployMission::EnginePointerAt<void*>(deployMission::DeployIface(), 0);
    if (!vtable)
        return false;

    void* current = nullptr;
    if (!deployMission::ReadEngine(vtable, current))
        return false;
    if (current == reinterpret_cast<void*>(&hkRegisterDeployMissionParam))
        return true;

    g_OrigRegisterDeployMissionParam.store(reinterpret_cast<RegisterDeployMissionParam_t>(current), std::memory_order_release);
    g_Slot = vtable;
    if (!deployMission::WriteVtableSlot(vtable, reinterpret_cast<void*>(&hkRegisterDeployMissionParam)))
    {
        g_OrigRegisterDeployMissionParam.store(nullptr, std::memory_order_release);
        g_Slot = nullptr;
        LogError("Dispatch: cannot patch the dispatch controller's RegisterDeployMissionParam slot, custom missions cannot register");
        return false;
    }
    return true;
}

void BeginDeployMissionCapture()
{
    t_Captured = false;
    t_Capturing = true;
}

bool EndDeployMissionCapture(std::uint8_t* record)
{
    t_Capturing = false;
    if (!t_Captured)
        return false;
    t_Captured = false;
    std::memcpy(record, t_Record, deployMission::kRecordSize);
    return true;
}

void Uninstall_DeployControllerImpl_RegisterDeployMissionParam_Capture()
{
    std::lock_guard<std::mutex> lock(g_SwapLock);
    void* current = nullptr;
    const auto original = g_OrigRegisterDeployMissionParam.load(std::memory_order_acquire);
    if (g_Slot && original && deployMission::ReadEngine(g_Slot, current)
        && current == reinterpret_cast<void*>(&hkRegisterDeployMissionParam))
        deployMission::WriteVtableSlot(g_Slot, reinterpret_cast<void*>(original));
    g_Slot = nullptr;
}
