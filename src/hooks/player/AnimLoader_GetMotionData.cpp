#include <pch.h>

#include <Windows.h>
#include <cstdint>

#include "AddressSet.h"
#include "AnimControl_GetAnimFileFromGaniPath.h"
#include "AnimLoader_GetMotionData.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    using GetMotionData_t = void*(__fastcall*)(void* loader, std::uint64_t clip, void* group);

    GetMotionData_t g_Orig = nullptr;
    void* g_Target = nullptr;

    void* __fastcall hk_GetMotionData(void* loader, std::uint64_t clip, void* group)
    {
        void* file = g_Orig(loader, clip, group);
        return file ? file : FrameWorkMotion_GetAnimFile(clip); // a clip is looked up again here when it starts playing, without GetAnimFileFromGaniPath
    }
}

bool Install_AnimLoader_GetMotionData_Hook()
{
    if (g_Target)
        return true;
    void* target = ResolveGameAddress(gAddr.AnimLoader_GetMotionData);
    if (!target || !CreateAndEnableHook(target, reinterpret_cast<void*>(&hk_GetMotionData), reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("FrameWorkMotion: AnimLoader GetMotionData hook failed, clips from V_FrameWork_motion.mtar won't play");
        return false;
    }
    g_Target = target;
    return true;
}

bool Uninstall_AnimLoader_GetMotionData_Hook()
{
    if (!g_Target)
        return true;
    DisableAndRemoveHook(g_Target);
    g_Target = nullptr;
    g_Orig = nullptr;
    return true;
}
