#include <pch.h>
#include "AttackActionImpl_UpdateShootingByPad.h"

#include <Windows.h>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "AddressSet.h"
#include "HookUtils.h"
#include "LuaBroadcaster.h"
#include "MissionCodeGuard.h"
#include "log.h"
#include "../equip/EquipAliasLease.h"
#include "../equip/EquipPartParams.h"
#include "../equip/GunBasicInject.h"
#include "../equip/TppEquip_ReloadEquipIdTable.h"

namespace
{
    using UpdateShootingByPad_t = void(__fastcall*)(void* self, std::uint8_t* work, std::uint32_t slot);
    using SetAmmoCount_t = void(__fastcall*)(void* self, std::uint8_t* work, std::uint32_t slot,
                                             std::uint64_t magazine, std::uint32_t total);
    using GetQuarkSystemTable_t = std::uint8_t*(__fastcall*)();

    using PlaySound_t = void(__fastcall*)(void* sounds, std::uint32_t slot, std::uint32_t* handle, std::uint32_t eventId,
                                          const float* position, std::uint32_t follow);
    using StopSound_t = void(__fastcall*)(void* sounds, std::uint32_t slot, std::uint32_t handle);
    using IsPlaying_t = bool(__fastcall*)(void* sounds, std::uint32_t slot, std::uint32_t handle);
    using MoveSound_t = void(__fastcall*)(void* sounds, std::uint32_t slot, std::uint32_t handle, const float* matrix);

    using IsRealized_t = bool(__fastcall*)(void* equipSystem, std::uint32_t equipHandle);
    using GetEffectController_t = void*(__fastcall*)(void* equipSystemImpl, std::uint32_t equipHandle, std::uint32_t* partIndex);
    using ChimeraEffectController_t = void*(__fastcall*)(void* chimeraSystemImpl, std::uint32_t chimeraIndex,
                                                         std::uint32_t partType, std::uint32_t* partIndex);
    using CreateGunEffect_t = void(__fastcall*)(void* controller, std::uint32_t partIndex, std::uint64_t name, bool oneShot,
                                                std::uint32_t variation);
    using StopGunEffect_t = void(__fastcall*)(void* controller, std::uint32_t partIndex, std::uint64_t name, bool killNow);
    using FindPartsEffect_t = std::uint32_t(__fastcall*)(void* parts, std::uint32_t partIndex, std::uint64_t name);
    using EffectWorldMatrix_t = void(__fastcall*)(void* controller, std::uint32_t partIndex, const std::uint8_t* attach,
                                                  std::uint64_t locator, std::uint8_t effectIndex, float* matrix);
    using CreateFx_t = void**(__fastcall*)(void** effect, const void* desc);
    using SetFxFlag_t = std::uint32_t*(__fastcall*)(void* effect, std::uint32_t* error, bool on);
    using SendFxDestroy_t = std::uint32_t*(__fastcall*)(void* effect, std::uint32_t* error);
    using SetSharedPtr_t = void(__fastcall*)(void** pointer, void* value);
    using ConstructGeoControl_t = void(__fastcall*)(void* control, std::uint32_t scene);
    using LineCheck_t = void(__fastcall*)(void* control, std::uint32_t flags, const float* start, const float* end,
                                          float radius, std::uint64_t scene);
    using GetHitPosition_t = float*(__fastcall*)(const void* hit, float* position);

    struct ChargeSpec
    {
        float cooldown = 1.0f;
        equip::ChargeShotStyle style;
        std::vector<equip::ChargeShotLevel> levels;
    };

    enum class Phase : std::uint8_t { Idle, Charging, Cooldown };

    struct SlotState
    {
        Phase phase = Phase::Idle;
        void* attackAction = nullptr;
        std::uint8_t* work = nullptr;
        int equipId = 0;
        int level = 0;
        float elapsed = 0.0f;
        float cooldownLeft = 0.0f;
        bool waitForRelease = false;
        ChargeSpec spec;
        int armedLevel = 0;
        int armedTicks = 0;
        std::uint32_t chargeSound = 0;
        int knownEquipId = -1;
        unsigned knownGeneration = 0;
        bool knownHud = false;
        int tracedHeldEquipId = -1;
        int tracedShotEquipId = -1;
        void* effectSystem = nullptr;
        std::uint32_t effectGun = 0;
        std::uint64_t heldEffect = 0;
        std::uint64_t heldEffectNextTick = 0;
        std::uint32_t heldVariation = equip::kChargeShotNoVariation;
        std::uint32_t heldVariationNextTick = equip::kChargeShotNoVariation;
        bool chargeEffectOn = false;
        bool chargeEffectNextTick = false;
    };

    struct PendingShot // armed by Fire, used by the gun's DoFire, which the equip system runs later
    {
        int ticksLeft = 0;
        int bulletId = 0;
        int runs = 1;
        std::uint64_t impactEffect = 0;
        std::uint32_t impactVariation = equip::kChargeShotNoVariation;
    };

    struct ShotLine // muzzle line from DoFire, used on the next tick for the impact effect
    {
        bool ready = false;
        float start[3] = {};
        float dir[3] = {};
        std::uint64_t impactEffect = 0;
        std::uint32_t impactVariation = equip::kChargeShotNoVariation;
    };

    struct alignas(16) FxEffectDesc // what SimplePartsEffectControllerImpl::CreateEffectImpl hands fox::fx::Effect::Create
    {
        float matrix[16] = {};
        void* file = nullptr;
        std::uint64_t reserved48 = 0;
        std::uint64_t name = 0;
        std::uint64_t reserved58 = 0;
        std::uint32_t variation = 0;
        std::uint32_t reserved64 = 0;
        void* owner = nullptr;
        std::uint64_t reserved70 = 0;
        std::uint32_t hasFixedId = 0;
        std::uint32_t fixedId = 0;
        std::uint8_t flags = 0;
        std::uint8_t reserved81[15] = {};
    };
    static_assert(offsetof(FxEffectDesc, file) == 0x40 && offsetof(FxEffectDesc, name) == 0x50
                  && offsetof(FxEffectDesc, variation) == 0x60 && offsetof(FxEffectDesc, hasFixedId) == 0x78
                  && offsetof(FxEffectDesc, flags) == 0x80, "fox::fx effect description layout");

    struct alignas(16) GeoCheckControl // fox::geo::GeoCheckControlN<1>, its constructor fills two 0x60-byte hit slots
    {
        std::uint8_t bytes[0x130];
    };

    struct ImpactEffect
    {
        void* effect = nullptr;
        float secondsLeft = 0.0f;
    };

    struct GunEffectTarget
    {
        void* controller = nullptr;
        std::uint32_t partIndex = 0;
    };

    struct ImpactCalls
    {
        GetEffectController_t getEffectController = nullptr;
        ChimeraEffectController_t chimeraEffectController = nullptr;
        EffectWorldMatrix_t effectWorldMatrix = nullptr;
        CreateFx_t createEffect = nullptr;
        SetFxFlag_t setVisible = nullptr;
        SetFxFlag_t setKillNow = nullptr;
        SendFxDestroy_t sendDestroy = nullptr;
        SetSharedPtr_t setSharedPtr = nullptr;
        GetHitPosition_t getHitPosition = nullptr;
        ConstructGeoControl_t constructGeoControl = nullptr;
        LineCheck_t lineCheck = nullptr;
    };

    constexpr std::uint32_t kSlotCount = 16;
    constexpr std::uint32_t kFireButton = 0x10;
    constexpr std::uint32_t kShotRequested = 0x1000000;
    constexpr std::uint32_t kInfiniteAmmo = 0x10000000;
    constexpr std::uint32_t kGunAttack = 1;
    constexpr int kPendingTicks = 4;
    constexpr int kHudMax = 1000;

    constexpr std::uint32_t kWalkerChargeSound = 0xA7E07F9E;
    constexpr std::uint32_t kWalkerLevelSounds[3] = { 0xA7E07F9D, 0xA7E07F9C, 0xA7E07F9B };
    constexpr std::uint32_t kWalkerFullSound = 0x539F6BDD;
    constexpr std::uint64_t kChargeFlash = 0x6ED4515E9DBCull; // walker CHARGE_FLASH effect name
    constexpr float kImpactRange = 200.0f; // how far along the aim line impactEffect looks for the hit

    constexpr int kChimeraFirst = 0x367; // customized weapon in hand: 0x367 + 2 * loadout slot + player
    constexpr int kChimeraLast = 0x36C;
    constexpr std::size_t kQuarkGame = 0x98;              // the in-mission loadout, as EquipBgTexture reads it
    constexpr std::size_t kGameVars = 0x10;
    constexpr std::size_t kGameLoadouts = 0x130;
    constexpr std::size_t kVarsSortieSlot = 0x1528;       // vars.currentSortieLoadoutSlot
    constexpr std::size_t kLoadoutsReady = 0xAF8;
    constexpr std::size_t kLoadoutsInMission = 0x3B8;
    constexpr std::size_t kLoadoutSize = 0xE8;
    constexpr std::size_t kLoadoutWeapons = 0x18;         // primary, primary 2, secondary
    constexpr std::size_t kLoadoutWeaponStride = 0x14;

