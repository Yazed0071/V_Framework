#include "pch.h"
#include "UiTextureOverrides.h"

#include <Windows.h>
#include <cstdint>
#include <unordered_map>

#include "FoxHashes.h"
#include "HookUtils.h"
#include "log.h"
#include "MissionCodeGuard.h"
#include "AddressSet.h"

namespace
{
    using SetEquipBackgroundTexture_t = uint8_t(__fastcall*)(int equipId, void* sortieWeaponNode);
    using SetTextureName_t = void(__fastcall*)(void* modelNodeMesh, uint64_t textureHash, uint64_t slotHash, int unk);
    using LoadingScreenOrGameOverSplash2_t = void(__fastcall*)(void* self);
    using GameOverSetVisible_t = void(__fastcall*)(uint64_t* layout, char visible);
    using LoadingTipsEvUpdateActPhase_t = void(__fastcall*)(void* self);


    static uint64_t g_MaskTextureSlotHash = 0;


    static constexpr uint64_t SLOT_MAIN_TEXTURE = 0x3bbf9889ull;


    static constexpr uint64_t SLOT_BLUR_LAYER = 0x8d982b8eull;

    static SetEquipBackgroundTexture_t      g_OrigSetEquipBackgroundTexture = nullptr;
    static SetTextureName_t                 g_SetTextureName = nullptr;
    static LoadingScreenOrGameOverSplash2_t g_OrigLoadingScreenOrGameOverSplash2 = nullptr;
    static GameOverSetVisible_t             g_OrigGameOverSetVisible = nullptr;
    static LoadingTipsEvUpdateActPhase_t    g_OrigLoadingTipsEvUpdateActPhase = nullptr;

    static uint64_t g_DefaultTexture = 0;
    static uint64_t g_EnemyWeaponTexture = 0;
    static std::unordered_map<int, uint64_t> g_PerEquipTextures;
    static std::unordered_map<int, uint64_t> g_PerEnemyEquipTextures;

    struct SplashConfig
    {
        uint64_t mainTexture = 0;
        uint64_t blurTexture = 0;
    };

    static SplashConfig g_LoadingConfig;
    static SplashConfig g_GameOverConfig;


    static bool ShouldUseVanillaDdWeaponBg(int equipId, void* sortieWeaponNode)
    {
        if (sortieWeaponNode == nullptr)
            return false;

        if (equipId == 0)
            return false;

        if (static_cast<uint32_t>(equipId - 1) <= 0x47u)
            return false;

        if (static_cast<uint32_t>(equipId - 0x7d) <= 3u)
            return false;

        return true;
    }


    static bool IsUniqueArmEquip(int equipId)
    {
        return equipId >= 0x203 && equipId <= 0x206;
    }


    static bool IsEnemyWeaponLikeCategory(int equipId, void* sortieWeaponNode)
    {
        if (sortieWeaponNode == nullptr)
            return false;

        if (equipId == 0)
            return false;

        return !ShouldUseVanillaDdWeaponBg(equipId, sortieWeaponNode);
    }


    static uint8_t ApplyTexture(void* modelNodeMesh, uint64_t textureHash)
    {
        if (!modelNodeMesh || !textureHash || !g_SetTextureName)
            return 0;

        g_SetTextureName(modelNodeMesh, textureHash, g_MaskTextureSlotHash, 2);
        return 1;
    }


    static bool ResolveUiHelpers()
    {
        if (!g_SetTextureName)
        {
            g_SetTextureName = reinterpret_cast<SetTextureName_t>(
                ResolveGameAddress(gAddr.SetTextureName));
        }

        if (g_MaskTextureSlotHash == 0)
        {
            g_MaskTextureSlotHash = static_cast<uint64_t>(FoxHashes::StrCode32("Mask_Texture"));
        }

        return g_SetTextureName != nullptr && g_MaskTextureSlotHash != 0;
    }
}


void EquipBg_SetDefaultTexture(uint64_t textureHash)
{
    g_DefaultTexture = textureHash;
    Log("[EquipBg] DefaultTexture = 0x%llX\n",
        static_cast<unsigned long long>(textureHash));
}


void EquipBg_ClearDefaultTexture()
{
    g_DefaultTexture = 0;
    Log("[EquipBg] DefaultTexture cleared\n");
}


void EquipBg_SetEnemyWeaponTexture(uint64_t textureHash)
{
    g_EnemyWeaponTexture = textureHash;
    Log("[EquipBg] EnemyWeaponTexture = 0x%llX\n",
        static_cast<unsigned long long>(textureHash));
}


