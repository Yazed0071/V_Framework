#include <pch.h>

#include "RideWalkerGearActionImpl_SubjectiveCamera.h"

#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <cstdint>

#include "AddressSet.h"
#include "BattleGearWeapons.h"
#include "BuddyCodeCave.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    using Polling_t       = bool(__fastcall*)(std::uint8_t* self, std::uint32_t index, std::uint32_t id, const void* context);
    using UpdateBase_t     = void(__fastcall*)(std::uint8_t* self, std::uint32_t index, std::uint8_t mode, std::uint32_t state);
    using CameraButton_t   = int(__fastcall*)(void* input, int group, int mask);
    using VehicleScope_t   = void(__fastcall*)(void* ui, std::uint32_t sightType);

    // PollingForSubjectiveCamera runs on the subjective-camera interface 0x40 into the ride action
    constexpr std::size_t kOwner          = 0x40;    // back to the action plugin
    constexpr std::size_t kSlots          = 0x10;    // back to the player slots: bits at +0x08, first index at +0x24
    constexpr std::size_t kSlotBits       = 0x08;
    constexpr std::size_t kFirstSlot      = 0x24;
    constexpr std::size_t kWorks          = 0x08;
    constexpr std::size_t kWorkSize       = 0xB0;
    constexpr std::size_t kWorkFlags      = 0xA0;
    constexpr std::uint32_t kSubjective   = 0x400;   // what the walker's own first person (player mode 0x12) sets
    constexpr std::size_t kPlayerSystem   = 0x138;
    constexpr std::size_t kInput          = 0xB0;
    constexpr std::size_t kCameraButtonSlot = 0x90 / sizeof(void*);
    constexpr int         kCameraButtonGroup = 10;   // the tanks' first-person toggle: RideVehicleActionImpl::PollingForSubjectiveCamera
    constexpr int         kCameraButtonMask  = 0xC;
    constexpr int         kPressed           = 2;
    constexpr std::size_t kUiController      = 0xD8;   // player::UiController: ShowVehicleScope(type) vt+0x60, HideVehicleScope vt+0x68
    constexpr std::size_t kShowScopeSlot     = 0x60 / sizeof(void*);
    constexpr std::size_t kHideScopeSlot     = 0x68 / sizeof(void*);
    constexpr std::uint32_t kMainGunScope    = 0x20;   // what GetSubjectiveSightType gives the tanks' main gun (vehicle kind 7)

    // UpdateCameraBasePosition runs on the ride action itself
    constexpr std::size_t kAction         = 0x08;    // the action plugin holding the player system
    constexpr std::size_t kPlayerCamera   = 0xB8;
    constexpr std::size_t kCameraBase     = 0x170;   // the camera's base point, x y z w
    constexpr std::uint8_t kModeSubjective = 1;
    constexpr std::uint8_t kModeReady      = 2;      // weapon ready: the view circles the seat
    constexpr std::uint8_t kModeDrive      = 5;      // driving: it circles the walker's own position, and the rider's while he
                                                     // gets on or off (the base point blends from one to the other)

    constexpr float kCupolaEye = 1.3f;    // metres above where the rider stands in the hatch: over the turret's sensor ball
    constexpr float kMgEyeBack = 1.4f;    // behind the roof MG's muzzle along its barrel, and over it: at its sights
    constexpr float kMgEyeUp   = 0.25f;
    // third person circles the D-Walker (its own position driving, its seat with the weapon ready: 1 m over the ground and under
    // the Battle Gear's hull); the tanks' view circles a point over the turret and looks down across it. The cupola stands 1.9 m
    // above the hull's middle.
    constexpr float kRidePivot   = 2.3f;    // metres above the hull's middle the driving view circles: 0.4 over the cupola
    constexpr float kAimPivot    = 3.0f;    // and the aiming view, over the turret its neck has raised
    constexpr float kPivotEase   = 0.25f;   // seconds its height takes after the hull's: a step or a ledge does not jolt the view
    constexpr float kPivotBlend  = 0.8f;    // seconds from the game's base point up to this one, after the climb in
    constexpr float kLongestFrame = 0.1f;
    // the walker's StateDrive asks the player camera for its preset each frame: mov rax, [r14] / mov edx, r13d, then
    // mov rcx, r14 / call [rax+0x298] with the preset in r8d
    constexpr std::uint8_t kPresetSite[] = { 0x49, 0x8B, 0x06, 0x41, 0x8B, 0xD5 };
    constexpr std::uint32_t kTankCloseView = 0x18;   // the tanks' middle view (0x17 the wheeled armoured vehicles')

    const std::uint8_t kPollingEntry[]    = { 0x48, 0x8B, 0x41, 0xF0, 0x44, 0x8B, 0xC2, 0x44, 0x8B, 0xDA, 0x44, 0x2B, 0x40, 0x24 };
    const std::uint8_t kUpdateBaseEntry[] = { 0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x56, 0x48, 0x83, 0xEC, 0x20 };

    Polling_t    g_OrigPolling = nullptr;
    UpdateBase_t g_OrigUpdateBase = nullptr;
    void*        g_PollingTarget = nullptr;
    void*        g_UpdateBaseTarget = nullptr;
    bool         g_ScopeShown = false;
    std::uint8_t g_CloseView = 0;        // the middle of the three views: the cave reads it
    BuddyCodeCave g_PresetCave{ "Battle Gear close view" };
    bool         g_PivotKnown = false;
    float        g_PivotHeight = 0.0f;   // world height the third-person view circles
    float        g_Eye[3] = {};          // the first-person camera's base point last written
    bool         g_EyeKnown = false;
    float        g_PivotShare = 0.0f;    // 0 the game's base point, 1 ours
    LONGLONG     g_PivotClock = 0;
