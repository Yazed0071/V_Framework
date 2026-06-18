#include "pch.h"
#include "LoadingSplash.h"

#include <cstdint>

#include "HookUtils.h"
#include "log.h"
#include "AddressSet.h"

namespace
{
    using SplashFn_t = void(__fastcall*)(void* self);
    using SetTextureName_t = void(__fastcall*)(void* modelNodeMesh, uint64_t textureHash, uint64_t slotHash, int unk);

    constexpr uint64_t SLOT_MAIN_TEXTURE = 0x3bbf9889ull;

    SplashFn_t       g_OrigSplash = nullptr;
    SplashFn_t       g_OrigTips = nullptr;
    SetTextureName_t g_SetTextureName = nullptr;

    uint64_t g_MainTexture = 0;
    uint64_t g_BlurTexture = 0;


    bool Resolve()
    {
        if (!g_SetTextureName)
            g_SetTextureName = reinterpret_cast<SetTextureName_t>(ResolveGameAddress(gAddr.SetTextureName));
        return g_SetTextureName != nullptr;
    }


    void Apply(void* self)
    {
        if (!self || !Resolve())
            return;

        const uintptr_t base = reinterpret_cast<uintptr_t>(self);
        void* const mainNode = *reinterpret_cast<void**>(base + 0x9d8);
        void* const blurNode = *reinterpret_cast<void**>(base + 0x9e0);

        if (g_MainTexture != 0 && mainNode)
            g_SetTextureName(mainNode, g_MainTexture, SLOT_MAIN_TEXTURE, 2);
        if (g_BlurTexture != 0 && blurNode)
            g_SetTextureName(blurNode, g_BlurTexture, SLOT_MAIN_TEXTURE, 2);
    }


    void __fastcall hkSplash(void* self)
    {
        g_OrigSplash(self);
        Apply(self);
    }


    void __fastcall hkTips(void* self)
    {
        g_OrigTips(self);
        Apply(self);
    }
}


void LoadingSplash_SetMainTexture(uint64_t textureHash)
{
    g_MainTexture = textureHash;
}

void LoadingSplash_ClearMainTexture()
{
    g_MainTexture = 0;
}

void LoadingSplash_SetBlurTexture(uint64_t textureHash)
{
    g_BlurTexture = textureHash;
}

void LoadingSplash_ClearBlurTexture()
{
    g_BlurTexture = 0;
}

void LoadingSplash_ClearTextures()
{
    g_MainTexture = 0;
    g_BlurTexture = 0;
}


bool Install_LoadingSplash_Hook()
{
    void* target = ResolveGameAddress(gAddr.LoadingScreenOrGameOverSplash2);
    if (!target)
        return false;

    const bool ok = CreateAndEnableHook(
        target,
        reinterpret_cast<void*>(&hkSplash),
        reinterpret_cast<void**>(&g_OrigSplash));

    if (gAddr.LoadingTipsEv_UpdateActPhase != 0)
    {
        void* targetTips = ResolveGameAddress(gAddr.LoadingTipsEv_UpdateActPhase);
        if (targetTips)
            CreateAndEnableHook(
                targetTips,
                reinterpret_cast<void*>(&hkTips),
                reinterpret_cast<void**>(&g_OrigTips));
    }

    Log("[Hook] LoadingSplash: %s\n", ok ? "OK" : "FAIL");
    return ok;
}


bool Uninstall_LoadingSplash_Hook()
{
    DisableAndRemoveHook(ResolveGameAddress(gAddr.LoadingScreenOrGameOverSplash2));
    if (gAddr.LoadingTipsEv_UpdateActPhase != 0)
        DisableAndRemoveHook(ResolveGameAddress(gAddr.LoadingTipsEv_UpdateActPhase));

    g_OrigSplash = nullptr;
    g_OrigTips = nullptr;
    return true;
}