void EquipBg_ClearEnemyWeaponTexture()
{
    g_EnemyWeaponTexture = 0;
    Log("[EquipBg] EnemyWeaponTexture cleared\n");
}


void EquipBg_SetEquipTexture(int equipId, uint64_t textureHash)
{
    g_PerEquipTextures[equipId] = textureHash;
    Log("[EquipBg] EquipId %d -> 0x%llX\n",
        equipId,
        static_cast<unsigned long long>(textureHash));
}


void EquipBg_ClearEquipTexture(int equipId)
{
    g_PerEquipTextures.erase(equipId);
    Log("[EquipBg] EquipId %d cleared\n", equipId);
}


void EquipBg_SetEnemyEquipTexture(int equipId, uint64_t textureHash)
{
    g_PerEnemyEquipTextures[equipId] = textureHash;
    Log("[EquipBg][EnemyPerEquip] EquipId %d -> 0x%llX\n",
        equipId,
        static_cast<unsigned long long>(textureHash));
}


void EquipBg_ClearEnemyEquipTexture(int equipId)
{
    g_PerEnemyEquipTextures.erase(equipId);
    Log("[EquipBg][EnemyPerEquip] EquipId %d cleared\n", equipId);
}


void EquipBg_ClearAllEquipTextures()
{
    g_PerEquipTextures.clear();
    g_PerEnemyEquipTextures.clear();
    Log("[EquipBg] All per-equip textures cleared\n");
}


void LoadingSplash_SetMainTexture(uint64_t textureHash)
{
    g_LoadingConfig.mainTexture = textureHash;
    Log("[LoadingSplash] MainTexture = 0x%llX\n",
        static_cast<unsigned long long>(textureHash));
}


void LoadingSplash_ClearMainTexture()
{
    g_LoadingConfig.mainTexture = 0;
    Log("[LoadingSplash] MainTexture cleared\n");
}


void LoadingSplash_SetBlurTexture(uint64_t textureHash)
{
    g_LoadingConfig.blurTexture = textureHash;
    Log("[LoadingSplash] BlurTexture = 0x%llX\n",
        static_cast<unsigned long long>(textureHash));
}


void LoadingSplash_ClearBlurTexture()
{
    g_LoadingConfig.blurTexture = 0;
    Log("[LoadingSplash] BlurTexture cleared\n");
}


void LoadingSplash_ClearTextures()
{
    g_LoadingConfig.mainTexture = 0;
    g_LoadingConfig.blurTexture = 0;
    Log("[LoadingSplash] All textures cleared\n");
}


void GameOverSplash_SetMainTexture(uint64_t textureHash)
{
    g_GameOverConfig.mainTexture = textureHash;
    Log("[GameOverSplash] MainTexture = 0x%llX\n",
        static_cast<unsigned long long>(textureHash));
}


void GameOverSplash_ClearMainTexture()
{
    g_GameOverConfig.mainTexture = 0;
    Log("[GameOverSplash] MainTexture cleared\n");
}


void GameOverSplash_SetBlurTexture(uint64_t textureHash)
{
    g_GameOverConfig.blurTexture = textureHash;
    Log("[GameOverSplash] BlurTexture = 0x%llX\n",
        static_cast<unsigned long long>(textureHash));
}


void GameOverSplash_ClearBlurTexture()
{
    g_GameOverConfig.blurTexture = 0;
    Log("[GameOverSplash] BlurTexture cleared\n");
}


void GameOverSplash_ClearTextures()
{
    g_GameOverConfig.mainTexture = 0;
    g_GameOverConfig.blurTexture = 0;
    Log("[GameOverSplash] All textures cleared\n");
}