#ifdef _DEBUG
    ULONGLONG    g_NextPivotLog = 0;
#endif

    struct Slot
    {
        std::uint32_t* flags;
        void*          input;
        void*          ui;
    };

    bool ReadSlotSEH(std::uint8_t* self, std::uint32_t index, Slot& slot)
    {
        __try
        {
            const auto* slots = *reinterpret_cast<const std::uint8_t* const*>(self - kSlots);
            const std::uint32_t at = index - *reinterpret_cast<const std::uint32_t*>(slots + kFirstSlot);
            const auto* bits = *reinterpret_cast<const std::uint32_t* const*>(slots + kSlotBits);
            if (((bits[at >> 5] >> (at & 0x1F)) & 1) == 0)
                return false;
            auto* works = *reinterpret_cast<std::uint8_t* const*>(self + kWorks);
            const auto* owner = *reinterpret_cast<const std::uint8_t* const*>(self - kOwner);
            const auto* system = *reinterpret_cast<const std::uint8_t* const*>(owner + kPlayerSystem);
            slot.flags = reinterpret_cast<std::uint32_t*>(works + static_cast<std::uint64_t>(at) * kWorkSize + kWorkFlags);
            slot.input = *reinterpret_cast<void* const*>(system + kInput);
            slot.ui = *reinterpret_cast<void* const*>(system + kUiController);
            return works && slot.input;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool MarkSubjectiveSEH(std::uint32_t* flags)
    {
        __try
        {
            *flags |= kSubjective;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool CallScopeSEH(void* ui, std::size_t slot)
    {
        if (!ui)
            return false;
        __try
        {
            const auto call = reinterpret_cast<VehicleScope_t>((*static_cast<void* const* const*>(ui))[slot]);
            call(ui, kMainGunScope);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    // the tanks ask for their scope each frame it shows; one that is not loaded shows nothing (the HUD finds no window for its type)
    void ShowScope(void* ui, bool show)
    {
        if (!show && !g_ScopeShown)
            return;
        if (CallScopeSEH(ui, show ? kShowScopeSlot : kHideScopeSlot))
        {
#ifdef _DEBUG
            if (show != g_ScopeShown)
                LogDebug("BattleGear first person: main-gun scope %s", show ? "asked for" : "hidden");   // TEMP probe
#endif
            g_ScopeShown = show;
        }
    }

    std::uint8_t* PlayerCameraSEH(std::uint8_t* self)
    {
        __try
        {
            const auto* action = *reinterpret_cast<const std::uint8_t* const*>(self + kAction);
            const auto* system = *reinterpret_cast<const std::uint8_t* const*>(action + kPlayerSystem);
            return *reinterpret_cast<std::uint8_t* const*>(system + kPlayerCamera);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

#ifdef _DEBUG
    // TEMP probe: the player's stage block position setter (enable byte +0x14, whole-metre x y z at +0x18), what the map
    // streams its detail round
    bool StageBlockSetterSEH(std::uint8_t* self, int* place, int* enabled)
    {
        __try
        {
            const auto* action = *reinterpret_cast<const std::uint8_t* const*>(self + kAction);
            const auto* system = *reinterpret_cast<const std::uint8_t* const*>(action + kPlayerSystem);
            const auto* setter = *reinterpret_cast<const std::uint8_t* const*>(system + 0x2C8);
            if (!setter)
                return false;
            *enabled = setter[0x14];
            for (int k = 0; k < 3; ++k)
                place[k] = *reinterpret_cast<const std::int32_t*>(setter + 0x18 + 4 * k);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }
#endif

    bool WriteBaseSEH(std::uint8_t* camera, const float* eye)
    {
        __try
        {
            float* base = reinterpret_cast<float*>(camera + kCameraBase);
            base[0] = eye[0];
            base[1] = eye[1];
            base[2] = eye[2];
            base[3] = 1.0f;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool RaiseBaseSEH(std::uint8_t* camera, float share, float& from)
    {
        __try
        {
            float* height = reinterpret_cast<float*>(camera + kCameraBase) + 1;
            from = *height;
            *height += (g_PivotHeight - *height) * share;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    float SecondsSinceLastPivot()
    {
        LARGE_INTEGER now{};
        LARGE_INTEGER perSecond{};
        QueryPerformanceCounter(&now);
        QueryPerformanceFrequency(&perSecond);
        const float dt = g_PivotClock && perSecond.QuadPart > 0
                       ? static_cast<float>(static_cast<double>(now.QuadPart - g_PivotClock) / static_cast<double>(perSecond.QuadPart)) : 0.0f;
        g_PivotClock = now.QuadPart;
        return std::clamp(dt, 0.0f, kLongestFrame);
    }

    // the game put the base point on the D-Walker: it keeps its place over the ground and rises over the turret, at the hull's own
    // height (the walker under it hops onto ledges and off them), from the climb in on; it comes back down through the climb out
    void RaisePivot(std::uint8_t* self, std::uint8_t mode, std::uint32_t state)
    {
        alignas(16) float hull[16];
        float clearance = 0.0f;
        std::uint8_t* camera = PlayerCameraSEH(self);
        if (!camera || !battleGearWeapons::HullFrame(hull, &clearance))
            return;
        const float dt = SecondsSinceLastPivot();
        const float goal = hull[13] + (battleGearWeapons::Aiming() ? kAimPivot : kRidePivot);
        g_PivotHeight = g_PivotKnown ? g_PivotHeight + (goal - g_PivotHeight) * (1.0f - std::exp(-dt / kPivotEase)) : goal;
        g_PivotKnown = true;
        const bool seated = battleGearWeapons::Ridden() && !battleGearWeapons::Climbing();
        g_PivotShare = std::clamp(g_PivotShare + (seated ? dt : -dt) / kPivotBlend, 0.0f, 1.0f);
        float from = 0.0f;
        const bool raised = RaiseBaseSEH(camera, g_PivotShare * g_PivotShare * (3.0f - 2.0f * g_PivotShare), from);
#ifdef _DEBUG
        const ULONGLONG now = GetTickCount64();   // TEMP probe: the ride's camera mode, and the base point's height as the game set it
        if (now >= g_NextPivotLog)
        {
            g_NextPivotLog = now + 1000;
            LogDebug("BattleGear camera: mode %u state 0x%X, the game's base point %.2f m high, ours %.2f (hull's middle %.2f), share "
                     "%.2f%s", mode, state, from, g_PivotHeight, hull[13], g_PivotShare, raised ? "" : ", base write failed");
            int block[3] = {};
            int enabled = -1;
            if (StageBlockSetterSEH(self, block, &enabled))
                LogDebug("BattleGear stage block: the player's setter %s at %d %d %d, the hull at %.0f %.0f %.0f (%.0f m apart)",
                         enabled ? "on" : "off", block[0], block[1], block[2], hull[12], hull[13], hull[14],
                         std::sqrt((block[0] - hull[12]) * (block[0] - hull[12]) + (block[2] - hull[14]) * (block[2] - hull[14])));
            else
                LogDebug("BattleGear stage block: the player's setter could not be read");
        }
#endif
        (void)raised;
    }

    bool __fastcall hkPolling(std::uint8_t* self, std::uint32_t index, std::uint32_t id, const void* context)
    {
        const bool vanilla = g_OrigPolling(self, index, id, context);
        Slot slot{};
        if (vanilla || !ReadSlotSEH(self, index, slot))
            return vanilla;
        if (!battleGearWeapons::Ridden() || battleGearWeapons::Climbing())
        {
            battleGearWeapons::SetFirstPerson(false);   // getting in or out is seen from outside, as from a tank
            g_CloseView = 0;
        }
        else
        {
            const auto button = reinterpret_cast<CameraButton_t>((*static_cast<void* const* const*>(slot.input))[kCameraButtonSlot]);
            if (button(slot.input, kCameraButtonGroup, kCameraButtonMask) == kPressed)   // driving, close, first person, as a tank
            {
                if (battleGearWeapons::FirstPerson())
                    battleGearWeapons::SetFirstPerson(false);
                else if (g_CloseView || !g_PresetCave.installed)
                {
                    g_CloseView = 0;
                    battleGearWeapons::SetFirstPerson(true);
                }
                else
                    g_CloseView = 1;
#ifdef _DEBUG
                LogDebug("BattleGear view: %s", battleGearWeapons::FirstPerson() ? "first person"   // TEMP probe
                         : (g_CloseView ? "close (the tanks' preset 0x18)" : "driving"));
#endif
            }
        }
        const bool firstPerson = battleGearWeapons::FirstPerson();
        ShowScope(slot.ui, firstPerson && !battleGearWeapons::SidearmInHand());   // the rail-gun and the missiles; the MG has open sights
        return firstPerson && MarkSubjectiveSEH(slot.flags);
    }

    void __fastcall hkUpdateBase(std::uint8_t* self, std::uint32_t index, std::uint8_t mode, std::uint32_t state)
    {
        g_OrigUpdateBase(self, index, mode, state);
        g_EyeKnown = false;
        const bool ridden = battleGearWeapons::Ridden();
        if (!ridden && !battleGearWeapons::Climbing())
        {
            g_PivotKnown = false;
            g_PivotShare = 0.0f;
            g_PivotClock = 0;
            return;
        }
        if (!battleGearWeapons::FirstPerson() && (mode == kModeReady || mode == kModeDrive))
        {
            RaisePivot(self, mode, state);
            return;
        }
        if (mode != kModeSubjective || !ridden || !battleGearWeapons::FirstPerson())
            return;
        alignas(16) float hatch[16];
        if (!battleGearWeapons::Hatch(hatch))
            return;
        std::uint8_t* camera = PlayerCameraSEH(self);
        if (!camera)
            return;
        float eye[3] = { hatch[12] + hatch[4] * kCupolaEye, hatch[13] + hatch[5] * kCupolaEye, hatch[14] + hatch[6] * kCupolaEye };
        float muzzle[3];
        float barrel[3];
        if (battleGearWeapons::SidearmInHand() && battleGearWeapons::MuzzleFor(1, 0, muzzle) && battleGearWeapons::BarrelFor(1, barrel))
        {
            const float length = std::sqrt(barrel[0] * barrel[0] + barrel[1] * barrel[1] + barrel[2] * barrel[2]);
            if (length > 1e-4f)
                for (int k = 0; k < 3; ++k)
                    eye[k] = muzzle[k] - barrel[k] / length * kMgEyeBack + hatch[4 + k] * kMgEyeUp;
        }
        g_EyeKnown = WriteBaseSEH(camera, eye);
        if (g_EyeKnown)
            std::copy(eye, eye + 3, g_Eye);
    }

    bool InstallPresetCave()
    {
        const std::uintptr_t siteAbs = gAddr.RideWalkerGearActionImpl_StateDrive_CameraPreset;
        const auto base = reinterpret_cast<std::uintptr_t>(siteAbs ? ResolveGameAddress(siteAbs) : nullptr);
        if (!base)
        {
            LogWarn("BattleGear: close view address missing on this build, the camera button only toggles first person");
            return false;
        }
        CaveCode c;
        c.Emit({ 0x48, 0xB8 });                                              // mov rax, &close view
        c.Imm64(reinterpret_cast<std::uint64_t>(&g_CloseView));
        c.Emit({ 0x80, 0x38, 0x00 });                                        // cmp byte [rax], 0
        const std::size_t keep = c.Jcc8(0x74);                               // je
        c.Emit({ 0x41, 0xB8 });                                              // mov r8d, the tanks' close view
        c.Emit({ static_cast<std::uint8_t>(kTankCloseView), 0x00, 0x00, 0x00 });
        c.Bind8(keep);
        c.Emit({ 0x49, 0x8B, 0x06 });                                        // mov rax, [r14]
        c.Emit({ 0x41, 0x8B, 0xD5 });                                        // mov edx, r13d
        c.JmpAbs(base + sizeof(kPresetSite));
        if (!InstallBuddyCodeCave(g_PresetCave, siteAbs, kPresetSite, sizeof(kPresetSite), c))
        {
            LogWarn("BattleGear: close view code differs on this build, the camera button only toggles first person");
            return false;
        }
        return true;
    }

    bool HookOne(std::uintptr_t address, const std::uint8_t* entry, std::size_t size, void* detour, void** original, void*& target,
                 const char* what)
    {
        void* fn = address ? ResolveGameAddress(address) : nullptr;
        if (!fn)
        {
            LogError("BattleGear: %s address missing on this build, riding stays in third person", what);
            return false;
        }
        const HookEntry state = CheckHookEntry(fn, entry, size);
        if (state == HookEntry::Unknown)
        {
            char desc[160] = {};
            DescribeFunctionEntry(fn, desc, sizeof(desc));
            LogError("BattleGear: %s differs on this build (%s), riding stays in third person", what, desc);
            return false;
        }
        if (state == HookEntry::ForeignHook)
            LogHookedOnTop(what, fn);
        if (!CreateAndEnableHook(fn, detour, original))
        {
            LogError("BattleGear: %s hook failed, riding stays in third person", what);
            return false;
        }
        target = fn;
        return true;
    }
}

bool Install_RideWalkerGearActionImpl_SubjectiveCamera_Hooks()
{
    if (g_PollingTarget && g_UpdateBaseTarget)
        return true;
    if (!HookOne(gAddr.RideWalkerGearActionImpl_UpdateCameraBasePosition, kUpdateBaseEntry, sizeof(kUpdateBaseEntry),
                 reinterpret_cast<void*>(&hkUpdateBase), reinterpret_cast<void**>(&g_OrigUpdateBase), g_UpdateBaseTarget,
                 "walker ride camera base"))
        return false;
    if (!HookOne(gAddr.RideWalkerGearActionImpl_PollingForSubjectiveCamera, kPollingEntry, sizeof(kPollingEntry),
                 reinterpret_cast<void*>(&hkPolling), reinterpret_cast<void**>(&g_OrigPolling), g_PollingTarget,
                 "walker ride first-person polling"))
    {
        Uninstall_RideWalkerGearActionImpl_SubjectiveCamera_Hooks();
        return false;
    }
    InstallPresetCave();
    return true;
}

bool BattleGearFirstPersonEye(float* eye)
{
    if (!g_EyeKnown || !battleGearWeapons::FirstPerson())
        return false;
    std::copy(g_Eye, g_Eye + 3, eye);
    return true;
}

void Uninstall_RideWalkerGearActionImpl_SubjectiveCamera_Hooks()
{
    if (g_PollingTarget)
    {
        DisableAndRemoveHook(g_PollingTarget);
        g_PollingTarget = nullptr;
        g_OrigPolling = nullptr;
    }
    if (g_UpdateBaseTarget)
    {
        DisableAndRemoveHook(g_UpdateBaseTarget);
        g_UpdateBaseTarget = nullptr;
        g_OrigUpdateBase = nullptr;
    }
    UninstallBuddyCodeCave(g_PresetCave);
    g_CloseView = 0;
    battleGearWeapons::SetFirstPerson(false);
    g_ScopeShown = false;
    g_EyeKnown = false;
    g_PivotKnown = false;
    g_PivotShare = 0.0f;
    g_PivotClock = 0;
}
