#include "pch.h"

#include "EffectInstance_Update.h"

#include "HeadMotion.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    using EffectUpdate_t         = outfit::headmotion::EffectUpdate_t;
    using EffectContextAcquire_t = outfit::headmotion::EffectContextAcquire_t;

    EffectUpdate_t         g_OrigUpdate    = nullptr;
    EffectContextAcquire_t g_OrigAcquire   = nullptr;
    void*                  g_UpdateTarget  = nullptr;
    void*                  g_AcquireTarget = nullptr;

    bool __fastcall hkUpdate(void* instance, float delta, void* context)
    {
        if (outfit::headmotion::DeferEffectUpdate(instance, delta))
            return true;
        return g_OrigUpdate(instance, delta, context);
    }

    void* __fastcall hkAcquire(void* pool)
    {
        outfit::headmotion::NoteEffectContextPool(pool);
        return g_OrigAcquire(pool);
    }
}

bool Install_EffectInstance_Update_Hook()
{
    const std::uintptr_t update  = outfit::headmotion::EffectUpdateAddress();
    const std::uintptr_t acquire = outfit::headmotion::EffectContextAcquireAddress();
    if (!update || !acquire)
        return true;

    void* acquireTarget = ResolveGameAddress(acquire);
    if (!acquireTarget || !CreateAndEnableHook(acquireTarget,
            reinterpret_cast<void*>(&hkAcquire),
            reinterpret_cast<void**>(&g_OrigAcquire)))
    {
        LogWarn("HeadMotion: effect context hook failed, head effects follow one frame late");
        return false;
    }
    g_AcquireTarget = acquireTarget;

    void* updateTarget = ResolveGameAddress(update);
    if (!updateTarget || !CreateAndEnableHook(updateTarget,
            reinterpret_cast<void*>(&hkUpdate),
            reinterpret_cast<void**>(&g_OrigUpdate)))
    {
        LogWarn("HeadMotion: effect update hook failed, head effects follow one frame late");
        DisableAndRemoveHook(g_AcquireTarget);
        g_AcquireTarget = nullptr;
        g_OrigAcquire   = nullptr;
        return false;
    }
    g_UpdateTarget = updateTarget;
    outfit::headmotion::SetEffectUpdateOriginals(g_OrigUpdate, g_OrigAcquire);
    return true;
}

void Uninstall_EffectInstance_Update_Hook()
{
    outfit::headmotion::SetEffectUpdateOriginals(nullptr, nullptr);
    if (g_UpdateTarget)
        DisableAndRemoveHook(g_UpdateTarget);
    if (g_AcquireTarget)
        DisableAndRemoveHook(g_AcquireTarget);
    g_UpdateTarget  = nullptr;
    g_AcquireTarget = nullptr;
    g_OrigUpdate    = nullptr;
    g_OrigAcquire   = nullptr;
}
