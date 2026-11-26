#include <pch.h>

#include "BattleGearEffects.h"

#include <Windows.h>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <cstring>
#include <initializer_list>
#include <mutex>

#include "AddressSet.h"
#include "BattleGearLegs.h"
#include "BattleGearWeapons.h"
#include "FoxHashes.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    using CreateEffect_t  = void**(__fastcall*)(void** effect, const void* desc);
    using EffectMatrix_t  = std::uint32_t*(__fastcall*)(void* effect, std::uint32_t* err, const float* matrix);
    using EffectDestroy_t = std::uint32_t*(__fastcall*)(void* effect, std::uint32_t* err);
    using EffectFlag_t    = std::uint32_t*(__fastcall*)(void* effect, std::uint32_t* err, bool on);
    using SharedAssign_t  = void(__fastcall*)(void** slot, void* value);
    using BuddyFile_t     = void*(__fastcall*)(void* controller, std::uint32_t group, std::uint64_t pathId);
    using SetVariable_t   = void*(__fastcall*)(void* effect, void* result, std::uint64_t name, const void* value);
    using QuarkTable_t    = std::uint8_t*(__fastcall*)();
    using EjectCasing_t   = void(__fastcall*)(void* casings, const void* desc);

    constexpr std::uint32_t kMainPackGroup    = 0;        // buddy block group of the main pack, buddy_btg_00
    constexpr std::size_t   kControllerImpl   = 0x10;
    constexpr std::size_t   kFile_State       = 0x40;
    constexpr std::uint8_t  kFileLoaded       = 2;
    constexpr std::size_t   kVfxFile_Resource = 0x80;
    constexpr std::size_t   kEffect_Flags     = 0x54;
    constexpr std::uint32_t kEffectAlive      = 4;
    constexpr std::uint64_t kEmptyStringId    = 0xB8A0BF169F98ull;   // StrCode64 of ""
    constexpr std::uint32_t kEmptyStrCode32   = 0xBF169F98;
    constexpr std::uint8_t  kEffectDescFlags  = 0x0B;
    constexpr std::int32_t  kHeavyMgArm       = 1;
    constexpr int           kFlashSlots       = 6;        // flashes still playing out while the next shots fire
    constexpr std::uint64_t kColorVariable    = 0xCFF50B635575ull;   // Color: the barrel heat effect's own variable
    constexpr std::uint32_t kVector4Variable  = 0xAB93FFAB;
    constexpr float         kOverheatedGlow   = 2.5f;     // an overheated barrel keeps its full haze until it has cooled to 0.4
    constexpr std::size_t   kQstSystems       = 0x98;
    constexpr std::size_t   kCasingSystem     = 0x240;    // vt[0] throws one casing
    constexpr std::int32_t  kBeltLink         = 5;        // TppEquip.CASING_BeltLink
    constexpr float         kBeltLinkScale    = 1.2f;
    constexpr float         kEjectSpeed       = 3.7258f;  // 5 m/s over (1 + sin 20 deg), as the cupola gun's code makes it
    constexpr float         kEjectSpread      = 0.34202f; // sin 20 deg
    constexpr std::uint32_t kMostLinksAFrame  = 4;
    // CNP_EJECT of hw01_main0_def on SKL_010_GUN: where it sits and its own y and z axes (its x runs along the barrel)
    constexpr float         kEjectAt[3]       = { -0.1194966f, 0.046f, 0.216f };
    constexpr float         kEjectY[2]        = { -0.932026f, 0.362393f };
    constexpr float         kEjectZ[2]        = { -0.362393f, -0.932026f };

    struct alignas(16) EffectDesc   // what FoxFxEffect_Create reads, as HeadMotion fills it
    {
        float         matrix[16];
        void*         file;
        std::uint32_t reserved0;
        std::uint32_t pad0;
        std::uint64_t name;
        std::uint64_t reserved1;
        std::uint32_t tag;
        std::uint32_t pad1;
        void*         owner;
        std::uint64_t reserved2;
        std::uint32_t fixedId;
        std::uint32_t id;
        std::uint8_t  flags;
        std::uint8_t  pad2[15];
    };
    static_assert(offsetof(EffectDesc, file) == 0x40 && offsetof(EffectDesc, name) == 0x50 && offsetof(EffectDesc, tag) == 0x60
                      && offsetof(EffectDesc, flags) == 0x80,
                  "EffectDesc layout");

    const std::uint8_t kCreateEntry[]       = { 0x48, 0x8B, 0xC4, 0x48, 0x89, 0x48, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x30 };
    const std::uint8_t kSetMatrixEntry[]    = { 0x41, 0x0F, 0x28, 0x00, 0xC7, 0x02, 0x00, 0x00, 0x00, 0x00, 0x48, 0x8B, 0xC2 };
    const std::uint8_t kSendDestroyEntry[]  = { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x8B, 0x41, 0x54 };
    const std::uint8_t kSetVisibleEntry[]   = { 0x45, 0x84, 0xC0, 0x74, 0x15, 0x83, 0x49, 0x54, 0x08 };
    const std::uint8_t kSetKillNowEntry[]   = { 0xC7, 0x02, 0x00, 0x00, 0x00, 0x00, 0x48, 0x8B, 0xC2, 0x45, 0x84, 0xC0 };
    const std::uint8_t kSharedAssignEntry[] = { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xDA };
    const std::uint8_t kBuddyFileEntry[]    = { 0x4C, 0x89, 0x44, 0x24, 0x18, 0x48, 0x83, 0xEC, 0x28, 0x4C, 0x8B, 0x41, 0x10 };
    const std::uint8_t kSetVariableEntry[]  = { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xDA, 0xE8 };

    struct alignas(16) EffectVariable   // what the parts effect controller hands fox::fx::Effect for a Vector4 variable
    {
        std::uint32_t type;
        std::uint16_t size;
        std::uint8_t  pad[10];
        float         value[4];
    };
    static_assert(offsetof(EffectVariable, value) == 0x10 && sizeof(EffectVariable) == 0x20, "EffectVariable layout");

    struct alignas(16) CasingDesc   // what the cupola gun's FireSpecs fills for the casing system
    {
        float        position[4];
        float        rotation[4];
        float        velocity[4];
        float        scale;
        std::int32_t type;
        std::int32_t reserved;
        std::int32_t pad;
    };
    static_assert(offsetof(CasingDesc, scale) == 0x30 && sizeof(CasingDesc) == 0x40, "CasingDesc layout");

    struct Engine
    {
        CreateEffect_t  create = nullptr;
        EffectMatrix_t  setWorldMatrix = nullptr;
        EffectDestroy_t sendDestroy = nullptr;
        EffectFlag_t    setVisible = nullptr;
        EffectFlag_t    setKillNow = nullptr;
        SharedAssign_t  sharedAssign = nullptr;
        BuddyFile_t     buddyFile = nullptr;
        void* const*    controllerSlot = nullptr;
        SetVariable_t   setVariable = nullptr;   // without it the barrel heat shows at full strength
        QuarkTable_t    quarkTable = nullptr;    // without it no belt links
    };

    struct Vfx
    {
        const char*   path;
        const char*   missing;   // the LogWarn when the .vfx is not in buddy_btg_00
        std::uint64_t pathId;
        bool          warned;
    };

    struct Kept   // one effect that stays while its condition holds
    {
        Vfx   vfx;
        void* effect;
    };

    Engine            g_Engine{};
    std::atomic<bool> g_Ready{ false };
    std::mutex        g_Mutex;
    Kept              g_Downwash{ { "/Assets/tpp/effect/vfx_data/mecha/fx_tpp_mchuthdnwsmk01_s5LG.vfx",
                                    "BattleGear: the downwash smoke .vfx is not loaded with buddy_btg_00, the glide has no smoke", 0, false },
                                  nullptr };
    Kept              g_BarrelHeat{ { "/Assets/tpp/effect/vfx_data/mecha/fx_tpp_mchhw1blh00_s3.vfx",
                                      "BattleGear: the barrel heat .vfx is not loaded with buddy_btg_00, the warm heavy MG shows no heat", 0,
                                      false },
                                    nullptr };
    Kept              g_BarrelSmoke{ { "/Assets/tpp/effect/vfx_data/mecha/fx_tpp_mchnadblhsmk00_s1.vfx",
                                       "BattleGear: the barrel smoke .vfx is not loaded with buddy_btg_00, the overheated heavy MG does not smoke",
                                       0, false },
                                     nullptr };
    Kept              g_ChargeGlow{ { "/Assets/tpp/effect/vfx_data/mecha/fx_tpp_mchmgmstncrg01_s3.vfx",
                                      "BattleGear: the charge glow .vfx is not loaded with buddy_btg_00, the rail-gun charges without a glow",
                                      0, false },
                                    nullptr };
    Vfx               g_MgFlash{ "/Assets/tpp/effect/vfx_data/muzzle/fx_tpp_mzfgg01_s0FG.vfx",
                                 "BattleGear: the Gatling muzzle flash .vfx is not loaded with buddy_btg_00, the heavy MG fires without a flash",
                                 0, false };
    void*             g_Flashes[kFlashSlots] = {};
    int               g_NextFlash = 0;
    std::uint32_t     g_SeenMgShots = 0;
    std::atomic<std::uint32_t> g_LinkSeed{ 0x2545F491u };

    void* ResolveChecked(std::uintptr_t address, const std::uint8_t* entry, std::size_t size)
    {
        void* fn = address ? ResolveGameAddress(address) : nullptr;
        return fn && CheckHookEntry(fn, entry, size) != HookEntry::Unknown ? fn : nullptr;
    }

    void PointMatrix(const float* point, float* m)
    {
        std::memset(m, 0, 16 * sizeof(float));
        m[0] = m[5] = m[10] = m[15] = 1.0f;
        m[12] = point[0];
        m[13] = point[1];
        m[14] = point[2];
    }

    bool AimMatrix(const float* point, const float* target, float* m)   // rows x, y, z = forward (the flash points along z)
    {
        float f[3] = { target[0] - point[0], target[1] - point[1], target[2] - point[2] };
        const float length = std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
        if (length < 0.5f)
            return false;
        for (float& v : f)
            v /= length;
        const float up[3] = { 0.0f, std::fabs(f[1]) > 0.99f ? 0.0f : 1.0f, std::fabs(f[1]) > 0.99f ? 1.0f : 0.0f };
        float x[3] = { up[1] * f[2] - up[2] * f[1], up[2] * f[0] - up[0] * f[2], up[0] * f[1] - up[1] * f[0] };
        const float xLength = std::sqrt(x[0] * x[0] + x[1] * x[1] + x[2] * x[2]);
        for (float& v : x)
            v /= xLength;
        const float y[3] = { f[1] * x[2] - f[2] * x[1], f[2] * x[0] - f[0] * x[2], f[0] * x[1] - f[1] * x[0] };
        const float* rows[3] = { x, y, f };
        for (int r = 0; r < 3; ++r)
        {
            m[r * 4] = rows[r][0];
            m[r * 4 + 1] = rows[r][1];
            m[r * 4 + 2] = rows[r][2];
            m[r * 4 + 3] = 0.0f;
        }
        m[12] = point[0];
        m[13] = point[1];
        m[14] = point[2];
        m[15] = 1.0f;
        return true;
    }

    void* EffectFileSEH(std::uint64_t pathId)
    {
        __try
        {
            void* controller = *g_Engine.controllerSlot;
            if (!controller || !*reinterpret_cast<void* const*>(static_cast<const std::uint8_t*>(controller) + kControllerImpl))
                return nullptr;
            auto* file = static_cast<std::uint8_t*>(g_Engine.buddyFile(controller, kMainPackGroup, pathId));
            if (!file || file[kFile_State] != kFileLoaded || !*reinterpret_cast<void* const*>(file + kVfxFile_Resource))
                return nullptr;
            return file;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    void* CreateEffectSEH(const EffectDesc& desc)
    {
        __try
        {
            void* effect = nullptr;
            g_Engine.create(&effect, &desc);
            if (effect)
            {
                std::uint32_t err = 0;
                g_Engine.setVisible(effect, &err, true);
            }
            return effect;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    bool AliveSEH(const void* effect)
    {
        __try
        {
            return (*reinterpret_cast<const std::uint32_t*>(static_cast<const std::uint8_t*>(effect) + kEffect_Flags) & kEffectAlive) != 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool MoveEffectSEH(void* effect, const float* matrix)
    {
        __try
        {
            const std::uint32_t flags = *reinterpret_cast<const std::uint32_t*>(static_cast<const std::uint8_t*>(effect) + kEffect_Flags);
            if ((flags & kEffectAlive) == 0)
                return false;
            std::uint32_t err = 0;
            g_Engine.setWorldMatrix(effect, &err, matrix);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void DestroyEffectSEH(void*& effect)
    {
        __try
        {
            std::uint32_t err = 0;
            g_Engine.setKillNow(effect, &err, true);
            g_Engine.sendDestroy(effect, &err);
            g_Engine.sharedAssign(&effect, nullptr);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            effect = nullptr;
        }
    }

    void ReleaseEffectSEH(void*& effect)
    {
        __try
        {
            g_Engine.sharedAssign(&effect, nullptr);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            effect = nullptr;
        }
    }

    void* Spawn(Vfx& vfx, const float* matrix)   // g_Mutex held
    {
        void* file = EffectFileSEH(vfx.pathId);
        if (!file)
        {
            if (!vfx.warned)
            {
                vfx.warned = true;
                LogWarn("%s", vfx.missing);
            }
            return nullptr;
        }
        EffectDesc desc{};
        std::memcpy(desc.matrix, matrix, sizeof(desc.matrix));
        desc.file = file;
        desc.name = kEmptyStringId;
        desc.tag = kEmptyStrCode32;
        desc.flags = kEffectDescFlags;
        return CreateEffectSEH(desc);
    }

    void Keep(Kept& kept, bool on, const float* point)   // g_Mutex held
    {
        if (!on)
        {
            if (kept.effect)
                DestroyEffectSEH(kept.effect);
            return;
        }
        alignas(16) float matrix[16];
        PointMatrix(point, matrix);
        if (kept.effect)
        {
            if (!MoveEffectSEH(kept.effect, matrix))
                ReleaseEffectSEH(kept.effect);
            return;
        }
        kept.effect = Spawn(kept.vfx, matrix);
    }

    void KeepAt(Kept& kept, bool on, const float* matrix)   // g_Mutex held; the effect rides a bone: matrix = its world matrix
    {
        if (!on)
        {
            if (kept.effect)
                DestroyEffectSEH(kept.effect);
            return;
        }
        if (kept.effect)
        {
            if (!MoveEffectSEH(kept.effect, matrix))
                ReleaseEffectSEH(kept.effect);
            return;
        }
        kept.effect = Spawn(kept.vfx, matrix);
    }

    void SetColorSEH(void* effect, float alpha)
    {
        __try
        {
            if ((*reinterpret_cast<const std::uint32_t*>(static_cast<const std::uint8_t*>(effect) + kEffect_Flags) & kEffectAlive) == 0)
                return;
            EffectVariable colour{};
            colour.type = kVector4Variable;
            colour.size = sizeof(EffectVariable);
            colour.value[0] = colour.value[1] = colour.value[2] = 1.0f;
            colour.value[3] = alpha;
            alignas(16) std::uint8_t result[16] = {};
            g_Engine.setVariable(effect, result, kColorVariable, &colour);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    bool EjectSEH(const CasingDesc& desc)
    {
        __try
        {
            std::uint8_t* table = g_Engine.quarkTable();
            std::uint8_t* systems = table ? *reinterpret_cast<std::uint8_t**>(table + kQstSystems) : nullptr;
            void* casings = systems ? *reinterpret_cast<void**>(systems + kCasingSystem) : nullptr;
            if (!casings)
                return false;
            reinterpret_cast<EjectCasing_t>((*static_cast<void***>(casings))[0])(casings, &desc);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    float NextUnit()   // -1 .. 1
    {
        std::uint32_t x = g_LinkSeed.load(std::memory_order_relaxed);
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 5;
        g_LinkSeed.store(x, std::memory_order_relaxed);
        return static_cast<float>(x >> 8) * (2.0f / 16777216.0f) - 1.0f;
    }

    void FlashForNewShots()   // g_Mutex held
    {
        for (void*& flash : g_Flashes)
            if (flash && !AliveSEH(flash))
                ReleaseEffectSEH(flash);
        const std::uint32_t shots = battleGearWeapons::MgShots();
        if (shots == g_SeenMgShots)
            return;
        g_SeenMgShots = shots;
        float muzzle[3] = {};
        float target[3] = {};
        alignas(16) float matrix[16];
        if (!battleGearWeapons::MuzzleFor(kHeavyMgArm, 0, muzzle) || !battleGearWeapons::AimPoint(target) || !AimMatrix(muzzle, target, matrix))
            return;
        void*& slot = g_Flashes[g_NextFlash];
        g_NextFlash = (g_NextFlash + 1) % kFlashSlots;
        if (slot)
            DestroyEffectSEH(slot);
        slot = Spawn(g_MgFlash, matrix);
    }
}

namespace battleGearEffects
{
    bool Install()
    {
        g_Engine.create = reinterpret_cast<CreateEffect_t>(ResolveChecked(gAddr.FoxFxEffect_Create, kCreateEntry, sizeof(kCreateEntry)));
        g_Engine.setWorldMatrix = reinterpret_cast<EffectMatrix_t>(
            ResolveChecked(gAddr.FoxFxEffect_SetWorldMatrix, kSetMatrixEntry, sizeof(kSetMatrixEntry)));
        g_Engine.sendDestroy = reinterpret_cast<EffectDestroy_t>(
            ResolveChecked(gAddr.FoxFxEffect_SendDestroy, kSendDestroyEntry, sizeof(kSendDestroyEntry)));
        g_Engine.setVisible = reinterpret_cast<EffectFlag_t>(
            ResolveChecked(gAddr.FoxFxEffect_SetVisible, kSetVisibleEntry, sizeof(kSetVisibleEntry)));
        g_Engine.setKillNow = reinterpret_cast<EffectFlag_t>(
            ResolveChecked(gAddr.FoxFxEffect_SetKillNow, kSetKillNowEntry, sizeof(kSetKillNowEntry)));
        g_Engine.sharedAssign = reinterpret_cast<SharedAssign_t>(
            ResolveChecked(gAddr.FoxSharedPtrBase_Set, kSharedAssignEntry, sizeof(kSharedAssignEntry)));
        g_Engine.buddyFile = reinterpret_cast<BuddyFile_t>(
            ResolveChecked(gAddr.Buddy2BlockControllerImpl_GetFileForPathFromBuddyBlock, kBuddyFileEntry, sizeof(kBuddyFileEntry)));
        g_Engine.controllerSlot = static_cast<void* const*>(
            gAddr.Buddy2BlockController_Instance ? ResolveGameAddress(gAddr.Buddy2BlockController_Instance) : nullptr);
        if (!g_Engine.create || !g_Engine.setWorldMatrix || !g_Engine.sendDestroy || !g_Engine.setVisible || !g_Engine.setKillNow
            || !g_Engine.sharedAssign || !g_Engine.buddyFile || !g_Engine.controllerSlot)
        {
            LogWarn("BattleGear: effect addresses missing or changed on %s, the Battle Gear plays no smoke, glow or MG flash",
                    GetGameBuildName(gGameBuild));
            return false;
        }
        g_Engine.setVariable = reinterpret_cast<SetVariable_t>(
            ResolveChecked(gAddr.FoxFxEffect_SetVariable, kSetVariableEntry, sizeof(kSetVariableEntry)));
        if (!g_Engine.setVariable)
            LogWarn("BattleGear: effect variable address missing or changed on %s, the heavy MG's barrel heat shows at full strength",
                    GetGameBuildName(gGameBuild));
        g_Engine.quarkTable = reinterpret_cast<QuarkTable_t>(gAddr.GetQuarkSystemTable ? ResolveGameAddress(gAddr.GetQuarkSystemTable)
                                                                                        : nullptr);
        for (Vfx* vfx : { &g_Downwash.vfx, &g_BarrelHeat.vfx, &g_BarrelSmoke.vfx, &g_ChargeGlow.vfx, &g_MgFlash })
            vfx->pathId = FoxHashes::PathCode64Ext(vfx->path);
        g_SeenMgShots = battleGearWeapons::MgShots();
        g_Ready.store(true, std::memory_order_release);
        return true;
    }

    void Uninstall()
    {
        Drop();
        g_Ready.store(false, std::memory_order_release);
    }

    void OnFieldFrame()
    {
        if (!g_Ready.load(std::memory_order_acquire))
            return;
        float ground[3] = {};
        const bool hovering = battleGearLegs::HoverGround(ground);
        alignas(16) float gun[16];
        const bool gunKnown = battleGearWeapons::RoofGun(gun, nullptr);
        const float heat = battleGearWeapons::MgHeat();
        const bool overheated = battleGearWeapons::MgOverheated();
        float rail[3] = {};
        const bool charging = battleGearWeapons::RailCharging() && battleGearWeapons::RailGunMuzzle(rail);
        std::lock_guard<std::mutex> lock(g_Mutex);
        Keep(g_Downwash, hovering, ground);
        KeepAt(g_BarrelHeat, gunKnown && heat > 0.0f, gun);   // the cupola gun's update: on from the first round until it is cold
        if (g_BarrelHeat.effect && g_Engine.setVariable)
        {
            const float glow = (std::min)(overheated ? kOverheatedGlow * heat : heat, 1.0f);
            SetColorSEH(g_BarrelHeat.effect, glow * glow);
        }
        KeepAt(g_BarrelSmoke, gunKnown && overheated, gun);
        Keep(g_ChargeGlow, charging, rail);
        FlashForNewShots();
    }

    void Drop()
    {
        if (!g_Ready.load(std::memory_order_acquire))
            return;
        std::lock_guard<std::mutex> lock(g_Mutex);
        for (Kept* kept : { &g_Downwash, &g_BarrelHeat, &g_BarrelSmoke, &g_ChargeGlow })
            if (kept->effect)
                DestroyEffectSEH(kept->effect);
        for (void*& flash : g_Flashes)
            if (flash)
                DestroyEffectSEH(flash);
        g_SeenMgShots = battleGearWeapons::MgShots();
    }

    void EjectBeltLinks(std::uint32_t rounds)
    {
        alignas(16) float gun[16];
        float moving[3] = {};
        if (!g_Ready.load(std::memory_order_acquire) || !g_Engine.quarkTable || !battleGearWeapons::RoofGun(gun, moving))
            return;
        const float* across = gun;
        const float* up = gun + 4;
        const float* along = gun + 8;
        std::uint32_t thrown = 0;
        for (int tries = 0; thrown < (std::min)(rounds, kMostLinksAFrame) && tries < 64; ++tries)
        {
            const float spread[3] = { NextUnit(), NextUnit(), NextUnit() };
            const float length = std::sqrt(spread[0] * spread[0] + spread[1] * spread[1] + spread[2] * spread[2]);
            if (length < 0.01f || length > 1.0f)   // only points inside the unit ball, pushed out to its surface
                continue;
            ++thrown;
            const float out[3] = { kEjectSpread * spread[0] / length, kEjectSpread * spread[1] / length,
                                   1.0f + kEjectSpread * spread[2] / length };   // along the point's x, y, z
            CasingDesc link{};
            for (int k = 0; k < 3; ++k)
            {
                const float pointY = kEjectY[0] * across[k] + kEjectY[1] * up[k];
                const float pointZ = kEjectZ[0] * across[k] + kEjectZ[1] * up[k];
                link.position[k] = gun[12 + k] + kEjectAt[0] * across[k] + kEjectAt[1] * up[k] + kEjectAt[2] * along[k];
                link.velocity[k] = (out[0] * along[k] + out[1] * pointY + out[2] * pointZ) * kEjectSpeed + moving[k];
            }
            link.position[3] = 1.0f;
            link.rotation[3] = 1.0f;
            link.scale = kBeltLinkScale;
            link.type = kBeltLink;
            if (!EjectSEH(link))
                return;
        }
    }
}