static uint8_t __fastcall hkSetEquipBackgroundTexture(int equipId, void* sortieWeaponNode)
{
    if (!ResolveUiHelpers())
        return g_OrigSetEquipBackgroundTexture(equipId, sortieWeaponNode);

    const auto it = g_PerEquipTextures.find(equipId);
    if (it != g_PerEquipTextures.end())
    {
        Log("[EquipBg][PerEquip] equipId=%d -> 0x%llX\n",
            equipId,
            static_cast<unsigned long long>(it->second));
        return ApplyTexture(sortieWeaponNode, it->second);
    }

    if (IsEnemyWeaponLikeCategory(equipId, sortieWeaponNode))
    {
        const auto enemyIt = g_PerEnemyEquipTextures.find(equipId);
        if (enemyIt != g_PerEnemyEquipTextures.end())
        {
            Log("[EquipBg][EnemyPerEquip] equipId=%d -> 0x%llX\n",
                equipId,
                static_cast<unsigned long long>(enemyIt->second));
            return ApplyTexture(sortieWeaponNode, enemyIt->second);
        }

        if (g_EnemyWeaponTexture != 0)
        {
            Log("[EquipBg][EnemyWeapon] equipId=%d -> 0x%llX\n",
                equipId,
                static_cast<unsigned long long>(g_EnemyWeaponTexture));
            return ApplyTexture(sortieWeaponNode, g_EnemyWeaponTexture);
        }
    }

    if (ShouldUseVanillaDdWeaponBg(equipId, sortieWeaponNode) &&
        !IsUniqueArmEquip(equipId) &&
        g_DefaultTexture != 0)
    {
        Log("[EquipBg][Default] equipId=%d -> 0x%llX\n",
            equipId,
            static_cast<unsigned long long>(g_DefaultTexture));
        return ApplyTexture(sortieWeaponNode, g_DefaultTexture);
    }

    return g_OrigSetEquipBackgroundTexture(equipId, sortieWeaponNode);
}


static void __fastcall hkLoadingScreenOrGameOverSplash2(void* self)
{
    g_OrigLoadingScreenOrGameOverSplash2(self);

    if (!self || !ResolveUiHelpers())
        return;

    const uintptr_t base = reinterpret_cast<uintptr_t>(self);
    void* const mainNode = *reinterpret_cast<void**>(base + 0x9d8);
    void* const blurNode = *reinterpret_cast<void**>(base + 0x9e0);

    if (g_LoadingConfig.mainTexture != 0 && mainNode)
    {
        Log("[LoadingSplash][OverrideMain] node=%p -> 0x%llX\n",
            mainNode,
            static_cast<unsigned long long>(g_LoadingConfig.mainTexture));
        g_SetTextureName(mainNode, g_LoadingConfig.mainTexture, SLOT_MAIN_TEXTURE, 2);
    }

    if (g_LoadingConfig.blurTexture != 0 && blurNode)
    {
        Log("[LoadingSplash][OverrideBlur] node=%p -> 0x%llX\n",
            blurNode,
            static_cast<unsigned long long>(g_LoadingConfig.blurTexture));
        g_SetTextureName(blurNode, g_LoadingConfig.blurTexture, SLOT_MAIN_TEXTURE, 2);
    }
}


static void __fastcall hkLoadingTipsEvUpdateActPhase(void* self)
{
    g_OrigLoadingTipsEvUpdateActPhase(self);

    if (!self || !ResolveUiHelpers())
        return;

    const uintptr_t base = reinterpret_cast<uintptr_t>(self);
    void* const mainNode = *reinterpret_cast<void**>(base + 0x9d8);
    void* const blurNode = *reinterpret_cast<void**>(base + 0x9e0);


    static void* s_lastMainNode = nullptr;
    static std::uint64_t s_lastMainHash = 0;
    static void* s_lastBlurNode = nullptr;
    static std::uint64_t s_lastBlurHash = 0;

    if (g_LoadingConfig.mainTexture != 0 && mainNode)
    {
        g_SetTextureName(mainNode, g_LoadingConfig.mainTexture, SLOT_MAIN_TEXTURE, 2);
        if (mainNode != s_lastMainNode || g_LoadingConfig.mainTexture != s_lastMainHash)
        {
            Log("[LoadingTips][OverrideMain] node=%p -> 0x%llX\n",
                mainNode,
                static_cast<unsigned long long>(g_LoadingConfig.mainTexture));
            s_lastMainNode = mainNode;
            s_lastMainHash = g_LoadingConfig.mainTexture;
        }
    }

    if (g_LoadingConfig.blurTexture != 0 && blurNode)
    {
        g_SetTextureName(blurNode, g_LoadingConfig.blurTexture, SLOT_MAIN_TEXTURE, 2);
        if (blurNode != s_lastBlurNode || g_LoadingConfig.blurTexture != s_lastBlurHash)
        {
            Log("[LoadingTips][OverrideBlur] node=%p -> 0x%llX\n",
                blurNode,
                static_cast<unsigned long long>(g_LoadingConfig.blurTexture));
            s_lastBlurNode = blurNode;
            s_lastBlurHash = g_LoadingConfig.blurTexture;
        }
    }
}


