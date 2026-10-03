#include "pch.h"
#include "LoadingSplash.h"

#include <cstdint>

#include "HookUtils.h"
#include "log.h"
#include "AddressSet.h"
#include "MissionCodeGuard.h"
#include "MissionTextureTable.h"

namespace
{
    using SplashFn_t = void(__fastcall*)(void* self);
    using SetTextureName_t = void(__fastcall*)(void* modelNodeMesh, uint64_t textureHash, uint64_t slotHash, int unk);

    constexpr uint64_t SLOT_MAIN_TEXTURE = 0x3bbf9889ull;

    SplashFn_t       g_OrigSplash = nullptr;
    SplashFn_t       g_OrigTips = nullptr;
    SetTextureName_t g_SetTextureName = nullptr;

    MissionTextureTable g_MainTextures;
    MissionTextureTable g_BlurTextures;

    void*    g_AppliedSelf = nullptr;
    uint64_t g_AppliedMain = 0;
    uint64_t g_AppliedBlur = 0;


    bool Resolve()
    {
        if (!g_SetTextureName)
            g_SetTextureName = reinterpret_cast<SetTextureName_t>(ResolveGameAddress(gAddr.SetTextureName));
        return g_SetTextureName != nullptr;
    }


    void Apply(void* self, bool force)
    {
        if (!self || !Resolve())
            return;

        const uint32_t mission = MissionCodeGuard::GetCurrentMissionCode();
        const uint64_t mainTex = g_MainTextures.Resolve(mission);
        const uint64_t blurTex = g_BlurTextures.Resolve(mission);

        if (!force && self == g_AppliedSelf && mainTex == g_AppliedMain && blurTex == g_AppliedBlur)
            return;

        const uintptr_t base = reinterpret_cast<uintptr_t>(self);
        void* const mainNode = *reinterpret_cast<void**>(base + 0x9d8);
        void* const blurNode = *reinterpret_cast<void**>(base + 0x9e0);

        if (mainTex != 0 && mainNode)
            g_SetTextureName(mainNode, mainTex, SLOT_MAIN_TEXTURE, 2);
        if (blurTex != 0 && blurNode)
            g_SetTextureName(blurNode, blurTex, SLOT_MAIN_TEXTURE, 2);

        g_AppliedSelf = self;
        g_AppliedMain = mainTex;
        g_AppliedBlur = blurTex;
    }


    void __fastcall hkSplash(void* self)
    {
        g_OrigSplash(self);
        Apply(self, true);
    }


    void __fastcall hkTips(void* self)
    {
        g_OrigTips(self);
        Apply(self, false);
    }
}


void LoadingSplash_SetMainTexture(uint64_t textureHash, uint32_t missionCode)
{
    g_MainTextures.Set(missionCode, textureHash);
    if (textureHash == 0)
        LogWarn("SetLoadingSplashMainTexturePath: path hashed to 0 (mission %u), vanilla texture used", missionCode);
}

void LoadingSplash_ClearMainTexture(uint32_t missionCode)
{
    g_MainTextures.Clear(missionCode);
}

void LoadingSplash_SetBlurTexture(uint64_t textureHash, uint32_t missionCode)
{
    g_BlurTextures.Set(missionCode, textureHash);
    if (textureHash == 0)
        LogWarn("SetLoadingSplashBlurTexturePath: path hashed to 0 (mission %u), vanilla texture used", missionCode);
}

void LoadingSplash_ClearBlurTexture(uint32_t missionCode)
{
    g_BlurTextures.Clear(missionCode);
}

void LoadingSplash_ClearTextures(uint32_t missionCode)
{
    g_MainTextures.Clear(missionCode);
    g_BlurTextures.Clear(missionCode);
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

    if (!ok)
        LogError("LoadingSplash: splash hook failed, custom loading textures off");
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
