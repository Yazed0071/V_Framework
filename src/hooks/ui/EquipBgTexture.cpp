#include "pch.h"
#include "EquipBgTexture.h"

#include <Windows.h>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <unordered_set>

#include "FoxHashes.h"
#include "HookUtils.h"
#include "log.h"
#include "AddressSet.h"
#include "MissionCodeGuard.h"

namespace
{
    using SetWeaponPanelLogo_t = uint8_t(__fastcall*)(int equipId, void* node);
    using SetTextureName_t = uint8_t(__fastcall*)(void* modelNodeMesh, uint64_t textureHash, uint64_t slotHash, int unk);
    using GetUixUtility_t = void** (__fastcall*)();
    using GetQuarkSystemTable_t = void* (__fastcall*)();

    using TexStatusCreate_t = char(__fastcall*)(void*, void*, unsigned);

    TexStatusCreate_t     g_OrigTexStatusCreate = nullptr;

    bool DescriptorReadableSEH(const void* src)
    {
        const uintptr_t v = reinterpret_cast<uintptr_t>(src);
        if (v < 0x10000ull || v >= 0x7FFFFFFFFFFFull)
            return false;
        __try
        {
            volatile unsigned probe =
                *reinterpret_cast<const unsigned*>(
                    static_cast<const char*>(src) + 0x18);
            (void)probe;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    char __fastcall hkTexStatusCreate(void* out, void* src, unsigned flags)
    {
        if (src && !DescriptorReadableSEH(src))
        {
            static bool s_Warned = false;
            if (!s_Warned)
            {
                s_Warned = true;
                LogWarn("UiTextureGuard: corrupt texture descriptor %p, bind skipped, slot draws untextured", src);
            }
            return 0;
        }
        return g_OrigTexStatusCreate(out, src, flags);
    }

    constexpr uint8_t kTexStatusCreateEntry154[] = {
        0x40, 0x53, 0x48, 0x83, 0xEC, 0x30, 0x48, 0xC7, 0x44, 0x24, 0x20, 0xFE, 0xFF, 0xFF,
        0xFF, 0x41, 0x8B, 0xD8, 0x48, 0x85, 0xD2, 0x75, 0x04, 0x32, 0xC0, 0xEB, 0x35 };
    constexpr uint8_t kTexStatusCreateThunkEn153[] = { 0xE9, 0x7B, 0xEA, 0x99, 0x0B };
    constexpr uint8_t kTexStatusCreateThunkJp153[] = { 0xE9, 0xFB, 0xCC, 0x03, 0x0C };

    struct TexStatusCreateSite
    {
        uintptr_t      address;
        const uint8_t* entryBytes;
        size_t         entrySize;
    };

    TexStatusCreateSite TexStatusCreateSiteForBuild()
    {
        switch (gGameBuild)
        {
        case ::AddressSetRuntime::GameBuild::En_1_0_15_3:
            return { 0x141DBBD20ull, kTexStatusCreateThunkEn153, sizeof(kTexStatusCreateThunkEn153) };
        case ::AddressSetRuntime::GameBuild::Jp_1_0_15_3:
            return { 0x141DBBD60ull, kTexStatusCreateThunkJp153, sizeof(kTexStatusCreateThunkJp153) };
        case ::AddressSetRuntime::GameBuild::En_1_0_15_4a:
        case ::AddressSetRuntime::GameBuild::En_1_0_15_4:
            return { 0x141DBC2D0ull, kTexStatusCreateEntry154, sizeof(kTexStatusCreateEntry154) };
        case ::AddressSetRuntime::GameBuild::Jp_1_0_15_4a:
        case ::AddressSetRuntime::GameBuild::Jp_1_0_15_4:
            return { 0x141DBC330ull, kTexStatusCreateEntry154, sizeof(kTexStatusCreateEntry154) };
        default:
            return { 0, nullptr, 0 };
        }
    }

    bool EntryBytesMatchSEH(const void* entry, const uint8_t* bytes, size_t size)
    {
        __try
        {
            return std::memcmp(entry, bytes, size) == 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void InstallTextureStreamerGuard()
    {
        const TexStatusCreateSite site = TexStatusCreateSiteForBuild();
        void* target = ResolveGameAddress(site.address);
        if (!target)
            return;

        if (!EntryBytesMatchSEH(target, site.entryBytes, site.entrySize))
        {
            LogError("UiTextureGuard: TextureStreamerStatus entry bytes differ at %p, guard off", target);
            return;
        }

        if (!CreateAndEnableHook(
                target,
                reinterpret_cast<void*>(&hkTexStatusCreate),
                reinterpret_cast<void**>(&g_OrigTexStatusCreate)))
            LogError("UiTextureGuard: TextureStreamerStatus hook failed, corrupt UI texture will crash");
    }

    SetWeaponPanelLogo_t  g_OrigSetWeaponPanelLogo = nullptr;
    SetTextureName_t      g_SetTextureName = nullptr;
    GetUixUtility_t       g_GetUixUtility = nullptr;
    GetQuarkSystemTable_t g_GetQuarkSystemTable = nullptr;
    uint64_t              g_MaskSlot = 0;

    constexpr uint64_t MAIN_SLOT = 0x3BBF9889ull;

    constexpr uintptr_t NODE_COLOR_OFFSET = 0x50;

    constexpr uint64_t VANILLA_BG = 0x15695ED8A56AE919ull;

    constexpr uint64_t WHITE_MASK = 0x156B0FA3F5A52C33ull;

    struct BgTex { uint64_t hash = 0; bool colored = false; float opacity = 1.0f; };
    BgTex g_Default;
    BgTex g_EnemyDefault;
    std::unordered_map<int, BgTex> g_PerEquipTextures;
    std::unordered_map<int, BgTex> g_PerEnemyEquipTextures;
    std::unordered_map<void*, bool> g_TouchedNodes;


    uint16_t CurrentHandEquip()
    {
        if (!g_GetQuarkSystemTable)
            return 0;
        __try
        {
            char* quark = reinterpret_cast<char*>(g_GetQuarkSystemTable());
            if (!quark) return 0;
            const uintptr_t mgr = *reinterpret_cast<uintptr_t*>(quark + 0x98);
            if (!mgr) return 0;
            const uintptr_t vars = *reinterpret_cast<uintptr_t*>(mgr + 0x10);
            if (!vars) return 0;
            return *reinterpret_cast<uint16_t*>(vars + 0x5d4);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    }


    bool IsBionicArm(int equipId)
    {
        if (equipId >= 0x203 && equipId <= 0x207)
            return true;
        if (equipId >= 0x367 && equipId <= 0x36C)
            return true;
        const uint16_t hand = CurrentHandEquip();
        return hand != 0 && equipId == static_cast<int>(hand);
    }


    bool Resolve()
    {
        if (!g_SetTextureName)
            g_SetTextureName = reinterpret_cast<SetTextureName_t>(ResolveGameAddress(gAddr.SetTextureName));
        if (!g_GetQuarkSystemTable && gAddr.GetQuarkSystemTable != 0)
            g_GetQuarkSystemTable = reinterpret_cast<GetQuarkSystemTable_t>(ResolveGameAddress(gAddr.GetQuarkSystemTable));
        if (g_MaskSlot == 0)
            g_MaskSlot = static_cast<uint64_t>(FoxHashes::StrCode32("Mask_Texture"));
        return g_SetTextureName != nullptr && g_MaskSlot != 0;
    }


    void Prefetch(uint64_t textureHash)
    {
        if (textureHash == 0 || gAddr.GetUixUtilityToFeedQuarkEnvironment == 0)
            return;
        if (!g_GetUixUtility)
            g_GetUixUtility = reinterpret_cast<GetUixUtility_t>(ResolveGameAddress(gAddr.GetUixUtilityToFeedQuarkEnvironment));
        if (!g_GetUixUtility)
            return;
        __try
        {
            void** util = g_GetUixUtility();
            if (!util) return;
            void** vtbl = *reinterpret_cast<void***>(util);
            if (!vtbl) return;
            auto fn = reinterpret_cast<void(__fastcall*)(void*, uint64_t, int)>(vtbl[0x548 / sizeof(void*)]);
            if (fn) fn(util, textureHash, 2);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    bool IsSortieEquip(int equipId)
    {
        if (!g_GetQuarkSystemTable)
            return true;
        __try
        {
            char* quark = reinterpret_cast<char*>(g_GetQuarkSystemTable());
            if (!quark) return true;
            const uintptr_t mgr = *reinterpret_cast<uintptr_t*>(quark + 0x98);
            if (!mgr) return true;
            const uintptr_t lm = *reinterpret_cast<uintptr_t*>(mgr + 0x130);
            if (!lm) return true;
            const uint8_t idx = *reinterpret_cast<uint8_t*>(lm + 0x3b4);
            char* info = reinterpret_cast<char*>(lm + 0x10 + static_cast<uintptr_t>(idx) * 0xe8);

            if (*reinterpret_cast<int*>(info + 0x18) == equipId) return true;
            if (*reinterpret_cast<int*>(info + 0x2c) == equipId) return true;
            if (*reinterpret_cast<int*>(info + 0x40) == equipId) return true;
            for (int i = 0; i < 8; ++i)
                if (*reinterpret_cast<int*>(info + 0x54 + i * 4) == equipId) return true;
            for (int i = 0; i < 8; ++i)
                if (*reinterpret_cast<int*>(info + 0x74 + i * 4) == equipId) return true;
            return false;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return true; }
    }


    void SetNodeColor(void* node, float r, float g, float b, float a)
    {
        if (!node)
            return;
        __try
        {
            float* c = reinterpret_cast<float*>(reinterpret_cast<char*>(node) + NODE_COLOR_OFFSET);
            c[0] = r; c[1] = g; c[2] = b; c[3] = a;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }


    void ClearColorLayer(void* node)
    {
        const auto it = g_TouchedNodes.find(node);
        if (it == g_TouchedNodes.end())
            return;
        SetNodeColor(node, 1.0f, 1.0f, 1.0f, 1.0f);
        if (it->second)
        {
            Prefetch(VANILLA_BG);
            if (g_SetTextureName(node, VANILLA_BG, MAIN_SLOT, 2) == 0)
            {
                static bool s_Warned = false;
                if (!s_Warned)
                {
                    s_Warned = true;
                    LogWarn("EquipBg: vanilla bg restore bound nothing on node %p, retrying next bind", node);
                }
                return;
            }
            g_SetTextureName(node, VANILLA_BG, g_MaskSlot, 2);
        }
        g_TouchedNodes.erase(it);
    }


    void Apply(void* node, const BgTex& t)
    {
        Prefetch(t.hash);
        bool mainBound = false;
        if (t.colored)
        {
            Prefetch(WHITE_MASK);
            if (g_SetTextureName(node, WHITE_MASK, g_MaskSlot, 2) == 0)
            {
                static bool s_MaskWarned = false;
                if (!s_MaskWarned)
                {
                    s_MaskWarned = true;
                    LogWarn("EquipBg: white mask bound nothing on node %p, colored bg black stays invisible", node);
                }
                g_SetTextureName(node, t.hash, g_MaskSlot, 2);
            }
            mainBound = g_SetTextureName(node, t.hash, MAIN_SLOT, 2) != 0;
            if (!mainBound)
            {
                static bool s_Warned = false;
                if (!s_Warned)
                {
                    s_Warned = true;
                    LogWarn("EquipBg: colored bind of 0x%llX bound nothing on node %p, drawn monochrome",
                        static_cast<unsigned long long>(t.hash), node);
                }
                g_SetTextureName(node, t.hash, g_MaskSlot, 2);
            }
        }
        else
        {
            g_SetTextureName(node, t.hash, g_MaskSlot, 2);
        }
        const auto it = g_TouchedNodes.find(node);
        const bool mainHoldsCustom = mainBound || (it != g_TouchedNodes.end() && it->second);
        if (mainHoldsCustom || t.opacity != 1.0f)
        {
            SetNodeColor(node, 1.0f, 1.0f, 1.0f, t.opacity);
            g_TouchedNodes[node] = mainHoldsCustom;
        }
    }


    uint8_t EquipBgApply(int equipId, void* node, SetWeaponPanelLogo_t orig)
    {
        if (node && Resolve())
        {
            if (equipId == 0)
            {
                const auto it = g_PerEquipTextures.find(0);
                if (it != g_PerEquipTextures.end())
                {
                    Apply(node, it->second);
                    return 1;
                }
                ClearColorLayer(node);
                return orig(equipId, node);
            }

            if (IsBionicArm(equipId))
            {
                const auto it = g_PerEquipTextures.find(equipId);
                if (it != g_PerEquipTextures.end())
                {
                    Apply(node, it->second);
                    return 1;
                }
                ClearColorLayer(node);
                return orig(equipId, node);
            }

            const bool sortie = IsSortieEquip(equipId);

            const BgTex* t = nullptr;
            if (sortie)
            {
                const auto it = g_PerEquipTextures.find(equipId);
                if (it != g_PerEquipTextures.end())
                    t = &it->second;
                else if (g_Default.hash != 0)
                    t = &g_Default;
            }
            else
            {
                const auto it = g_PerEnemyEquipTextures.find(equipId);
                if (it != g_PerEnemyEquipTextures.end())
                    t = &it->second;
                else if (g_EnemyDefault.hash != 0)
                    t = &g_EnemyDefault;
            }

            if (t != nullptr)
            {
                Apply(node, *t);
                return 1;
            }

            ClearColorLayer(node);
        }
        return orig(equipId, node);
    }


    uint8_t __fastcall hkSetWeaponPanelLogo(int equipId, void* node)
    {
        if (MissionCodeGuard::ShouldBypassHooks())
        {
            if (node && Resolve())
                ClearColorLayer(node);
            return g_OrigSetWeaponPanelLogo(equipId, node);
        }
        return EquipBgApply(equipId, node, g_OrigSetWeaponPanelLogo);
    }
}


void EquipBg_SetDefaultTexture(uint64_t textureHash, bool colored, float opacity)
{
    g_Default = { textureHash, colored, opacity };
    Prefetch(textureHash);
}

void EquipBg_ClearDefaultTexture()
{
    g_Default = {};
}

void EquipBg_SetEquipTexture(int equipId, uint64_t textureHash, bool colored, float opacity)
{
    g_PerEquipTextures[equipId] = { textureHash, colored, opacity };
    Prefetch(textureHash);
}

void EquipBg_ClearEquipTexture(int equipId)
{
    g_PerEquipTextures.erase(equipId);
}

void EquipBg_SetEnemyWeaponTexture(uint64_t textureHash, bool colored, float opacity)
{
    g_EnemyDefault = { textureHash, colored, opacity };
    Prefetch(textureHash);
}

void EquipBg_ClearEnemyWeaponTexture()
{
    g_EnemyDefault = {};
}

void EquipBg_SetEnemyEquipTexture(int equipId, uint64_t textureHash, bool colored, float opacity)
{
    g_PerEnemyEquipTextures[equipId] = { textureHash, colored, opacity };
    Prefetch(textureHash);
}

void EquipBg_ClearEnemyEquipTexture(int equipId)
{
    g_PerEnemyEquipTextures.erase(equipId);
}

void EquipBg_ClearAllEquipTextures()
{
    g_PerEquipTextures.clear();
    g_PerEnemyEquipTextures.clear();
}


bool Install_EquipBgTexture_Hook()
{
    void* target = ResolveGameAddress(gAddr.SetEquipBackgroundTexture);
    if (!target)
        return false;

    const bool ok = CreateAndEnableHook(
        target,
        reinterpret_cast<void*>(&hkSetWeaponPanelLogo),
        reinterpret_cast<void**>(&g_OrigSetWeaponPanelLogo));

    Resolve();

    InstallTextureStreamerGuard();

    if (!ok)
        LogError("EquipBgTexture: hook failed, custom equip backgrounds off");
    return ok;
}


bool Uninstall_EquipBgTexture_Hook()
{
    DisableAndRemoveHook(ResolveGameAddress(gAddr.SetEquipBackgroundTexture));

    if (g_OrigTexStatusCreate)
    {
        DisableAndRemoveHook(ResolveGameAddress(TexStatusCreateSiteForBuild().address));
        g_OrigTexStatusCreate = nullptr;
    }

    g_OrigSetWeaponPanelLogo = nullptr;
    g_TouchedNodes.clear();
    return true;
}