static void __fastcall hkGameOverSetVisible(uint64_t* layout, char visible)
{
    if (MissionCodeGuard::ShouldBypassHooks())
    {
        g_OrigGameOverSetVisible(layout, visible);
        return;
    }

    g_OrigGameOverSetVisible(layout, visible);

    if (!visible || !layout || !ResolveUiHelpers())
        return;

    void* const node8 = reinterpret_cast<void*>(layout[8]);
    void* const node9 = reinterpret_cast<void*>(layout[9]);
    void* const node10 = reinterpret_cast<void*>(layout[10]);
    void* const node11 = reinterpret_cast<void*>(layout[11]);

    if (g_GameOverConfig.mainTexture != 0)
    {
        if (node8)
            g_SetTextureName(node8, g_GameOverConfig.mainTexture, SLOT_MAIN_TEXTURE, 2);
        if (node9)
            g_SetTextureName(node9, g_GameOverConfig.mainTexture, SLOT_MAIN_TEXTURE, 2);

        Log("[GameOverSplash][OverrideMain] main=0x%llX\n",
            static_cast<unsigned long long>(g_GameOverConfig.mainTexture));
    }

    if (g_GameOverConfig.blurTexture != 0)
    {
        if (node8)
            g_SetTextureName(node8, g_GameOverConfig.blurTexture, SLOT_BLUR_LAYER, 2);
        if (node9)
            g_SetTextureName(node9, g_GameOverConfig.blurTexture, SLOT_BLUR_LAYER, 2);
        if (node10)
            g_SetTextureName(node10, g_GameOverConfig.blurTexture, SLOT_MAIN_TEXTURE, 2);
        if (node11)
            g_SetTextureName(node11, g_GameOverConfig.blurTexture, SLOT_MAIN_TEXTURE, 2);

        Log("[GameOverSplash][OverrideBlur] blur=0x%llX\n",
            static_cast<unsigned long long>(g_GameOverConfig.blurTexture));
    }
}


bool Install_UiTextureOverrides_Hook()
{
    ResolveUiHelpers();

    void* targetEquip = ResolveGameAddress(gAddr.SetEquipBackgroundTexture);
    void* targetLoading = ResolveGameAddress(gAddr.LoadingScreenOrGameOverSplash2);
    void* targetGameOver = ResolveGameAddress(gAddr.GameOverSetVisible);

    if (!targetEquip || !targetLoading || !targetGameOver || !g_SetTextureName)
        return false;

    const bool okA = CreateAndEnableHook(
        targetEquip,
        reinterpret_cast<void*>(&hkSetEquipBackgroundTexture),
        reinterpret_cast<void**>(&g_OrigSetEquipBackgroundTexture));

    const bool okB = CreateAndEnableHook(
        targetLoading,
        reinterpret_cast<void*>(&hkLoadingScreenOrGameOverSplash2),
        reinterpret_cast<void**>(&g_OrigLoadingScreenOrGameOverSplash2));

    const bool okC = CreateAndEnableHook(
        targetGameOver,
        reinterpret_cast<void*>(&hkGameOverSetVisible),
        reinterpret_cast<void**>(&g_OrigGameOverSetVisible));

    bool okE = true;
    if (gAddr.LoadingTipsEv_UpdateActPhase != 0)
    {
        void* targetTips = ResolveGameAddress(gAddr.LoadingTipsEv_UpdateActPhase);
        if (targetTips)
        {
            okE = CreateAndEnableHook(
                targetTips,
                reinterpret_cast<void*>(&hkLoadingTipsEvUpdateActPhase),
                reinterpret_cast<void**>(&g_OrigLoadingTipsEvUpdateActPhase));
        }
    }

    const bool ok = okA && okB && okC;
    Log("[Hook] UiTextureOverrides: %s\n", ok ? "OK" : "FAIL");
    if (okE && g_OrigLoadingTipsEvUpdateActPhase)
        Log("[Hook] LoadingTipsEvUpdateActPhase: OK\n");
    return ok;
}

bool Uninstall_UiTextureOverrides_Hook()
{
    DisableAndRemoveHook(ResolveGameAddress(gAddr.SetEquipBackgroundTexture));
    DisableAndRemoveHook(ResolveGameAddress(gAddr.LoadingScreenOrGameOverSplash2));
    DisableAndRemoveHook(ResolveGameAddress(gAddr.GameOverSetVisible));

    if (gAddr.LoadingTipsEv_UpdateActPhase != 0)
        DisableAndRemoveHook(ResolveGameAddress(gAddr.LoadingTipsEv_UpdateActPhase));

    g_OrigSetEquipBackgroundTexture = nullptr;
    g_OrigLoadingScreenOrGameOverSplash2 = nullptr;
    g_OrigGameOverSetVisible = nullptr;
    g_OrigLoadingTipsEvUpdateActPhase = nullptr;

    return true;
}