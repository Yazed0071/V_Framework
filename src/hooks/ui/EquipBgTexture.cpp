#include "pch.h"
#include "EquipBgTexture.h"

#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "HookUtils.h"
#include "log.h"
#include "AddressSet.h"
#include "MissionCodeGuard.h"
#include "UiTexturePrefetch.h"
#include "EquipAliasLease.h"

namespace
{
    using SetWeaponPanelLogo_t  = uint8_t(__fastcall*)(int equipId, void* node);
    using SetTextureName_t      = uint8_t(__fastcall*)(void* modelNodeMesh, uint64_t textureHash, uint64_t slotHash, int level);
    using GetQuarkSystemTable_t = void* (__fastcall*)();
    using IsReadyTexture_t      = char(__fastcall*)(void* controller, void* texture, unsigned level);

    IsReadyTexture_t g_OrigIsReadyTexture = nullptr;

    bool TextureReadableSEH(const void* texture)
    {
        const uintptr_t v = reinterpret_cast<uintptr_t>(texture);
        if (v < 0x10000ull || v >= 0x7FFFFFFFFFFFull)
            return false;
        __try
        {
            volatile unsigned probe =
                *reinterpret_cast<const unsigned*>(
                    static_cast<const char*>(texture) + 0x18); // texture manager handle
            (void)probe;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    char __fastcall hkIsReadyTexture(void* controller, void* texture, unsigned level)
    {
        if (texture && !TextureReadableSEH(texture))
        {
            static std::atomic<bool> s_Warned{ false };
            if (!s_Warned.exchange(true))
                LogWarn("UiTextureGuard: corrupt texture %p, bind skipped, slot shows the loading placeholder", texture);
            return 0;
        }
        return g_OrigIsReadyTexture(controller, texture, level);
    }

    constexpr uint8_t kIsReadyTextureEntry154[] = {
        0x40, 0x53, 0x48, 0x83, 0xEC, 0x30, 0x48, 0xC7, 0x44, 0x24, 0x20, 0xFE, 0xFF, 0xFF,
        0xFF, 0x41, 0x8B, 0xD8, 0x48, 0x85, 0xD2, 0x75, 0x04, 0x32, 0xC0, 0xEB, 0x35 };
    constexpr uint8_t kIsReadyTextureThunkEn153[] = { 0xE9, 0x7B, 0xEA, 0x99, 0x0B };
    constexpr uint8_t kIsReadyTextureThunkJp153[] = { 0xE9, 0xFB, 0xCC, 0x03, 0x0C };

    struct IsReadyTextureSite
    {
        uintptr_t      address;
        const uint8_t* entryBytes;
        size_t         entrySize;
    };

    IsReadyTextureSite IsReadyTextureSiteForBuild()
    {
        switch (gGameBuild)
        {
        case ::AddressSetRuntime::GameBuild::En_1_0_15_3:
            return { 0x141DBBD20ull, kIsReadyTextureThunkEn153, sizeof(kIsReadyTextureThunkEn153) };
        case ::AddressSetRuntime::GameBuild::Jp_1_0_15_3:
            return { 0x141DBBD60ull, kIsReadyTextureThunkJp153, sizeof(kIsReadyTextureThunkJp153) };
        case ::AddressSetRuntime::GameBuild::En_1_0_15_4a:
        case ::AddressSetRuntime::GameBuild::En_1_0_15_4:
            return { 0x141DBC2D0ull, kIsReadyTextureEntry154, sizeof(kIsReadyTextureEntry154) };
        case ::AddressSetRuntime::GameBuild::Jp_1_0_15_4a:
        case ::AddressSetRuntime::GameBuild::Jp_1_0_15_4:
            return { 0x141DBC330ull, kIsReadyTextureEntry154, sizeof(kIsReadyTextureEntry154) };
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

    void InstallTextureGuard()
    {
        const IsReadyTextureSite site = IsReadyTextureSiteForBuild();
        void* target = ResolveGameAddress(site.address);
        if (!target)
            return;

        if (!EntryBytesMatchSEH(target, site.entryBytes, site.entrySize))
        {
            LogError("UiTextureGuard: IsReadyTexture entry bytes differ at %p, guard off", target);
            return;
        }

        if (!CreateAndEnableHook(
                target,
                reinterpret_cast<void*>(&hkIsReadyTexture),
                reinterpret_cast<void**>(&g_OrigIsReadyTexture)))
            LogError("UiTextureGuard: IsReadyTexture hook failed, corrupt UI texture will crash");
    }

    SetWeaponPanelLogo_t         g_OrigSetWeaponPanelLogo = nullptr;
    SetTextureName_t             g_SetTextureName = nullptr;
    GetQuarkSystemTable_t        g_GetQuarkSystemTable = nullptr;

    constexpr uint64_t MASK_SLOT = 0xBCAE534Bull; // Mask_Texture
    constexpr uint64_t BASE_SLOT = 0x3BBF9889ull; // Base_Texture

    constexpr uint64_t FLAT_WHITE = 0x156B0FA3F5A52C33ull; // /Assets/fox/ui/prefab/ModelAsset/prefab/Pictures/ui_prefab_flat_white.ftex

    constexpr uintptr_t NODE_SCALE      = 0x40; // x, y, z, w floats
    constexpr uintptr_t NODE_COLOR      = 0x50; // rgba floats
    constexpr uintptr_t NODE_BLEND      = 0x84; // 1 alpha blend, 2 additive (the logo plate is additive, so black never shows)
    constexpr uint8_t   BLEND_ALPHA     = 1;
    constexpr uintptr_t NODE_SLOT_COUNT = 0xD8;
    constexpr uintptr_t NODE_SLOTS      = 0xE0; // 0x20 per slot: slot hash, flags, texture at +8
    constexpr uintptr_t NODE_SLOT_SIZE  = 0x20;
    constexpr uintptr_t SLOT_FLAGS      = 0x04;
    constexpr uintptr_t SLOT_TEXTURE    = 0x08;
    constexpr uintptr_t TEXTURE_PATH    = 0x10; // fox::Path, first qword is the path hash
    constexpr uint32_t  SLOT_LOCKED     = 0x100000; // layout resets keep this slot's texture instead of the layout default

    constexpr uintptr_t QUARK_GAME            = 0x98;   // game side of the quark table
    constexpr uintptr_t GAME_VARS             = 0x10;   // script vars
    constexpr uintptr_t GAME_LOADOUTS         = 0x130;  // loadout manager
    constexpr uintptr_t VARS_HAND_EQUIP       = 0x5D4;  // vars.handEquip
    constexpr uintptr_t VARS_SORTIE_SLOT      = 0x1528; // vars.currentSortieLoadoutSlot
    constexpr uintptr_t LOADOUTS_READY        = 0xAF8;  // set once the loadouts are read from vars
    constexpr uintptr_t LOADOUTS_IN_MISSION   = 0x3B8;  // 4 in-mission loadouts, pickups and supply drops land here
    constexpr uintptr_t LOADOUT_SIZE          = 0xE8;
    constexpr uintptr_t LOADOUT_WEAPONS       = 0x18;   // primary, primary 2, secondary, 0x14 apart
    constexpr uintptr_t LOADOUT_WEAPON_STRIDE = 0x14;
    constexpr uintptr_t LOADOUT_SUPPORTS      = 0x54;   // 8 support weapons
    constexpr uintptr_t LOADOUT_ITEMS         = 0x74;   // 8 items

    constexpr int HAND_FIRST         = 0x203;
    constexpr int HAND_LAST          = 0x209;
    constexpr int CHIMERA_FIRST      = 0x367; // customized weapon in hand: 0x367 + 2 * slot + player
    constexpr int CHIMERA_LAST       = 0x36C;
    constexpr int ENEMY_GUN_FIRST    = 0x01;  // enemy-only guns and shields, vanilla draws no logo for them
    constexpr int ENEMY_GUN_LAST     = 0x48;
    constexpr int ENEMY_SHIELD_FIRST = 0x7D;
    constexpr int ENEMY_SHIELD_LAST  = 0x80;

    constexpr size_t NODE_PRUNE_AT = 256;

    struct BgTex { uint64_t hash = 0; bool colored = false; float opacity = 1.0f; float scaleX = 1.0f; float scaleY = 1.0f; };

    struct NodeState
    {
        bool     colorOurs = false;
        float    origColor[4] = {};
        float    wroteColor[4] = {};
        bool     maskOurs = false;
        uint64_t boundMask = 0;
        bool     baseOurs = false;
        uint64_t origBase = 0;
        uint64_t boundBase = 0;
        bool     blendOurs = false;
        uint8_t  origBlend = 0;
        bool     scaleOurs = false;
        float    origScale[2] = {};
        float    wroteScale[2] = {};
    };

    std::mutex g_Lock;
    BgTex g_Default;
    BgTex g_EnemyDefault;
    std::unordered_map<int, BgTex> g_PerEquipTextures;
    std::unordered_map<int, BgTex> g_PerEnemyEquipTextures;
    std::unordered_map<void*, NodeState> g_Nodes;


    void Prefetch(uint64_t textureHash)
    {
        UiTexturePrefetch::Prefetch(textureHash);
    }

    void DeletePrefetches(const std::vector<uint64_t>& hashes)
    {
        for (const uint64_t hash : hashes)
            UiTexturePrefetch::Release(hash);
    }

    void ReleasePrefetchRef(uint64_t hash, std::vector<uint64_t>& released)
    {
        if (hash != 0)
            released.push_back(hash);
    }

    void ReplaceTexture(BgTex& slot, const BgTex& value, std::vector<uint64_t>& released)
    {
        ReleasePrefetchRef(slot.hash, released);
        slot = value;
    }


    char* GameSideSEH()
    {
        __try
        {
            char* quark = reinterpret_cast<char*>(g_GetQuarkSystemTable());
            if (!quark) return nullptr;
            return *reinterpret_cast<char**>(quark + QUARK_GAME);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    }

    uint16_t CurrentHandEquipSEH()
    {
        if (!g_GetQuarkSystemTable)
            return 0;
        __try
        {
            char* game = GameSideSEH();
            if (!game) return 0;
            char* vars = *reinterpret_cast<char**>(game + GAME_VARS);
            if (!vars) return 0;
            return *reinterpret_cast<uint16_t*>(vars + VARS_HAND_EQUIP);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    }

    const char* InMissionLoadoutSEH()
    {
        if (!g_GetQuarkSystemTable)
            return nullptr;
        __try
        {
            char* game = GameSideSEH();
            if (!game) return nullptr;
            char* vars = *reinterpret_cast<char**>(game + GAME_VARS);
            char* loadouts = *reinterpret_cast<char**>(game + GAME_LOADOUTS);
            if (!vars || !loadouts) return nullptr;
            if (*reinterpret_cast<uint8_t*>(loadouts + LOADOUTS_READY) == 0) return nullptr;
            const uint8_t slot = *reinterpret_cast<uint8_t*>(vars + VARS_SORTIE_SLOT);
            if (slot > 3) return nullptr;
            return loadouts + LOADOUTS_IN_MISSION + slot * LOADOUT_SIZE;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    }

    bool LoadoutHoldsSEH(const char* loadout, int equipId)
    {
        __try
        {
            // the loadout holds the alias of a wide equipId, equipId here is the real one
            for (int i = 0; i < 3; ++i)
                if (equip::FromEngine(*reinterpret_cast<const int*>(loadout + LOADOUT_WEAPONS + i * LOADOUT_WEAPON_STRIDE)) == equipId) return true;
            for (int i = 0; i < 8; ++i)
                if (equip::FromEngine(*reinterpret_cast<const int*>(loadout + LOADOUT_SUPPORTS + i * 4)) == equipId) return true;
            for (int i = 0; i < 8; ++i)
                if (equip::FromEngine(*reinterpret_cast<const int*>(loadout + LOADOUT_ITEMS + i * 4)) == equipId) return true;
            return false;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return true; }
    }

    int ChimeraRealIdSEH(const char* loadout, int equipId)
    {
        const int index = equipId - CHIMERA_FIRST;
        if (index & 1)
            return equipId;
        __try
        {
            const int real = *reinterpret_cast<const int*>(loadout + LOADOUT_WEAPONS + (index >> 1) * LOADOUT_WEAPON_STRIDE);
            return real > 0 ? equip::FromEngine(real) : equipId;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return equipId; }
    }

    bool IsHand(int equipId)
    {
        if (equipId >= HAND_FIRST && equipId <= HAND_LAST)
            return true;
        const uint16_t hand = CurrentHandEquipSEH();
        return hand != 0 && equipId == static_cast<int>(hand);
    }

    bool IsEnemyOnly(int equipId)
    {
        return (equipId >= ENEMY_GUN_FIRST && equipId <= ENEMY_GUN_LAST)
            || (equipId >= ENEMY_SHIELD_FIRST && equipId <= ENEMY_SHIELD_LAST);
    }

    bool PickTexture(int engineEquipId, BgTex& out)
    {
        const int equipId = equip::FromEngine(engineEquipId);
        const auto findIn = [&out](const std::unordered_map<int, BgTex>& table, int id)
        {
            const auto it = table.find(id);
            if (it == table.end())
                return false;
            out = it->second;
            return true;
        };

        if (equipId == 0 || IsHand(equipId))
            return findIn(g_PerEquipTextures, equipId);

        bool player = false;
        int playerId = equipId;
        if (!IsEnemyOnly(equipId))
        {
            const char* loadout = InMissionLoadoutSEH();
            if (equipId >= CHIMERA_FIRST && equipId <= CHIMERA_LAST)
            {
                player = true;
                if (loadout)
                    playerId = ChimeraRealIdSEH(loadout, equipId);
            }
            else
            {
                player = !loadout || LoadoutHoldsSEH(loadout, equipId);
            }
        }

        if (player)
        {
            if (findIn(g_PerEquipTextures, playerId))
                return true;
            out = g_Default;
            return g_Default.hash != 0;
        }

        if (findIn(g_PerEnemyEquipTextures, equipId))
            return true;
        out = g_EnemyDefault;
        return g_EnemyDefault.hash != 0;
    }


    bool ReadColorSEH(void* node, float* out)
    {
        __try
        {
            std::memcpy(out, static_cast<char*>(node) + NODE_COLOR, sizeof(float) * 4);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    void WriteColorSEH(void* node, const float* color)
    {
        __try
        {
            std::memcpy(static_cast<char*>(node) + NODE_COLOR, color, sizeof(float) * 4);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    bool ReadScaleSEH(void* node, float* out)
    {
        __try
        {
            std::memcpy(out, static_cast<char*>(node) + NODE_SCALE, sizeof(float) * 2);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    void WriteScaleSEH(void* node, const float* scale)
    {
        __try
        {
            std::memcpy(static_cast<char*>(node) + NODE_SCALE, scale, sizeof(float) * 2);
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    uint64_t SlotTextureHashSEH(void* node, uint32_t slotHash)
    {
        __try
        {
            const int count = *reinterpret_cast<int*>(static_cast<char*>(node) + NODE_SLOT_COUNT);
            const char* slots = *reinterpret_cast<char**>(static_cast<char*>(node) + NODE_SLOTS);
            if (!slots || count <= 0 || count > 64)
                return 0;
            for (int i = 0; i < count; ++i)
            {
                const char* slot = slots + i * NODE_SLOT_SIZE;
                if (*reinterpret_cast<const uint32_t*>(slot) != slotHash)
                    continue;
                const char* texture = *reinterpret_cast<char* const*>(slot + SLOT_TEXTURE);
                return texture ? *reinterpret_cast<const uint64_t*>(texture + TEXTURE_PATH) : 0;
            }
            return 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    }

    void SetSlotLockedSEH(void* node, uint32_t slotHash, bool locked)
    {
        __try
        {
            const int count = *reinterpret_cast<int*>(static_cast<char*>(node) + NODE_SLOT_COUNT);
            char* slots = *reinterpret_cast<char**>(static_cast<char*>(node) + NODE_SLOTS);
            if (!slots || count <= 0 || count > 64)
                return;
            for (int i = 0; i < count; ++i)
            {
                char* slot = slots + i * NODE_SLOT_SIZE;
                if (*reinterpret_cast<uint32_t*>(slot) != slotHash)
                    continue;
                uint32_t& flags = *reinterpret_cast<uint32_t*>(slot + SLOT_FLAGS);
                flags = locked ? (flags | SLOT_LOCKED) : (flags & ~SLOT_LOCKED);
                return;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    bool Bind(void* node, uint64_t textureHash, uint64_t slotHash)
    {
        return g_SetTextureName(node, textureHash, slotHash, 2) != 0;
    }

    bool BindLocked(void* node, uint64_t textureHash, uint64_t slotHash)
    {
        if (!Bind(node, textureHash, slotHash))
            return false;
        SetSlotLockedSEH(node, static_cast<uint32_t>(slotHash), true);
        return true;
    }

    bool ReadBlendSEH(void* node, uint8_t& out)
    {
        __try
        {
            out = *(static_cast<uint8_t*>(node) + NODE_BLEND);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    void WriteBlendSEH(void* node, uint8_t blend)
    {
        __try { *(static_cast<uint8_t*>(node) + NODE_BLEND) = blend; }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    void UseAlphaBlend(void* node, NodeState& state)
    {
        if (state.blendOurs)
            return;
        uint8_t current = 0;
        if (!ReadBlendSEH(node, current) || current == BLEND_ALPHA)
            return;
        state.origBlend = current;
        WriteBlendSEH(node, BLEND_ALPHA);
        state.blendOurs = true;
    }

    void RestoreBlend(void* node, NodeState& state)
    {
        if (!state.blendOurs)
            return;
        uint8_t current = 0;
        if (ReadBlendSEH(node, current) && current == BLEND_ALPHA)
            WriteBlendSEH(node, state.origBlend);
        state.blendOurs = false;
    }

    void WriteColor(void* node, NodeState& state, float opacity)
    {
        const float color[4] = { 1.0f, 1.0f, 1.0f, opacity };
        if (!state.colorOurs)
        {
            if (!ReadColorSEH(node, state.origColor))
                return;
            state.colorOurs = true;
        }
        WriteColorSEH(node, color);
        std::memcpy(state.wroteColor, color, sizeof(color));
    }

    void RestoreColor(void* node, NodeState& state)
    {
        if (!state.colorOurs)
            return;
        float current[4];
        if (ReadColorSEH(node, current) && std::memcmp(current, state.wroteColor, sizeof(current)) == 0)
            WriteColorSEH(node, state.origColor);
        state.colorOurs = false;
    }

    void WriteScale(void* node, NodeState& state, float scaleX, float scaleY)
    {
        if (!state.scaleOurs)
        {
            if (!ReadScaleSEH(node, state.origScale))
                return;
            state.scaleOurs = true;
        }
        const float scale[2] = { state.origScale[0] * scaleX, state.origScale[1] * scaleY };
        WriteScaleSEH(node, scale);
        std::memcpy(state.wroteScale, scale, sizeof(scale));
    }

    void RestoreScale(void* node, NodeState& state)
    {
        if (!state.scaleOurs)
            return;
        float current[2];
        if (ReadScaleSEH(node, current) && std::memcmp(current, state.wroteScale, sizeof(current)) == 0)
            WriteScaleSEH(node, state.origScale);
        state.scaleOurs = false;
    }

    bool RestoreBase(void* node, NodeState& state)
    {
        if (!state.baseOurs)
            return true;

        const uint64_t current = SlotTextureHashSEH(node, static_cast<uint32_t>(BASE_SLOT));
        if (current != 0 && current != state.boundBase)
        {
            SetSlotLockedSEH(node, static_cast<uint32_t>(BASE_SLOT), false);
            state.baseOurs = false;
            return true;
        }

        uint64_t original = state.origBase;
        if (original == 0)
        {
            static std::atomic<bool> s_Warned{ false };
            if (!s_Warned.exchange(true))
                LogWarn("EquipBg: logo node %p has no Base_Texture entry, colour layer reset to flat white", node);
            Prefetch(FLAT_WHITE);
            original = FLAT_WHITE;
        }
        if (!Bind(node, original, BASE_SLOT))
            return false;
        SetSlotLockedSEH(node, static_cast<uint32_t>(BASE_SLOT), false);
        state.baseOurs = false;
        return true;
    }

    void ReleaseMask(void* node, NodeState& state)
    {
        if (!state.maskOurs)
            return;
        SetSlotLockedSEH(node, static_cast<uint32_t>(MASK_SLOT), false);
        state.maskOurs = false;
    }

    bool StillOurs(void* node, const NodeState& state)
    {
        float color[4];
        if (state.colorOurs && ReadColorSEH(node, color) && std::memcmp(color, state.wroteColor, sizeof(color)) == 0)
            return true;
        if (state.maskOurs && SlotTextureHashSEH(node, static_cast<uint32_t>(MASK_SLOT)) == state.boundMask)
            return true;
        float scale[2];
        if (state.scaleOurs && ReadScaleSEH(node, scale) && std::memcmp(scale, state.wroteScale, sizeof(scale)) == 0)
            return true;
        if (state.baseOurs)
        {
            const uint64_t current = SlotTextureHashSEH(node, static_cast<uint32_t>(BASE_SLOT));
            if (current == state.boundBase || (current == 0 && state.origBase == 0))
                return true;
        }
        return false;
    }

    void PruneNodes(void* keep)
    {
        for (auto it = g_Nodes.begin(); it != g_Nodes.end();)
        {
            if (it->first != keep && !StillOurs(it->first, it->second))
                it = g_Nodes.erase(it);
            else
                ++it;
        }
    }

    NodeState& StateFor(void* node)
    {
        const auto it = g_Nodes.find(node);
        if (it == g_Nodes.end())
            return g_Nodes[node];
        if (!StillOurs(node, it->second))
            it->second = NodeState{};
        return it->second;
    }

    void WarnBindFailed(uint64_t textureHash, void* node)
    {
        static std::atomic<bool> s_Warned{ false };
        if (!s_Warned.exchange(true))
            LogWarn("EquipBg: background 0x%llX did not bind on node %p, vanilla background shown",
                static_cast<unsigned long long>(textureHash), node);
    }

    void RestoreNode(void* node)
    {
        const auto it = g_Nodes.find(node);
        if (it == g_Nodes.end())
            return;
        if (!StillOurs(node, it->second))
        {
            g_Nodes.erase(it);
            return;
        }
        ReleaseMask(node, it->second);
        RestoreBlend(node, it->second);
        RestoreScale(node, it->second);
        RestoreColor(node, it->second);
        if (!RestoreBase(node, it->second))
        {
            static std::atomic<bool> s_Warned{ false };
            if (!s_Warned.exchange(true))
                LogWarn("EquipBg: colour layer restore did not bind on node %p, retrying next refresh", node);
            return;
        }
        g_Nodes.erase(it);
    }

    bool Apply(void* node, const BgTex& t)
    {
        Prefetch(t.hash);
        NodeState& state = StateFor(node);

        const uint64_t mask = t.colored ? FLAT_WHITE : t.hash;
        if (t.colored)
            Prefetch(FLAT_WHITE);
        bool bound = BindLocked(node, mask, MASK_SLOT);
        if (bound)
        {
            state.maskOurs = true;
            state.boundMask = mask;
            if (t.colored)
            {
                if (!state.baseOurs)
                    state.origBase = SlotTextureHashSEH(node, static_cast<uint32_t>(BASE_SLOT));
                bound = BindLocked(node, t.hash, BASE_SLOT);
                if (bound)
                {
                    state.baseOurs = true;
                    state.boundBase = t.hash;
                }
            }
            else
            {
                bound = RestoreBase(node, state);
            }
        }

        if (!bound)
        {
            RestoreNode(node);
            WarnBindFailed(t.hash, node);
            return false;
        }

        if (t.colored)
            UseAlphaBlend(node, state);
        else
            RestoreBlend(node, state);

        if (t.colored || t.opacity != 1.0f)
            WriteColor(node, state, t.opacity);
        else
            RestoreColor(node, state);

        if (t.scaleX != 1.0f || t.scaleY != 1.0f)
            WriteScale(node, state, t.scaleX, t.scaleY);
        else
            RestoreScale(node, state);

        if (g_Nodes.size() > NODE_PRUNE_AT)
            PruneNodes(node);
        return true;
    }

    bool Resolve()
    {
        if (!g_SetTextureName)
            g_SetTextureName = reinterpret_cast<SetTextureName_t>(ResolveGameAddress(gAddr.SetTextureName));
        if (!g_GetQuarkSystemTable && gAddr.GetQuarkSystemTable != 0)
            g_GetQuarkSystemTable = reinterpret_cast<GetQuarkSystemTable_t>(ResolveGameAddress(gAddr.GetQuarkSystemTable));
        return g_SetTextureName != nullptr;
    }

    uint8_t __fastcall hkSetWeaponPanelLogo(int equipId, void* node)
    {
        if (node)
        {
            const bool bypass = MissionCodeGuard::ShouldBypassHooks();
            std::lock_guard<std::mutex> lock(g_Lock);
            if (Resolve())
            {
                BgTex t;
                if (!bypass && PickTexture(equipId, t) && Apply(node, t))
                    return 1;
                RestoreNode(node);
            }
        }
        return g_OrigSetWeaponPanelLogo(equipId, node);
    }
}


void EquipBg_SetDefaultTexture(uint64_t textureHash, bool colored, float opacity, float scaleX, float scaleY)
{
    std::vector<uint64_t> released;
    {
        std::lock_guard<std::mutex> lock(g_Lock);
        ReplaceTexture(g_Default, { textureHash, colored, opacity, scaleX, scaleY }, released);
    }
    UiTexturePrefetch::Acquire(textureHash);
    DeletePrefetches(released);
}

void EquipBg_ClearDefaultTexture()
{
    std::vector<uint64_t> released;
    {
        std::lock_guard<std::mutex> lock(g_Lock);
        ReplaceTexture(g_Default, {}, released);
    }
    DeletePrefetches(released);
}

void EquipBg_SetEquipTexture(int equipId, uint64_t textureHash, bool colored, float opacity, float scaleX, float scaleY)
{
    std::vector<uint64_t> released;
    {
        std::lock_guard<std::mutex> lock(g_Lock);
        ReplaceTexture(g_PerEquipTextures[equipId], { textureHash, colored, opacity, scaleX, scaleY }, released);
    }
    UiTexturePrefetch::Acquire(textureHash);
    DeletePrefetches(released);
}

void EquipBg_ClearEquipTexture(int equipId)
{
    std::vector<uint64_t> released;
    {
        std::lock_guard<std::mutex> lock(g_Lock);
        const auto it = g_PerEquipTextures.find(equipId);
        if (it == g_PerEquipTextures.end())
            return;
        ReleasePrefetchRef(it->second.hash, released);
        g_PerEquipTextures.erase(it);
    }
    DeletePrefetches(released);
}

void EquipBg_SetEnemyWeaponTexture(uint64_t textureHash, bool colored, float opacity, float scaleX, float scaleY)
{
    std::vector<uint64_t> released;
    {
        std::lock_guard<std::mutex> lock(g_Lock);
        ReplaceTexture(g_EnemyDefault, { textureHash, colored, opacity, scaleX, scaleY }, released);
    }
    UiTexturePrefetch::Acquire(textureHash);
    DeletePrefetches(released);
}

void EquipBg_ClearEnemyWeaponTexture()
{
    std::vector<uint64_t> released;
    {
        std::lock_guard<std::mutex> lock(g_Lock);
        ReplaceTexture(g_EnemyDefault, {}, released);
    }
    DeletePrefetches(released);
}

void EquipBg_SetEnemyEquipTexture(int equipId, uint64_t textureHash, bool colored, float opacity, float scaleX, float scaleY)
{
    std::vector<uint64_t> released;
    {
        std::lock_guard<std::mutex> lock(g_Lock);
        ReplaceTexture(g_PerEnemyEquipTextures[equipId], { textureHash, colored, opacity, scaleX, scaleY }, released);
    }
    UiTexturePrefetch::Acquire(textureHash);
    DeletePrefetches(released);
}

void EquipBg_ClearEnemyEquipTexture(int equipId)
{
    std::vector<uint64_t> released;
    {
        std::lock_guard<std::mutex> lock(g_Lock);
        const auto it = g_PerEnemyEquipTextures.find(equipId);
        if (it == g_PerEnemyEquipTextures.end())
            return;
        ReleasePrefetchRef(it->second.hash, released);
        g_PerEnemyEquipTextures.erase(it);
    }
    DeletePrefetches(released);
}

void EquipBg_ClearAllEquipTextures()
{
    std::vector<uint64_t> released;
    {
        std::lock_guard<std::mutex> lock(g_Lock);
        ReplaceTexture(g_Default, {}, released);
        ReplaceTexture(g_EnemyDefault, {}, released);
        for (const auto& entry : g_PerEquipTextures)
            ReleasePrefetchRef(entry.second.hash, released);
        for (const auto& entry : g_PerEnemyEquipTextures)
            ReleasePrefetchRef(entry.second.hash, released);
        g_PerEquipTextures.clear();
        g_PerEnemyEquipTextures.clear();
    }
    DeletePrefetches(released);
}


bool Install_EquipBgTexture_Hook()
{
    void* target = ResolveGameAddress(gAddr.SetEquipBackgroundTexture);
    if (!target)
        return false;

    {
        std::lock_guard<std::mutex> lock(g_Lock);
        Resolve();
    }

    const bool ok = CreateAndEnableHook(
        target,
        reinterpret_cast<void*>(&hkSetWeaponPanelLogo),
        reinterpret_cast<void**>(&g_OrigSetWeaponPanelLogo));

    InstallTextureGuard();

    if (!ok)
        LogError("EquipBgTexture: hook failed, custom equip backgrounds off");
    return ok;
}


bool Uninstall_EquipBgTexture_Hook()
{
    DisableAndRemoveHook(ResolveGameAddress(gAddr.SetEquipBackgroundTexture));

    if (g_OrigIsReadyTexture)
    {
        DisableAndRemoveHook(ResolveGameAddress(IsReadyTextureSiteForBuild().address));
        g_OrigIsReadyTexture = nullptr;
    }

    g_OrigSetWeaponPanelLogo = nullptr;
    std::lock_guard<std::mutex> lock(g_Lock);
    g_Nodes.clear();
    return true;
}
