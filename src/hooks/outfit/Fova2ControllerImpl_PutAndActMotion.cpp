#include "pch.h"

#include "Fova2ControllerImpl_PutAndActMotion.h"

#include "HeadMotion.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    using WorkActMotion_t = void (__fastcall*)(void* work, const float* timeBase);

    WorkActMotion_t g_OrigWorkActMotion = nullptr;
    void*           g_WorkActMotionTarget = nullptr;

    void __fastcall hkWorkActMotion(void* work, const float* timeBase)
    {
        g_OrigWorkActMotion(work, timeBase);
        outfit::headmotion::OnWorkActMotion(work, timeBase);
    }
}

bool Install_Fova2ControllerImpl_PutAndActMotion_Hook()
{
    const std::uintptr_t address = outfit::headmotion::WorkActMotionAddress();
    if (!address)
        return true;

    void* target = ResolveGameAddress(address);
    if (!target || !CreateAndEnableHook(target,
            reinterpret_cast<void*>(&hkWorkActMotion),
            reinterpret_cast<void**>(&g_OrigWorkActMotion)))
    {
        LogError("HeadMotion: head model act hook failed, PlayHeadMotion off");
        return false;
    }
    g_WorkActMotionTarget = target;
    return true;
}

void Uninstall_Fova2ControllerImpl_PutAndActMotion_Hook()
{
    if (g_WorkActMotionTarget)
        DisableAndRemoveHook(g_WorkActMotionTarget);
    g_WorkActMotionTarget = nullptr;
    g_OrigWorkActMotion   = nullptr;
    outfit::headmotion::Shutdown();
}