    constexpr std::uint32_t kGeoLineFlags = 0x4020700;  // what the walker checks its stun-gun shot line against
    constexpr std::uint64_t kMainScene = 0xD8FD1368C292ull; // MainScene
    constexpr std::uint64_t kGeoControlFilter = 0x70;
    constexpr std::uint64_t kGeoControlMode = 0x80000004;
    constexpr std::size_t kGeoControlModeAt = 0x18;
    constexpr std::size_t kGeoHitCount = 0x60;
    constexpr std::size_t kGeoNearestHit = 0x64;
    constexpr std::size_t kGeoHits = 0x70;
    constexpr std::size_t kGeoHitSize = 0x60;
    constexpr int kGeoHitSlots = 2;

    constexpr int kImpactsPerSlot = 4;
    constexpr float kImpactSeconds = 1.0f; // the walker removes its impact when the stun-gun state ends, a second after the shot
    constexpr std::size_t kEquipSystemInterface = 0x20; // the equip system Fire uses sits 0x20 into EquipSystemImpl
    constexpr std::size_t kEquipChimeraSystem = 0xF0;   // a customized gun's parts live in the chimera system
    constexpr std::size_t kChimeraInterface = 0x20;
    constexpr std::uint32_t kHandleEquipIdShift = 5;    // a gun handle carries its equipId in bits 5-15
    constexpr std::uint32_t kHandleEquipIdMask = 0x7FF;
    constexpr std::uint32_t kChimeraPartTypes = 12;
    constexpr std::size_t kGunEffectCreateSlot = 0x8;
    constexpr std::size_t kGunEffectStopSlot = 0x10;
    constexpr std::size_t kControllerParts = 0x38;
    constexpr std::size_t kPartsFindEffectSlot = 0x218;
    constexpr std::uint32_t kNoPartsEffect = 0xFF;
    constexpr std::size_t kPartsRowCount = 0x8;       // a row this high means the slot has no effects
    constexpr std::size_t kPartsFirstRow = 0x10;      // per part, a byte: its row in the effect table
    constexpr std::size_t kPartsEffectsPerRow = 0xC;
    constexpr std::size_t kPartsEffectTable = 0x20;   // fox::gk::EffectDescription pointers
    constexpr std::size_t kPartsModels = 0x58;        // per part its model, null when the gun has no such part
    constexpr std::size_t kEffectLocatorCount = 0x98;
    constexpr std::size_t kEffectLocators = 0xA0;
    constexpr std::size_t kEffectBoneCount = 0xA8;
    constexpr std::size_t kEffectBones = 0xB0;
    constexpr std::size_t kEffectHasFlag = 0xF9;
    constexpr std::size_t kEffectFlag = 0xF8;
    constexpr std::size_t kEffectFixedId = 0xFC;
    constexpr std::size_t kEffectVariation = 0x108;
    constexpr std::size_t kEffectFile = 0x118;
    constexpr std::size_t kEffectName = 0x88;
    constexpr std::uint64_t kStringIdMask = 0xFFFFFFFFFFFFull;
    constexpr int kListedEffectNames = 16;
    constexpr std::size_t kStringIdAt = 0x10;          // a fox::String keeps its StrCode64 0x10 into its data
    constexpr std::uint8_t kAttachToBone = 2;
    constexpr std::uint8_t kFxDescFlags = 0x03;
    constexpr std::uint8_t kFxDescHasFlag = 0x04;
    constexpr std::uint8_t kFxDescFlag = 0x02;
    constexpr std::size_t kFxEffectState = 0x54;
    constexpr std::uint32_t kFxEffectAlive = 4;
    constexpr std::uint64_t kEmptyStringId = 0xB8A0BF169F98ull; // ""

    const std::uint8_t kUpdateShootingByPadEntry[] =
    {
        0x48, 0x89, 0x4C, 0x24, 0x08, 0x53, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56,
        0x41, 0x57, 0x48, 0x81, 0xEC, 0xE8, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x41, 0x08, 0x4C, 0x8B,
        0xE1, 0x45, 0x8B, 0xF0, 0x48, 0x8B, 0x88, 0x38, 0x01, 0x00, 0x00,
    };

    const std::uint8_t kSetAmmoCountEntry[] =
    {
        0x40, 0x53, 0x41, 0x55, 0x41, 0x57, 0x48, 0x81, 0xEC, 0x80, 0x00, 0x00, 0x00, 0x48, 0x8B,
        0x05, 0x00, 0x00, 0x00, 0x00, 0x48, 0x33, 0xC4, 0x48, 0x89, 0x44, 0x24, 0x58, 0x48, 0x8B,
        0x41, 0x08, 0x48, 0x8B, 0xDA, 0x45, 0x8B, 0xF8,
    };
    const std::uint8_t kSetAmmoCountMask[] =
    {
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1, 1, 1,
    };

    const std::uint8_t kCreateEffectIfRealizedEntry[] = // EquipSystemImpl::CreateEffectIfRealized, interface slot 0x168
    {
        0x4C, 0x89, 0x44, 0x24, 0x18, 0x89, 0x54, 0x24, 0x10, 0x53, 0x48, 0x83, 0xEC, 0x30, 0x48, 0x8B,
        0x01, 0x48, 0x8B, 0xD9, 0xFF, 0x50, 0x40,
    };
    const std::uint8_t kStopEffectIfRealizedEntry[] = // EquipSystemImpl::StopEffectIfRealized, interface slot 0x170
    {
        0x48, 0x89, 0x5C, 0x24, 0x20, 0x4C, 0x89, 0x44, 0x24, 0x18, 0x89, 0x54, 0x24, 0x10, 0x57, 0x48,
        0x83, 0xEC, 0x20, 0x48, 0x8B, 0x01, 0x41, 0x0F, 0xB6, 0xF9, 0x48, 0x8B, 0xD9, 0xFF, 0x50, 0x40,
    };
    const std::uint8_t kChimeraFlashLightEntry[] = // ChimeraSystemImpl flashlight switch, chimera interface slot 0xD0
    {
        0x40, 0x53, 0x48, 0x83, 0xEC, 0x30, 0x8B, 0xC2, 0x41, 0x0F, 0xB6, 0xD8, 0x41, 0xB8, 0x00, 0x01, 0x00, 0x00,
    };
    const std::uint8_t kGunEffectCreateEntry[] = // SimplePartsEffectControllerImpl::CreateEffect, slot 0x8
    {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x4C, 0x89, 0x44, 0x24, 0x18, 0x57,
        0x48, 0x83, 0xEC, 0x30, 0x41, 0x0F, 0xB6, 0xF1, 0x8B, 0xFA,
    };
    const std::uint8_t kGunEffectStopEntry[] = // SimplePartsEffectControllerImpl::StopEffect, slot 0x10
    {
        0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57,
        0x48, 0x83, 0xEC, 0x20, 0x8B, 0x59, 0x34, 0x41, 0x0F, 0xB6, 0xE9,
    };
    const std::uint8_t kChimeraEffectControllerEntry[] =
    {
        0x41, 0x83, 0xF8, 0x0B, 0x77, 0x7D, 0x4C, 0x8D, 0x15,
    };

    const std::uint8_t kLineCheckEntry[] =
    {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57,
        0x48, 0x83, 0xEC, 0x30, 0x48, 0x8B, 0xE9, 0x48, 0x8B, 0x4C, 0x24, 0x68,
    };
    const std::uint8_t kGetEffectControllerEntry[] =
    {
        0x89, 0x54, 0x24, 0x10, 0x48, 0x83, 0xEC, 0x08, 0x8B, 0xC2, 0x4D, 0x8B, 0xD8, 0x4C, 0x8B, 0xC9,
    };
    const std::uint8_t kEffectWorldMatrixEntry[] =
    {
        0x48, 0x89, 0x5C, 0x24, 0x18, 0x56, 0x57, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x40,
    };
    const std::uint8_t kSetVisibleEntry[] =
    {
        0x45, 0x84, 0xC0, 0x74, 0x15, 0x83, 0x49, 0x54, 0x08, 0x81, 0x49, 0x54,
    };
    const std::uint8_t kSetKillNowEntry[] =
    {
        0xC7, 0x02, 0x00, 0x00, 0x00, 0x00, 0x48, 0x8B, 0xC2, 0x45, 0x84, 0xC0, 0x74, 0x05, 0x83, 0x49, 0x54, 0x01,
    };
    const std::uint8_t kSendDestroyEntry[] =
    {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x8B, 0x41, 0x54, 0x48, 0x8B, 0xDA,
    };
    const std::uint8_t kSetSharedPtrEntry[] =
    {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xDA, 0x48, 0x8B, 0xF9,
    };
    const std::uint8_t kGetHitPositionEntry[] =
    {
        0x48, 0x83, 0xEC, 0x48, 0xF3, 0x0F, 0x10, 0x41, 0x08, 0xF3, 0x0F, 0x10, 0x51, 0x04, 0x48, 0x8B, 0x41, 0x58,
    };
    const std::uint8_t kConstructGeoControlEntry[] =
    {
        0x48, 0x89, 0x4C, 0x24, 0x08, 0x48, 0x83, 0xEC, 0x18, 0x48, 0xC7, 0x04, 0x24, 0xFE, 0xFF, 0xFF, 0xFF,
    };

