#include "pch.h"

#include <Windows.h>
#include <cstdint>

#include "AddressSet.h"
#include "AnimFile_GetEventData.h"
#include "EventCallback_ExecCallback.h"
#include "HookUtils.h"
#include "LuaBroadcaster.h"
#include "MissionCodeGuard.h"
#include "log.h"

namespace
{
    using ExecCallback_t = void(__fastcall*)(void* callback, void* param, const void* group, int index, int section);

    ExecCallback_t g_Orig = nullptr;
    void* g_Target = nullptr;

    thread_local bool t_Firing = false;
    thread_local int t_OwnerId = -1;

    void __fastcall hk_ExecCallback(void* callback, void* param, const void* group, int index, int section)
    {
        ClipEventLabel label;
        if (t_Firing || !FindAddedClipEvent(group, index, label) || MissionCodeGuard::ShouldBypassHooks())
        {
            g_Orig(callback, param, group, index, section);
            return;
        }

        t_Firing = true;
        t_OwnerId = -1;
        g_Orig(callback, param, group, index, section);
        const int ownerId = t_OwnerId;
        t_Firing = false;
        t_OwnerId = -1;

        if (ownerId < 0)
            return;
        if (label.event.empty())
            V_FrameWork::EmitMessage("GameObject", "ClipEvent", label.eventHash, label.clip, ownerId);
        else
            V_FrameWork::EmitMessage("GameObject", "ClipEvent", label.event, label.clip, ownerId);
    }
}

bool ClipEvent_IsFiring()
{
    return t_Firing;
}

void ClipEvent_NoteOwner(int gameObjectId)
{
    if (t_Firing)
        t_OwnerId = gameObjectId;
}

bool Install_EventCallback_ExecCallback_Hook()
{
    if (g_Target)
        return true;
    if (!gAddr.EventCallback_ExecCallback)
    {
        LogError("ClipEvents: EventCallback::ExecCallback address missing for this build, no ClipEvent message");
        return true;
    }
    void* target = ResolveGameAddress(gAddr.EventCallback_ExecCallback);
    if (!target || !CreateAndEnableHook(target, reinterpret_cast<void*>(&hk_ExecCallback), reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("ClipEvents: ExecCallback hook failed, no ClipEvent message");
        return true;
    }
    g_Target = target;
    return true;
}

bool Uninstall_EventCallback_ExecCallback_Hook()
{
    if (g_Target)
        DisableAndRemoveHook(g_Target);
    g_Target = nullptr;
    g_Orig = nullptr;
    return true;
}
