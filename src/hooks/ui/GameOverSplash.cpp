#include "pch.h"
#include "GameOverSplash.h"

#include <cstdint>

#include "HookUtils.h"
#include "log.h"
#include "AddressSet.h"
#include "MissionCodeGuard.h"

namespace
{
    using GameOverSetVisible_t = void(__fastcall*)(uint64_t* layout, char visible);
    using SetTextureName_t = void(__fastcall*)(void* modelNodeMesh, uint64_t textureHash, uint64_t slotHash, int unk);

    constexpr uint64_t SLOT_MAIN_TEXTURE = 0x3bbf9889ull;
    constexpr uint64_t SLOT_BLUR_LAYER   = 0x8d982b8eull;

    GameOverSetVisible_t g_OrigGameOverSetVisible = nullptr;
    SetTextureName_t     g_SetTextureName = nullptr;

    uint64_t g_MainTexture = 0;
    uint64_t g_BlurTexture = 0;


    bool Resolve()
    {
        if (!g_SetTextureName)
            g_SetTextureName = reinterpret_cast<SetTextureName_t>(ResolveGameAddress(gAddr.SetTextureName));
        return g_SetTextureName != nullptr;
    }


    void __fastcall hkGameOverSetVisible(uint64_t* layout, char visible)
    {
        if (MissionCodeGuard::ShouldBypassHooks())
        {
            g_OrigGameOverSetVisible(layout, visible);
            return;
        }

        g_OrigGameOverSetVisible(layout, visible);

        if (!visible || !layout || !Resolve())
            return;

        void* const node8 = reinterpret_cast<void*>(layout[8]);
        void* const node9 = reinterpret_cast<void*>(layout[9]);
        void* const node10 = reinterpret_cast<void*>(layout[10]);
        void* const node11 = reinterpret_cast<void*>(layout[11]);

        if (g_MainTexture != 0)
        {
            if (node8) g_SetTextureName(node8, g_MainTexture, SLOT_MAIN_TEXTURE, 2);
            if (node9) g_SetTextureName(node9, g_MainTexture, SLOT_MAIN_TEXTURE, 2);
        }

        if (g_BlurTexture != 0)
        {
            if (node8)  g_SetTextureName(node8,  g_BlurTexture, SLOT_BLUR_LAYER, 2);
            if (node9)  g_SetTextureName(node9,  g_BlurTexture, SLOT_BLUR_LAYER, 2);
            if (node10) g_SetTextureName(node10, g_BlurTexture, SLOT_MAIN_TEXTURE, 2);
            if (node11) g_SetTextureName(node11, g_BlurTexture, SLOT_MAIN_TEXTURE, 2);
        }
    }
}


void GameOverSplash_SetMainTexture(uint64_t textureHash)
{
    g_MainTexture = textureHash;
}

void GameOverSplash_ClearMainTexture()
{
    g_MainTexture = 0;
}

void GameOverSplash_SetBlurTexture(uint64_t textureHash)
{
    g_BlurTexture = textureHash;
}

void GameOverSplash_ClearBlurTexture()
{
    g_BlurTexture = 0;
}

void GameOverSplash_ClearTextures()
{
    g_MainTexture = 0;
    g_BlurTexture = 0;
}


bool Install_GameOverSplash_Hook()
{
    void* target = ResolveGameAddress(gAddr.GameOverSetVisible);
    if (!target)
        return false;

    const bool ok = CreateAndEnableHook(
        target,
        reinterpret_cast<void*>(&hkGameOverSetVisible),
        reinterpret_cast<void**>(&g_OrigGameOverSetVisible));

    Log("[Hook] GameOverSplash: %s\n", ok ? "OK" : "FAIL");
    return ok;
}


bool Uninstall_GameOverSplash_Hook()
{
    DisableAndRemoveHook(ResolveGameAddress(gAddr.GameOverSetVisible));
    g_OrigGameOverSetVisible = nullptr;
    return true;
}