    std::mutex g_Mutex;
    std::unordered_map<int, ChargeSpec> g_ByEquipId;
    std::unordered_map<int, ChargeSpec> g_ByReceiverId;
    std::atomic<unsigned> g_Generation{ 1 };
    SlotState g_Slots[kSlotCount];

    std::mutex g_ShotMutex;
    PendingShot g_Pending[kSlotCount];
    ShotLine g_ShotLines[kSlotCount];

    ImpactEffect g_Impacts[kSlotCount][kImpactsPerSlot];

    std::atomic<int> g_HudEquipId{ 0 };
    std::atomic<int> g_HudValue{ 0 };
    std::atomic<int> g_PanelEquipId{ -1 }; // the weapon panel's own lookup: it switches weapon before the gun is raised
    std::atomic<unsigned> g_PanelGeneration{ 0 };
    std::atomic<int> g_PanelLevels{ 0 };

    UpdateShootingByPad_t g_Orig = nullptr;
    void* g_HookTarget = nullptr;
    SetAmmoCount_t g_SetAmmoCount = nullptr;
    GetQuarkSystemTable_t g_GetQuarkSystemTable = nullptr;
    int g_EffectSlotState = 0; // 0 unchecked, 1 matches, -1 does not
    int g_ChimeraState = 0;    // same
    int g_GunEffectCallState = 0;
    ImpactCalls g_Impact{};
    bool g_ImpactReady = false;

    template <typename Fn>
    Fn Slot(void* object, std::size_t offset)
    {
        return reinterpret_cast<Fn>((*reinterpret_cast<void***>(object))[offset / sizeof(void*)]);
    }

    template <typename Fn, std::size_t N>
    Fn ResolveCall(std::uintptr_t address, const std::uint8_t (&entry)[N])
    {
        void* code = address ? ResolveGameAddress(address) : nullptr;
        if (!code || CheckHookEntry(code, entry, N) == HookEntry::Unknown)
            return nullptr;
        return reinterpret_cast<Fn>(code);
    }

    struct WorkView
    {
        std::uint32_t attackBits = 0;
        std::uint32_t flags = 0;
        int engineEquipId = 0;
        int shotCount = 0;
        int magazine = 0;
        std::uint32_t heldButtons = 0;
        float dt = 0.0f;
    };

    bool ReadWorkSEH(void* self, std::uint8_t* work, std::uint32_t slot, WorkView* out)
    {
        __try
        {
            out->attackBits    = *reinterpret_cast<std::uint32_t*>(work + 0x27C);
            out->flags         = *reinterpret_cast<std::uint32_t*>(work + 0x3C0);
            out->engineEquipId = *reinterpret_cast<std::int32_t*>(work + 0x24C);
            out->shotCount     = work[0x3DF];
            out->magazine      = *reinterpret_cast<std::uint16_t*>(work + 0x25C);

            std::uint8_t* player = *reinterpret_cast<std::uint8_t**>(static_cast<std::uint8_t*>(self) + 0x8);
            std::uint8_t* controllers = *reinterpret_cast<std::uint8_t**>(player + 0x138);
            void* pad = *reinterpret_cast<void**>(controllers + 0xB0); // player PadImpl
            using Buttons_t = std::uint32_t(__fastcall*)(void*);
            out->heldButtons = (*reinterpret_cast<Buttons_t**>(pad))[0x30 / 8](pad); // buttons held right now

            void* clock = *reinterpret_cast<void**>(controllers + 0x60);
            using Clock_t = std::uint8_t*(__fastcall*)(void*);
            std::uint8_t* time = (*reinterpret_cast<Clock_t**>(clock))[0x8 / 8](clock);
            const float* perSlot = *reinterpret_cast<float**>(time + 0x38);
            out->dt = perSlot[slot] * *reinterpret_cast<float*>(time + 0x48); // this player's frame time, slow-mo included
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void HoldShotSEH(std::uint8_t* work)
    {
        __try
        {
            work[0x3DF] = 0;
            *reinterpret_cast<std::uint32_t*>(work + 0x3C0) &= ~kShotRequested;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    void RequestShotSEH(std::uint8_t* work)
    {
        __try
        {
            work[0x3DF] = 1; // the equip system keeps one fire request per gun and update, extra shots are rerun in DoFire
            *reinterpret_cast<std::uint32_t*>(work + 0x3C0) |= kShotRequested;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    bool ReadAmmoSEH(std::uint8_t* work, int* magazine, int* total, bool* infinite)
    {
        __try
        {
            *magazine = *reinterpret_cast<std::uint16_t*>(work + 0x25C);
            *total    = *reinterpret_cast<std::uint16_t*>(work + 0x260); // every round the gun has, the magazine included
            *infinite = (*reinterpret_cast<std::uint32_t*>(work + 0x3C0) & kInfiniteAmmo) != 0;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool SetAmmoCountSEH(void* self, std::uint8_t* work, std::uint32_t slot, int magazine, int total)
    {
        __try
        {
            g_SetAmmoCount(self, work, slot, static_cast<std::uint64_t>(magazine), static_cast<std::uint32_t>(total));
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool SwapRecordSEH(std::uint8_t* rec, bool secondary, std::uint8_t bulletId, int bulletType,
                       equip::ChargeShotDoFire* out)
    {
        __try
        {
            std::uint8_t* idCell = rec + (secondary ? 0x7D : 0x7C);
            std::uint8_t* typeCell = rec + (secondary ? 0x7F : 0x7E);
            out->bulletId = *idCell;
            out->bulletType = *typeCell;
            *idCell = bulletId;
            if (bulletType >= 0)
                *typeCell = static_cast<std::uint8_t>(bulletType);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void RestoreRecordSEH(std::uint8_t* rec, const equip::ChargeShotDoFire& swap)
    {
        __try
        {
            rec[swap.secondary ? 0x7D : 0x7C] = swap.bulletId;
            rec[swap.secondary ? 0x7F : 0x7E] = swap.bulletType;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    bool ReadShotSEH(void* request, int* owner, float start[3], float dir[3])
    {
        __try
        {
            const float* r = static_cast<const float*>(request);
            *owner = *reinterpret_cast<const std::uint16_t*>(static_cast<const std::uint8_t*>(request) + 0x2A);
            start[0] = r[0]; start[1] = r[1]; start[2] = r[2];
            dir[0] = r[4]; dir[1] = r[5]; dir[2] = r[6];
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void* SoundsOf(void* attackAction)
    {
        std::uint8_t* player = *reinterpret_cast<std::uint8_t**>(static_cast<std::uint8_t*>(attackAction) + 0x8);
        return *reinterpret_cast<void**>(player + 0x38);
    }

    void PlaySoundSEH(void* attackAction, std::uint8_t* work, std::uint32_t slot, std::uint32_t eventId,
                      std::uint32_t* handle)
    {
        if (eventId == 0)
            return;
        __try
        {
            alignas(16) float position[4] = {};
            const float* held = reinterpret_cast<const float*>(work + 0xF0); // held equip position, row 3 of the matrix at +0xC0
            position[0] = held[0]; position[1] = held[1]; position[2] = held[2]; position[3] = held[3];
            void* sounds = SoundsOf(attackAction);
            std::uint32_t oneShot = 0;
            Slot<PlaySound_t>(sounds, 0x70)(sounds, slot, handle ? handle : &oneShot, eventId, position, 1);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    void MoveSoundSEH(void* attackAction, std::uint8_t* work, std::uint32_t slot, std::uint32_t handle)
    {
        __try
        {
            void* sounds = SoundsOf(attackAction);
            if (Slot<IsPlaying_t>(sounds, 0x90)(sounds, slot, handle))
                Slot<MoveSound_t>(sounds, 0xA0)(sounds, slot, handle, reinterpret_cast<const float*>(work + 0xC0));
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    void StopSoundSEH(void* attackAction, std::uint32_t slot, std::uint32_t handle)
    {
        __try
        {
            void* sounds = SoundsOf(attackAction);
            if (Slot<IsPlaying_t>(sounds, 0x90)(sounds, slot, handle))
                Slot<StopSound_t>(sounds, 0x88)(sounds, slot, handle);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    void StopChargeSound(SlotState& s, std::uint32_t slot)
    {
        if (s.chargeSound != 0 && s.attackAction)
            StopSoundSEH(s.attackAction, slot, s.chargeSound);
        s.chargeSound = 0;
    }

    bool EffectSlotsMatchSEH(void* equipSystem)
    {
        void* create = nullptr;
        void* stop = nullptr;
        __try
        {
            void** slots = *reinterpret_cast<void***>(equipSystem);
            create = slots[0x168 / sizeof(void*)];
            stop = slots[0x170 / sizeof(void*)];
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
        return CheckHookEntry(create, kCreateEffectIfRealizedEntry, sizeof(kCreateEffectIfRealizedEntry)) != HookEntry::Unknown
            && CheckHookEntry(stop, kStopEffectIfRealizedEntry, sizeof(kStopEffectIfRealizedEntry)) != HookEntry::Unknown;
    }

    bool ReadEquipSystemSEH(void* attackAction, std::uint8_t* work, void** equipSystem, std::uint32_t* equipHandle)
    {
        __try
        {
            *equipSystem = *reinterpret_cast<void**>(static_cast<std::uint8_t*>(attackAction) + 0x60);
            *equipHandle = *reinterpret_cast<std::uint32_t*>(work + 0x240); // the gun Fire hands to the equip system
            return *equipSystem != nullptr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool EquipEffectsUsable(void* equipSystem)
    {
        if (g_EffectSlotState == 0)
        {
            g_EffectSlotState = EffectSlotsMatchSEH(equipSystem) ? 1 : -1;
            if (g_EffectSlotState < 0)
                LogError("ChargeShot: equip system effect call does not match %s, charge effects off",
                         GetGameBuildName(gGameBuild));
        }
        return g_EffectSlotState > 0;
    }

    void* VirtualSlotSEH(void* object, std::size_t offset)
    {
        __try
        {
            return (*reinterpret_cast<void***>(object))[offset / sizeof(void*)];
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    void* ChimeraInterfaceSEH(void* equipSystem)
    {
        __try
        {
            return *reinterpret_cast<void**>(static_cast<std::uint8_t*>(equipSystem) + kEquipChimeraSystem);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    bool ChimeraUsable(void* equipSystem)
    {
        if (g_ChimeraState == 0)
        {
            void* chimera = ChimeraInterfaceSEH(equipSystem);
            const bool matches = chimera && g_Impact.chimeraEffectController
                && CheckHookEntry(VirtualSlotSEH(chimera, 0xD0), kChimeraFlashLightEntry, sizeof(kChimeraFlashLightEntry))
                       != HookEntry::Unknown;
            g_ChimeraState = matches ? 1 : -1;
            if (!matches)
                LogError("ChargeShot: the customized-gun parts system does not match %s, customized guns show no charge effects",
                         GetGameBuildName(gGameBuild));
        }
        return g_ChimeraState > 0;
    }

    bool GunEffectCallsUsable(void* controller)
    {
        if (g_GunEffectCallState == 0)
        {
            const bool matches =
                CheckHookEntry(VirtualSlotSEH(controller, kGunEffectCreateSlot), kGunEffectCreateEntry, sizeof(kGunEffectCreateEntry))
                    != HookEntry::Unknown
                && CheckHookEntry(VirtualSlotSEH(controller, kGunEffectStopSlot), kGunEffectStopEntry, sizeof(kGunEffectStopEntry))
                    != HookEntry::Unknown;
            g_GunEffectCallState = matches ? 1 : -1;
            if (!matches)
                LogError("ChargeShot: the gun effect calls do not match %s, charge effects off", GetGameBuildName(gGameBuild));
        }
        return g_GunEffectCallState > 0;
    }

    void CreateGunEffectSEH(const GunEffectTarget& target, std::uint64_t name, std::uint32_t variation)
    {
        __try
        {
            Slot<CreateGunEffect_t>(target.controller, kGunEffectCreateSlot)(target.controller, target.partIndex, name, false,
                                                                             variation);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    void StopGunEffectSEH(const GunEffectTarget& target, std::uint64_t name, bool killNow)
    {
        __try
        {
            Slot<StopGunEffect_t>(target.controller, kGunEffectStopSlot)(target.controller, target.partIndex, name, killNow);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    bool FindImpactPointSEH(const ShotLine& shot, float* point)
    {
        alignas(16) float start[4] = { shot.start[0], shot.start[1], shot.start[2], 0.0f };
        alignas(16) float end[4] = { shot.start[0] + shot.dir[0] * kImpactRange, shot.start[1] + shot.dir[1] * kImpactRange,
                                     shot.start[2] + shot.dir[2] * kImpactRange, 0.0f };
        alignas(16) float hit[4] = {};
        GeoCheckControl control;
        __try
        {
            g_Impact.constructGeoControl(&control, 0);
            *reinterpret_cast<std::uint64_t*>(control.bytes) = kGeoControlFilter;
            *reinterpret_cast<std::uint64_t*>(control.bytes + kGeoControlModeAt) = kGeoControlMode;
            g_Impact.lineCheck(&control, kGeoLineFlags, start, end, 0.0f, kMainScene);
            const int hits = *reinterpret_cast<std::int32_t*>(control.bytes + kGeoHitCount);
            const int nearest = *reinterpret_cast<std::int32_t*>(control.bytes + kGeoNearestHit);
            if (hits <= 0 || nearest < 0 || nearest >= kGeoHitSlots)
                return false;
            g_Impact.getHitPosition(control.bytes + kGeoHits + nearest * kGeoHitSize, hit);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
        point[0] = hit[0];
        point[1] = hit[1];
        point[2] = hit[2];
        return true;
    }

    std::uint32_t ChimeraIndexOf(int equipId)
    {
        if (equipId < kChimeraFirst + 2)
            return static_cast<std::uint32_t>(equipId - kChimeraFirst) * 3;
        if (equipId < kChimeraFirst + 4)
            return static_cast<std::uint32_t>(equipId - kChimeraFirst - 2) * 3 + 1;
        return static_cast<std::uint32_t>(equipId - kChimeraFirst - 4) * 3 + 2;
    }

    int GunEffectControllersSEH(void* equipSystem, std::uint32_t equipHandle, int customizedEquipId, GunEffectTarget* parts)
    {
        __try
        {
            if (!Slot<IsRealized_t>(equipSystem, 0x40)(equipSystem, equipHandle))
                return 0;
            if (customizedEquipId == 0)
            {
                parts[0].controller = g_Impact.getEffectController(static_cast<std::uint8_t*>(equipSystem) - kEquipSystemInterface,
                                                                   equipHandle, &parts[0].partIndex);
                return parts[0].controller ? 1 : 0;
            }
            auto* chimera = *reinterpret_cast<std::uint8_t**>(static_cast<std::uint8_t*>(equipSystem) + kEquipChimeraSystem)
                - kChimeraInterface;
            const std::uint32_t chimeraIndex = ChimeraIndexOf(customizedEquipId);
            int count = 0;
            for (std::uint32_t partType = 0; partType < kChimeraPartTypes; ++partType)
            {
                parts[count].controller = g_Impact.chimeraEffectController(chimera, chimeraIndex, partType, &parts[count].partIndex);
                if (parts[count].controller)
                    ++count;
            }
            return count;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    int GunEffectControllers(void* equipSystem, std::uint32_t equipHandle, GunEffectTarget* parts)
    {
        const int equipId = static_cast<int>((equipHandle >> kHandleEquipIdShift) & kHandleEquipIdMask);
        const bool customized = equipId >= kChimeraFirst && equipId <= kChimeraLast;
        if (customized ? !ChimeraUsable(equipSystem) : !g_Impact.getEffectController)
            return 0;
        return GunEffectControllersSEH(equipSystem, equipHandle, customized ? equipId : 0, parts);
    }

    std::uint64_t StringIdOf(const std::uint8_t* string)
    {
        return *reinterpret_cast<const std::uint64_t*>(*reinterpret_cast<std::uint8_t* const*>(string) + kStringIdAt);
    }

    bool PartHasModel(const std::uint8_t* parts, std::uint32_t partIndex)
    {
        return (*reinterpret_cast<void* const* const*>(parts + kPartsModels))[partIndex] != nullptr;
    }

    std::uint32_t FindPartsEffectSEH(void* controller, std::uint32_t partIndex, std::uint64_t name)
    {
        __try
        {
            auto* parts = *reinterpret_cast<std::uint8_t**>(static_cast<std::uint8_t*>(controller) + kControllerParts);
            if (!PartHasModel(parts, partIndex))
                return kNoPartsEffect;
            return Slot<FindPartsEffect_t>(parts, kPartsFindEffectSlot)(parts, partIndex, name);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return kNoPartsEffect;
        }
    }

    struct GunEffectNames
    {
        bool hasModel = false;
        std::uint32_t row = 0;
        std::uint32_t rowCount = 0;
        std::uint32_t perRow = 0;
        int count = 0;
        std::uint64_t names[kListedEffectNames] = {};
    };

    bool ReadGunEffectNamesSEH(void* controller, std::uint32_t partIndex, GunEffectNames* out)
    {
        __try
        {
            auto* parts = *reinterpret_cast<std::uint8_t**>(static_cast<std::uint8_t*>(controller) + kControllerParts);
            out->hasModel = PartHasModel(parts, partIndex);
            if (!out->hasModel)
                return true;
            out->rowCount = *reinterpret_cast<std::uint32_t*>(parts + kPartsRowCount);
            out->row = (*reinterpret_cast<std::uint8_t**>(parts + kPartsFirstRow))[partIndex];
            if (out->row >= out->rowCount)
                return true;
            out->perRow = *reinterpret_cast<std::uint32_t*>(parts + kPartsEffectsPerRow);
            std::uint8_t** entries = *reinterpret_cast<std::uint8_t***>(parts + kPartsEffectTable) + out->row * out->perRow;
            for (std::uint32_t i = 0; i < out->perRow && out->count < kListedEffectNames; ++i)
            {
                if (entries[i])
                    out->names[out->count++] = StringIdOf(entries[i] + kEffectName) & kStringIdMask;
            }
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool FirstWarning(const char* about, std::uint64_t name)
    {
        struct Warned
        {
            const char* about;
            std::uint64_t name;
        };
        static std::vector<Warned> warned;
        for (const Warned& seen : warned)
        {
            if (seen.about == about && seen.name == name)
                return false;
        }
        warned.push_back({ about, name });
        return true;
    }

    bool FindInGunParts(const GunEffectTarget* parts, int count, std::uint64_t name, GunEffectTarget* found)
    {
        for (int part = 0; part < count; ++part)
        {
            if (FindPartsEffectSEH(parts[part].controller, parts[part].partIndex, name) != kNoPartsEffect)
            {
                *found = parts[part];
                return GunEffectCallsUsable(found->controller);
            }
        }
        return false;
    }

    bool FindGunEffect(void* equipSystem, std::uint32_t equipHandle, std::uint64_t name, GunEffectTarget* found)
    {
        GunEffectTarget parts[kChimeraPartTypes];
        const int count = GunEffectControllers(equipSystem, equipHandle, parts);
        return FindInGunParts(parts, count, name, found);
    }

    bool FindGunEffectOrWarn(void* equipSystem, std::uint32_t equipHandle, std::uint64_t name, const char* field,
                             GunEffectTarget* found)
    {
        GunEffectTarget parts[kChimeraPartTypes];
        const int count = GunEffectControllers(equipSystem, equipHandle, parts);
        if (FindInGunParts(parts, count, name, found))
            return true;
        if (count == 0)
            return false;
        if (FirstWarning(field, name) && name != kChargeFlash)
        {
            LogWarn("ChargeShot: %s 0x%012llX is not in the gun's parts, it shows nothing", field,
                    static_cast<unsigned long long>(name));
            for (int part = 0; part < count; ++part)
            {
                GunEffectNames names;
                if (ReadGunEffectNamesSEH(parts[part].controller, parts[part].partIndex, &names) && names.hasModel
                    && names.perRow != 0 && names.count == static_cast<int>(names.perRow))
                {
                    LogWarn("ChargeShot: the gun loads only the first %u effects of each .parts and they are taken, "
                            "list the effect earlier in its .parts", names.perRow);
                    break;
                }
            }
        }
        return false;
    }

    bool DescribeImpactSEH(void* controller, std::uint32_t partIndex, std::uint64_t name, const float* point,
                           FxEffectDesc* desc)
    {
        __try
        {
            auto* parts = *reinterpret_cast<std::uint8_t**>(static_cast<std::uint8_t*>(controller) + kControllerParts);
            if (!PartHasModel(parts, partIndex))
                return false;
            const std::uint32_t effectIndex = Slot<FindPartsEffect_t>(parts, kPartsFindEffectSlot)(parts, partIndex, name);
            if (effectIndex == kNoPartsEffect)
                return false;
            const std::uint8_t row = (*reinterpret_cast<std::uint8_t**>(parts + kPartsFirstRow))[partIndex];
            const std::uint32_t perRow = *reinterpret_cast<std::uint32_t*>(parts + kPartsEffectsPerRow);
            std::uint8_t* description = (*reinterpret_cast<std::uint8_t***>(parts + kPartsEffectTable))[row * perRow + effectIndex];
            desc->file = *reinterpret_cast<void**>(description + kEffectFile);
            if (!desc->file)
                return false;

            desc->matrix[0] = 1.0f;
            desc->matrix[5] = 1.0f;
            desc->matrix[10] = 1.0f;
            std::uint8_t attach = 0;
            std::uint64_t locator = 0;
            if (*reinterpret_cast<std::uint32_t*>(description + kEffectBoneCount) != 0)
            {
                attach = kAttachToBone;
                locator = StringIdOf(*reinterpret_cast<std::uint8_t**>(description + kEffectBones));
            }
            else if (*reinterpret_cast<std::uint32_t*>(description + kEffectLocatorCount) != 0)
            {
                locator = StringIdOf(*reinterpret_cast<std::uint8_t**>(description + kEffectLocators));
            }
            if (locator != 0)
                g_Impact.effectWorldMatrix(controller, partIndex, &attach, locator, static_cast<std::uint8_t>(effectIndex),
                                           desc->matrix);
            desc->matrix[12] = point[0];
            desc->matrix[13] = point[1];
            desc->matrix[14] = point[2];
            desc->matrix[15] = 1.0f;

            desc->name = kEmptyStringId;
            desc->variation = static_cast<std::uint32_t>(StringIdOf(description + kEffectVariation));
            const std::uint32_t fixedId = *reinterpret_cast<std::uint32_t*>(description + kEffectFixedId);
            if (fixedId != 0)
            {
                desc->hasFixedId = 1;
                desc->fixedId = fixedId;
            }
            desc->flags = kFxDescFlags;
            if (description[kEffectHasFlag] != 0)
            {
                desc->flags |= kFxDescHasFlag;
                if (description[kEffectFlag] != 0)
                    desc->flags |= kFxDescFlag;
                else
                    desc->flags &= ~kFxDescFlag;
            }
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void* CreateImpactEffectSEH(const FxEffectDesc& desc)
    {
        __try
        {
            void* effect = nullptr;
            g_Impact.createEffect(&effect, &desc);
            if (effect)
            {
                std::uint32_t error = 0;
                g_Impact.setVisible(effect, &error, true);
            }
            return effect;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    bool IsImpactAliveSEH(void* effect)
    {
        __try
        {
            return (*reinterpret_cast<std::uint32_t*>(static_cast<std::uint8_t*>(effect) + kFxEffectState) & kFxEffectAlive) != 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void DestroyImpactSEH(void* effect, bool stillAlive)
    {
        __try
        {
            if (stillAlive)
            {
                std::uint32_t error = 0;
                g_Impact.setKillNow(effect, &error, true);
                g_Impact.sendDestroy(effect, &error);
            }
            g_Impact.setSharedPtr(&effect, nullptr);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
    }

    void RemoveImpact(ImpactEffect& impact)
    {
        if (impact.effect && g_ImpactReady)
            DestroyImpactSEH(impact.effect, IsImpactAliveSEH(impact.effect));
        impact = ImpactEffect{};
    }

    void UpdateImpacts(std::uint32_t slot, float dt)
    {
        for (ImpactEffect& impact : g_Impacts[slot])
        {
            if (!impact.effect)
                continue;
            impact.secondsLeft -= dt;
            if (impact.secondsLeft <= 0.0f || !IsImpactAliveSEH(impact.effect))
                RemoveImpact(impact);
        }
    }

    ImpactEffect& FreeImpact(std::uint32_t slot)
    {
        ImpactEffect* oldest = &g_Impacts[slot][0];
        for (ImpactEffect& impact : g_Impacts[slot])
        {
            if (!impact.effect)
                return impact;
            if (impact.secondsLeft < oldest->secondsLeft)
                oldest = &impact;
        }
        RemoveImpact(*oldest);
        return *oldest;
    }

    void PlayImpactEffect(std::uint32_t slot, const ShotLine& shot)
    {
        const SlotState& s = g_Slots[slot];
        void* equipSystem = nullptr;
        std::uint32_t equipHandle = 0;
        if (!g_ImpactReady || !s.attackAction || !s.work
            || !ReadEquipSystemSEH(s.attackAction, s.work, &equipSystem, &equipHandle) || !EquipEffectsUsable(equipSystem))
            return;
        float point[3] = {};
        if (!FindImpactPointSEH(shot, point))
        {
            return;
        }
        GunEffectTarget target;
        if (!FindGunEffectOrWarn(equipSystem, equipHandle, shot.impactEffect, "impactEffect", &target))
            return;
        FxEffectDesc desc;
        if (!DescribeImpactSEH(target.controller, target.partIndex, shot.impactEffect, point, &desc))
        {
            if (FirstWarning("impactEffect file", shot.impactEffect))
                LogWarn("ChargeShot: impactEffect 0x%012llX has no loaded .vfx in the gun's parts, nothing shown at the hit",
                        static_cast<unsigned long long>(shot.impactEffect));
            return;
        }
        if (shot.impactVariation != equip::kChargeShotNoVariation)
            desc.variation = shot.impactVariation; // as CreateEffectImpl: a given variation beats the parts entry's
        ImpactEffect& impact = FreeImpact(slot);
        impact.effect = CreateImpactEffectSEH(desc);
        impact.secondsLeft = kImpactSeconds;
    }

    void HoldGunForEffects(SlotState& s, std::uint8_t* work)
    {
        s.effectSystem = nullptr;
        s.effectGun = 0;
        void* equipSystem = nullptr;
        std::uint32_t equipHandle = 0;
        if (!ReadEquipSystemSEH(s.attackAction, work, &equipSystem, &equipHandle) || !EquipEffectsUsable(equipSystem))
            return;
        s.effectSystem = equipSystem;
        s.effectGun = equipHandle;
    }

    bool PlayGunEffect(const SlotState& s, std::uint64_t name, std::uint32_t variation, const char* field)
    {
        GunEffectTarget target;
        if (name == 0 || !s.effectSystem || !FindGunEffectOrWarn(s.effectSystem, s.effectGun, name, field, &target))
            return false;
        CreateGunEffectSEH(target, name, variation);
        return true;
    }

    void StopGunEffect(const SlotState& s, std::uint64_t name, bool killNow)
    {
        GunEffectTarget target;
        if (name != 0 && s.effectSystem && FindGunEffect(s.effectSystem, s.effectGun, name, &target))
            StopGunEffectSEH(target, name, killNow);
    }

    void KillGunEffect(const SlotState& s, std::uint64_t name)
    {
        StopGunEffect(s, name, true);
    }

    void StartChargeEffect(SlotState& s)
    {
        KillGunEffect(s, s.spec.style.chargeEffect); // a copy still fading from the last charge would block the new one
        s.chargeEffectNextTick = s.spec.style.chargeEffect != 0 && s.effectSystem;
    }

    void HoldLevelEffect(SlotState& s, std::uint64_t name, std::uint32_t variation)
    {
        if ((name == s.heldEffect && variation == s.heldVariation)
            || (name == s.heldEffectNextTick && variation == s.heldVariationNextTick))
            return;
        KillGunEffect(s, s.heldEffect); // the walker swaps its charge glow at once, also for a new variation of it
        s.heldEffect = 0;
        s.heldEffectNextTick = 0;
        const bool chargeEffectShowsIt = name == s.spec.style.chargeEffect && (s.chargeEffectOn || s.chargeEffectNextTick);
        if (name == 0 || chargeEffectShowsIt || !s.effectSystem)
            return;
        KillGunEffect(s, name);
        s.heldEffectNextTick = name;
        s.heldVariationNextTick = variation;
    }

    void StartQueuedEffects(SlotState& s)
    {
        if (s.chargeEffectNextTick)
        {
            s.chargeEffectNextTick = false;
            s.chargeEffectOn = PlayGunEffect(s, s.spec.style.chargeEffect, equip::kChargeShotNoVariation, "chargeEffect");
        }
        if (s.heldEffectNextTick != 0)
        {
            const bool started = PlayGunEffect(s, s.heldEffectNextTick, s.heldVariationNextTick, "effect");
            s.heldEffect = started ? s.heldEffectNextTick : 0;
            s.heldVariation = s.heldVariationNextTick;
            s.heldEffectNextTick = 0;
        }
    }

    void StopChargeEffects(SlotState& s, bool killNow)
    {
        StopGunEffect(s, s.heldEffect, killNow);
        if (s.chargeEffectOn)
            StopGunEffect(s, s.spec.style.chargeEffect, killNow);
        s.heldEffect = 0;
        s.heldEffectNextTick = 0;
        s.chargeEffectOn = false;
        s.chargeEffectNextTick = false;
    }

    void ServiceShots(std::uint32_t slot)
    {
        ShotLine shot;
        {
            std::lock_guard<std::mutex> lock(g_ShotMutex);
            if (g_Pending[slot].ticksLeft > 0)
                --g_Pending[slot].ticksLeft;
            shot = g_ShotLines[slot];
            g_ShotLines[slot].ready = false;
        }
        if (shot.ready && shot.impactEffect != 0)
            PlayImpactEffect(slot, shot);
    }

    bool IsWeaponReady(std::uint32_t flags)
    {
        const bool raised = ((flags >> 18) & 3) != 0; // aiming or holding the gun up
        const bool blocked = (flags & (1u << 26)) != 0 || (flags & 0x4) != 0;
        return raised && !blocked;
    }

    int ReceiverOf(int equipId)
    {
        const int custom = EquipParam_GetReceiverForEquipId(equipId);
        if (custom > 0)
            return custom;
        const int weaponId = TppEquip_GetSubIdForEquipId(equipId);
        unsigned char row[12] = {};
        if (weaponId > 0 && GunBasic_ReadRowBytes(weaponId, row))
            return row[0];
        return 0;
    }

    int LoadoutWeaponSEH(int chimeraIndex)
    {
        __try
        {
            std::uint8_t* game = *reinterpret_cast<std::uint8_t**>(g_GetQuarkSystemTable() + kQuarkGame);
            if (!game)
                return 0;
            std::uint8_t* vars = *reinterpret_cast<std::uint8_t**>(game + kGameVars);
            std::uint8_t* loadouts = *reinterpret_cast<std::uint8_t**>(game + kGameLoadouts);
            if (!vars || !loadouts || loadouts[kLoadoutsReady] == 0)
                return 0;
            const std::uint8_t sortie = vars[kVarsSortieSlot];
            if (sortie > 3)
                return 0;
            return *reinterpret_cast<std::int32_t*>(loadouts + kLoadoutsInMission + sortie * kLoadoutSize
                                                    + kLoadoutWeapons + (chimeraIndex >> 1) * kLoadoutWeaponStride);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    int RealEquipId(int engineEquipId)
    {
        const int equipId = equip::FromEngine(engineEquipId);
        const int index = equipId - kChimeraFirst;
        if (equipId < kChimeraFirst || equipId > kChimeraLast || (index & 1) || !g_GetQuarkSystemTable)
            return equipId;
        const int real = LoadoutWeaponSEH(index);
        return real > 0 ? equip::FromEngine(real) : equipId;
    }

    bool FindSpec(int equipId, ChargeSpec* out)
    {
        {
            std::lock_guard<std::mutex> lock(g_Mutex);
            if (g_ByEquipId.empty() && g_ByReceiverId.empty())
                return false;
            const auto it = g_ByEquipId.find(equipId);
            if (it != g_ByEquipId.end())
            {
                *out = it->second;
                return true;
            }
            if (g_ByReceiverId.empty())
                return false;
        }
        const int receiverId = ReceiverOf(equipId);
        if (receiverId <= 0)
            return false;
        std::lock_guard<std::mutex> lock(g_Mutex);
        const auto it = g_ByReceiverId.find(receiverId);
        if (it == g_ByReceiverId.end())
            return false;
        *out = it->second;
        return true;
    }

    int ReachedLevel(const ChargeSpec& spec, float elapsed, int magazine, bool infinite)
    {
        int level = 0;
        for (std::size_t i = 0; i < spec.levels.size(); ++i)
        {
            const equip::ChargeShotLevel& l = spec.levels[i];
            if (elapsed < l.time)
                break;
            if (!infinite && magazine < l.ammo)
                break;
            level = static_cast<int>(i) + 1;
        }
        return level;
    }

    void UpdateHud(SlotState& s, int equipId)
    {
        if (equipId != s.knownEquipId || s.knownGeneration != g_Generation.load())
        {
            ChargeSpec spec;
            s.knownEquipId = equipId;
            s.knownGeneration = g_Generation.load();
            s.knownHud = equipId > 0 && FindSpec(equipId, &spec) && spec.style.hud;
        }
        int value = 0;
        if (s.knownHud && s.phase == Phase::Charging && !s.spec.levels.empty())
        {
            const float ratio = s.elapsed / s.spec.levels.back().time;
            value = static_cast<int>((ratio > 1.0f ? 1.0f : ratio) * kHudMax);
        }
        g_HudEquipId.store(s.knownHud ? equipId : 0);
        g_HudValue.store(value);
    }

    void Cancel(SlotState& s, std::uint32_t slot)
    {
        StopChargeSound(s, slot);
        StopChargeEffects(s, false);
        if (s.phase == Phase::Charging)
            V_FrameWork::EmitMessage("Player", "OnChargeCancel", s.equipId);
        s.phase = Phase::Idle;
        s.level = 0;
        s.elapsed = 0.0f;
    }

    void OnLevelReached(SlotState& s, std::uint8_t* work, std::uint32_t slot, int reached)
    {
        const equip::ChargeShotLevel& level = s.spec.levels[static_cast<std::size_t>(reached - 1)];
        PlaySoundSEH(s.attackAction, work, slot, level.sound, nullptr);
        if (reached == static_cast<int>(s.spec.levels.size()))
            PlaySoundSEH(s.attackAction, work, slot, s.spec.style.fullSound, nullptr);
        HoldLevelEffect(s, level.effect, level.effectVariation);
        V_FrameWork::EmitMessage("Player", "OnChargeLevel", s.equipId, reached);
    }

    void Tick(void* self, std::uint8_t* work, std::uint32_t slot)
    {
        if (slot >= kSlotCount)
            return;
        SlotState& s = g_Slots[slot];
        s.attackAction = self;
        if (s.armedTicks > 0 && --s.armedTicks == 0)
            s.armedLevel = 0;
        ServiceShots(slot);

        WorkView v{};
        if (!ReadWorkSEH(self, work, slot, &v))
        {
            static bool warned = false;
            if (!warned)
            {
                warned = true;
                LogWarn("ChargeShot: reading player %u's attack state faulted, no charge for this player", slot);
            }
            return;
        }
        UpdateImpacts(slot, v.dt);

        const bool gunAttack = ((v.attackBits >> 6) & 7) == kGunAttack;
        const bool underBarrel = ((v.attackBits >> 9) & 1) != 0;
        const int equipId = RealEquipId(v.engineEquipId);
        const bool held = (v.heldButtons & kFireButton) != 0;
        const bool infinite = (v.flags & kInfiniteAmmo) != 0;
        if (equipId != s.tracedHeldEquipId)
        {
            s.tracedHeldEquipId = equipId;
        }

        if (s.phase != Phase::Idle && (work != s.work || equipId != s.equipId || !gunAttack || underBarrel))
        {
            Cancel(s, slot);
            s.waitForRelease = false;
        }

        if (s.phase == Phase::Idle)
        {
            if (!held)
                s.waitForRelease = false;
            UpdateHud(s, gunAttack && !underBarrel ? equipId : 0);
            if (v.shotCount == 0 || !gunAttack || underBarrel || equipId <= 0)
                return;
            ChargeSpec spec;
            const bool charges = FindSpec(equipId, &spec) && !spec.levels.empty();
            if (equipId != s.tracedShotEquipId)
            {
                s.tracedShotEquipId = equipId;
            }
            if (!charges)
                return;
            HoldShotSEH(work);
            if (s.waitForRelease)
                return;
            s.phase = Phase::Charging;
            s.work = work;
            s.equipId = equipId;
            s.spec = spec;
            s.level = 0;
            s.elapsed = 0.0f;
            PlaySoundSEH(self, work, slot, s.spec.style.chargeSound, &s.chargeSound);
            HoldGunForEffects(s, work);
            StartChargeEffect(s);
            V_FrameWork::EmitMessage("Player", "OnChargeStart", equipId);
            return;
        }

        HoldShotSEH(work);

        if (s.phase == Phase::Cooldown)
        {
            UpdateHud(s, equipId);
            s.cooldownLeft -= v.dt;
            if (s.cooldownLeft <= 0.0f)
            {
                s.phase = Phase::Idle;
                s.waitForRelease = held;
            }
            return;
        }

        if (!IsWeaponReady(v.flags))
        {
            Cancel(s, slot);
            s.waitForRelease = held;
            UpdateHud(s, equipId);
            return;
        }

        StartQueuedEffects(s);
        if (held)
        {
            s.elapsed += v.dt;
            if (s.chargeSound != 0)
                MoveSoundSEH(self, work, slot, s.chargeSound);
            const int reached = ReachedLevel(s.spec, s.elapsed, v.magazine, infinite);
            for (int next = s.level + 1; next <= reached; ++next)
                OnLevelReached(s, work, slot, next);
            if (reached > s.level)
                s.level = reached;
            UpdateHud(s, equipId);
            return;
        }

        if (s.level <= 0)
        {
            Cancel(s, slot);
            UpdateHud(s, equipId);
            return;
        }

        StopChargeSound(s, slot);
        StopChargeEffects(s, false);
        const equip::ChargeShotLevel& level = s.spec.levels[static_cast<std::size_t>(s.level - 1)];
        RequestShotSEH(work);
        PlaySoundSEH(self, work, slot, level.fireSound, nullptr);
        s.armedLevel = s.level;
        s.armedTicks = 2;
        V_FrameWork::EmitMessage("Player", "OnChargeShot", s.equipId, s.level);
        s.phase = Phase::Cooldown;
        s.cooldownLeft = s.spec.cooldown;
        s.level = 0;
        s.elapsed = 0.0f;
        UpdateHud(s, equipId);
    }

    void __fastcall hkUpdateShootingByPad(void* self, std::uint8_t* work, std::uint32_t slot)
    {
        g_Orig(self, work, slot);
        if (MissionCodeGuard::ShouldBypassHooks())
            return;
        Tick(self, work, slot);
    }

    void ResolveDefaults(ChargeSpec& spec)
    {
        if (spec.style.chargeSound == equip::kChargeShotDefaultSound)
            spec.style.chargeSound = kWalkerChargeSound;
        if (spec.style.fullSound == equip::kChargeShotDefaultSound)
            spec.style.fullSound = kWalkerFullSound;
        const std::size_t count = spec.levels.size();
        for (std::size_t i = 0; i < count; ++i)
        {
            equip::ChargeShotLevel& level = spec.levels[i];
            const int row = i + 1 == count ? 2 : (i == 0 ? 0 : 1); // first, middle and last level take the walker's three sounds
            if (level.sound == equip::kChargeShotDefaultSound)
                level.sound = kWalkerLevelSounds[row];
            if (level.fireSound == equip::kChargeShotDefaultSound)
                level.fireSound = 0;
            if (level.effect == equip::kChargeShotDefaultEffect)
                level.effect = kChargeFlash;
        }
    }

    void ForgetAll()
    {
        for (std::uint32_t slot = 0; slot < kSlotCount; ++slot)
        {
            for (ImpactEffect& impact : g_Impacts[slot])
                RemoveImpact(impact);
            g_Slots[slot] = SlotState{};
        }
        {
            std::lock_guard<std::mutex> lock(g_ShotMutex);
            for (std::uint32_t slot = 0; slot < kSlotCount; ++slot)
            {
                g_Pending[slot] = PendingShot{};
                g_ShotLines[slot] = ShotLine{};
            }
        }
        g_HudEquipId.store(0);
        g_HudValue.store(0);
        g_PanelEquipId.store(-1);
    }
}

namespace equip
{
    void ChargeShot_Register(int equipId, int receiverId, float cooldown, const ChargeShotStyle& style,
                             const std::vector<ChargeShotLevel>& levels)
    {
        ChargeSpec spec;
        spec.cooldown = cooldown;
        spec.style = style;
        spec.levels = levels;
        ResolveDefaults(spec);
        {
            std::lock_guard<std::mutex> lock(g_Mutex);
            if (equipId > 0)
                g_ByEquipId[equipId] = spec;
            else
                g_ByReceiverId[receiverId] = spec;
        }
        ++g_Generation;
        LogInfo("SetChargeShot %s:%d levels:%zu", equipId > 0 ? "equipId" : "receiverId",
                equipId > 0 ? equipId : receiverId, spec.levels.size());
    }

    bool ChargeShot_BeginFire(std::uint8_t* work, std::uint32_t slot, ChargeShotFire* out)
    {
        if (slot >= kSlotCount || !work)
            return false;
        SlotState& s = g_Slots[slot];
        if (s.armedLevel <= 0 || s.work != work)
            return false;
        const std::size_t index = static_cast<std::size_t>(s.armedLevel - 1);
        if (index >= s.spec.levels.size())
            return false;
        const ChargeShotLevel& level = s.spec.levels[index];
        out->level = s.armedLevel;
        out->ammo = level.ammo;
        int total = 0;
        if (!ReadAmmoSEH(work, &out->magazineBefore, &total, &out->infiniteAmmo))
            return false;
        s.armedLevel = 0;
        s.armedTicks = 0;

        PendingShot pending;
        pending.ticksLeft = kPendingTicks;
        pending.bulletId = level.bulletId;
        pending.runs = level.shots;
        pending.impactEffect = level.impactEffect;
        pending.impactVariation = level.impactVariation;
        std::lock_guard<std::mutex> lock(g_ShotMutex);
        g_Pending[slot] = pending;
        return true;
    }

    void ChargeShot_EndFire(void* attackAction, std::uint8_t* work, std::uint32_t slot,
                            const ChargeShotFire& shot)
    {
        if (shot.infiniteAmmo || !g_SetAmmoCount)
            return;
        int magazine = 0;
        int total = 0;
        bool infinite = false;
        if (!ReadAmmoSEH(work, &magazine, &total, &infinite) || infinite)
            return;
        const int wanted = shot.magazineBefore > shot.ammo ? shot.magazineBefore - shot.ammo : 0;
        if (wanted == magazine)
            return;
        const int wantedTotal = total - magazine + wanted;
        if (!SetAmmoCountSEH(attackAction, work, slot, wanted, wantedTotal > 0 ? wantedTotal : 0))
        {
            static bool warned = false;
            if (!warned)
            {
                warned = true;
                LogWarn("ChargeShot: magazine write faulted, the shot used one round");
            }
        }
    }

    void ChargeShot_BeforeDoFire(std::uint8_t* gunRecord, bool secondary, void* request, ChargeShotDoFire* out)
    {
        out->runs = 1;
        out->swapped = false;
        int owner = 0;
        float start[3] = {};
        float dir[3] = {};
        if (!gunRecord || !request || !ReadShotSEH(request, &owner, start, dir))
            return;
        const std::uint32_t slot = static_cast<std::uint32_t>(owner) & 0x1FF; // a player's GameObjectId is its slot
        if ((owner >> 9) != 0 || slot >= kSlotCount)
            return;

        PendingShot pending;
        {
            std::lock_guard<std::mutex> lock(g_ShotMutex);
            if (g_Pending[slot].ticksLeft <= 0)
                return;
            pending = g_Pending[slot];
            g_Pending[slot].ticksLeft = 0;
            if (pending.impactEffect != 0)
            {
                const float length = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
                if (length > 1e-4f)
                {
                    ShotLine& shot = g_ShotLines[slot];
                    shot.ready = true;
                    shot.start[0] = start[0]; shot.start[1] = start[1]; shot.start[2] = start[2];
                    shot.dir[0] = dir[0] / length; shot.dir[1] = dir[1] / length; shot.dir[2] = dir[2] / length;
                    shot.impactEffect = pending.impactEffect;
                    shot.impactVariation = pending.impactVariation;
                }
            }
        }
        out->slot = static_cast<int>(slot);
        out->runs = pending.runs;
        if (pending.bulletId <= 0)
            return;
        out->secondary = secondary;
        const int bulletType = EquipParam_GetBulletType(pending.bulletId);
        const int lane = EquipParam_BulletLaneForUse(pending.bulletId);
        out->swapped = SwapRecordSEH(gunRecord, secondary, static_cast<std::uint8_t>(lane), bulletType, out);
    }

    void ChargeShot_AfterDoFire(std::uint8_t* gunRecord, const ChargeShotDoFire& state)
    {
        if (state.swapped && gunRecord)
            RestoreRecordSEH(gunRecord, state);
    }

    int ChargeShot_RealEquipId(int engineEquipId)
    {
        return RealEquipId(engineEquipId);
    }

    bool ChargeShot_HudCharge(int* equipId, int* value, int* max)
    {
        *equipId = g_HudEquipId.load();
        *value = g_HudValue.load();
        *max = kHudMax;
        return *equipId > 0;
    }

    int ChargeShot_HudLevels(int equipId)
    {
        const unsigned generation = g_Generation.load();
        if (equipId != g_PanelEquipId.load() || generation != g_PanelGeneration.load())
        {
            ChargeSpec spec;
            const bool hud = equipId > 0 && FindSpec(equipId, &spec) && spec.style.hud;
            g_PanelLevels.store(hud ? static_cast<int>(spec.levels.size()) : 0);
            g_PanelGeneration.store(generation);
            g_PanelEquipId.store(equipId);
        }
        return g_PanelLevels.load();
    }

    void ChargeShot_ClearForNewMission()
    {
        ForgetAll();
    }
}

bool Install_AttackActionImpl_UpdateShootingByPad_Hook()
{
    if (g_HookTarget)
        return true;
    if (!gAddr.AttackActionImpl_UpdateShootingByPad)
    {
        LogError("ChargeShot: no UpdateShootingByPad address for this build, SetChargeShot off");
        return false;
    }
    void* target = ResolveGameAddress(gAddr.AttackActionImpl_UpdateShootingByPad);
    const HookEntry entry = CheckHookEntry(target, kUpdateShootingByPadEntry, sizeof(kUpdateShootingByPadEntry));
    if (entry == HookEntry::Unknown)
    {
        char found[160] = {};
        DescribeFunctionEntry(target, found, sizeof(found));
        LogError("ChargeShot: UpdateShootingByPad does not match (%s), SetChargeShot off", found);
        return false;
    }
    if (entry == HookEntry::ForeignHook)
        LogHookedOnTop("ChargeShot: UpdateShootingByPad", target);
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hkUpdateShootingByPad),
                             reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("ChargeShot: UpdateShootingByPad hook failed @ %p, SetChargeShot off", target);
        return false;
    }
    g_HookTarget = target;

    void* setAmmo = gAddr.AttackActionImpl_SetAmmoCount
        ? ResolveGameAddress(gAddr.AttackActionImpl_SetAmmoCount) : nullptr;
    if (setAmmo && CheckHookEntryMasked(setAmmo, kSetAmmoCountEntry, kSetAmmoCountMask,
                                        sizeof(kSetAmmoCountEntry)) != HookEntry::Unknown)
        g_SetAmmoCount = reinterpret_cast<SetAmmoCount_t>(setAmmo);
    else
        LogError("ChargeShot: SetAmmoCount not found on this build, every level uses one round");

    g_GetQuarkSystemTable = gAddr.GetQuarkSystemTable
        ? reinterpret_cast<GetQuarkSystemTable_t>(ResolveGameAddress(gAddr.GetQuarkSystemTable)) : nullptr;
    if (!g_GetQuarkSystemTable)
        LogError("ChargeShot: GetQuarkSystemTable address missing, weapons built in Customize do not charge");

    g_Impact.getEffectController = ResolveCall<GetEffectController_t>(gAddr.EquipSystemImpl_GetEffectController,
                                                                      kGetEffectControllerEntry);
    g_Impact.chimeraEffectController = ResolveCall<ChimeraEffectController_t>(gAddr.ChimeraSystemImpl_GetEffectController,
                                                                              kChimeraEffectControllerEntry);
    if (!g_Impact.getEffectController || !g_Impact.chimeraEffectController)
        LogError("ChargeShot: gun effect lookups not found on %s, chargeEffect and level effects show nothing",
                 GetGameBuildName(gGameBuild));
    g_Impact.effectWorldMatrix = ResolveCall<EffectWorldMatrix_t>(gAddr.SimplePartsEffectControllerImpl_GetEffectWorldMatrix,
                                                                  kEffectWorldMatrixEntry);
    g_Impact.createEffect = gAddr.FoxFxEffect_Create
        ? reinterpret_cast<CreateFx_t>(ResolveGameAddress(gAddr.FoxFxEffect_Create)) : nullptr;
    g_Impact.setVisible = ResolveCall<SetFxFlag_t>(gAddr.FoxFxEffect_SetVisible, kSetVisibleEntry);
    g_Impact.setKillNow = ResolveCall<SetFxFlag_t>(gAddr.FoxFxEffect_SetKillNow, kSetKillNowEntry);
    g_Impact.sendDestroy = ResolveCall<SendFxDestroy_t>(gAddr.FoxFxEffect_SendDestroy, kSendDestroyEntry);
    g_Impact.setSharedPtr = ResolveCall<SetSharedPtr_t>(gAddr.FoxSharedPtrBase_Set, kSetSharedPtrEntry);
    g_Impact.getHitPosition = ResolveCall<GetHitPosition_t>(gAddr.FoxGeoResultI_GetPosition, kGetHitPositionEntry);
    g_Impact.constructGeoControl = ResolveCall<ConstructGeoControl_t>(gAddr.GeoCheckControlN1_Construct,
                                                                      kConstructGeoControlEntry);
    g_Impact.lineCheck = ResolveCall<LineCheck_t>(gAddr.FoxGeo_CollisionLineCheck, kLineCheckEntry);
    g_ImpactReady = g_Impact.getEffectController && g_Impact.effectWorldMatrix
        && g_Impact.createEffect && g_Impact.setVisible && g_Impact.setKillNow && g_Impact.sendDestroy
        && g_Impact.setSharedPtr && g_Impact.getHitPosition && g_Impact.constructGeoControl && g_Impact.lineCheck;
    if (!g_ImpactReady)
        LogError("ChargeShot: effect or geometry calls not found on %s, impactEffect shows nothing",
                 GetGameBuildName(gGameBuild));
    return true;
}

void Uninstall_AttackActionImpl_UpdateShootingByPad_Hook()
{
    if (g_HookTarget)
        DisableAndRemoveHook(g_HookTarget);
    g_HookTarget = nullptr;
    g_Orig = nullptr;
    for (std::uint32_t slot = 0; slot < kSlotCount; ++slot)
    {
        StopChargeSound(g_Slots[slot], slot);
        StopChargeEffects(g_Slots[slot], true);
    }
    ForgetAll();
    g_SetAmmoCount = nullptr;
    g_GetQuarkSystemTable = nullptr;
    g_EffectSlotState = 0;
    g_ChimeraState = 0;
    g_GunEffectCallState = 0;
    g_Impact = ImpactCalls{};
    g_ImpactReady = false;
    std::lock_guard<std::mutex> lock(g_Mutex);
    g_ByEquipId.clear();
    g_ByReceiverId.clear();
}
