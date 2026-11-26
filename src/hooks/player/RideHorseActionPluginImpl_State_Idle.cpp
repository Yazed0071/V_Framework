#include <pch.h>
#include "RideHorseActionPluginImpl_State_Idle.h"

#include <Windows.h>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"
#include "MissionCodeGuard.h"

namespace
{
    using StateIdle_t  = void(__fastcall*)(std::uint8_t* plugin, std::uint32_t player, std::uint32_t proc, void* param);
    using GetButtons_t = std::uint32_t(__fastcall*)(void* controller);
    using ShowIcon_t   = void(__fastcall*)(void* ui, std::uint32_t type, std::uint32_t icon, bool longPress, std::uint32_t kind,
                                           std::uint32_t message, std::uint32_t messageInDisplay, std::uint32_t messageArg,
                                           void* onLongPressDone, std::uint64_t onLongPressDoneArg);   // UiControllerImpl::Update calls onLongPressDone(type, icon, arg) when the gauge fills and there is no message
    using HideIcon_t   = void(__fastcall*)(void* ui, std::uint32_t type, std::uint32_t icon);

    const std::uint8_t kStateIdleEntry[] = { 0x4C, 0x8B, 0xDC, 0x49, 0x89, 0x6B, 0x18, 0x56, 0x57, 0x41, 0x56, 0x48, 0x81, 0xEC, 0x80, 0x00, 0x00, 0x00 };

    constexpr std::uint32_t kProcEnter       = 3;      // Work::ExecStateChangeImpl: 4 leaves the old state, 3 enters the new one
    constexpr std::uint32_t kProcLeave       = 4;
    constexpr std::uint32_t kProcMotion      = 9;      // the per-frame step that picks idle variants and follows D-Horse
    constexpr std::size_t   kSystemPlayer    = 0x08;   // Player2System -> Player2GameObject
    constexpr std::size_t   kPlayerUi        = 0xD8;   // UiControllerImpl, what Player.RequestToShowIcon / RequestToHideIcon call
    constexpr std::size_t   kShowIconSlot    = 0x2B8 / sizeof(void*);
    constexpr std::size_t   kHideIconSlot    = 0x2C0 / sizeof(void*);
    constexpr std::uint32_t kIconTypeReload  = 1;      // the Reload button
    constexpr std::uint32_t kIconHearts      = 121;
    constexpr std::uint32_t kIconRequestKind = 3;      // what the Lua binding passes
    constexpr std::size_t   kPluginImpl      = 0x08;
    constexpr std::size_t   kPluginPlayers   = 0x38;
    constexpr std::size_t   kPlayersFirst    = 0x24;
    constexpr std::size_t   kPluginWorks     = 0x68;
    constexpr std::size_t   kWorkSize        = 0x2B0;
    constexpr std::size_t   kWorkHorseInfo   = 0x18;   // RideHorseCommunicationInfo, which is D-Horse's own work + 0x70
    constexpr std::size_t   kWorkNextTag     = 0x28;
    constexpr std::size_t   kWorkIdleMode    = 0x58;   // what his idle switch reads: 2 plays snakadsn_idl_3, the pat
    constexpr std::size_t   kImplOwner       = 0x138;
    constexpr std::size_t   kOwnerController = 0xB0;
    constexpr std::size_t   kHeldButtonsSlot = 0x28 / sizeof(void*);   // what PadImpl::Update copies to PlayerVars.scannedButtonsDirect; +0x40 is set for one frame per press
    constexpr std::uint32_t kReloadButton    = 1u << 6;   // PlayerPad.RELOAD
    constexpr std::uint32_t kAimButton       = 1u << 3;   // PlayerPad.HOLD, readying the weapon
    constexpr std::size_t   kInfoInHorseWork = 0x70;
    constexpr std::size_t   kHorseState      = 0x260;
    constexpr std::size_t   kHorseNextState  = 0x261;
    constexpr std::size_t   kHorseIdleMode   = 0x1C0;
    constexpr std::size_t   kHorseAfterDung  = 0x431;  // State_EjectDungEnd sets it; State_RideOnIdle's entry turns it into idle mode 2
    constexpr std::uint8_t  kRideOnIdle      = 6;
    constexpr std::uint32_t kPatMode         = 2;
    constexpr std::uint64_t kIdleTag         = 0xF241F3BCF60Bull;
    constexpr std::uint64_t kTagMask         = 0xFFFFFFFFFFFFull;
    constexpr std::uint64_t kFollowMs        = 3000;
    constexpr std::uint64_t kEarlyPatWaitMs  = 3000;
    constexpr std::uint32_t kMaxPlayers      = 4;

    struct Ride
    {
        std::uint8_t* rider      = nullptr;
        std::uint8_t* horse      = nullptr;
        void*         controller = nullptr;
        std::uint32_t slot       = 0;
    };

    struct HorseIdle
    {
        std::uint8_t  state     = 0;
        std::uint8_t  next      = 0;
        std::uint32_t mode      = 0;
        std::uint8_t  afterDung = 0;
    };

    StateIdle_t g_Orig   = nullptr;
    void*       g_Target = nullptr;

    bool                       g_PattedThisHold[kMaxPlayers] = {};
    std::uint64_t              g_EarlyPatMs[kMaxPlayers] = {};
    std::uint32_t              g_LoggedButtons[kMaxPlayers] = {};
    bool                       g_LoggedNoRide = false;
    bool                       g_PromptShown  = false;
    std::atomic<bool>          g_GaugeFilled{ false };
    std::atomic<std::uint8_t*> g_PatHorse{ nullptr };
    std::atomic<std::uint8_t*> g_PatRider{ nullptr };
    std::atomic<std::uint64_t> g_PatMs{ 0 };

    bool ReadRide_SEH(std::uint8_t* plugin, std::uint32_t player, Ride& ride)
    {
        __try
        {
            const std::uint32_t first = *reinterpret_cast<const std::uint32_t*>(*reinterpret_cast<std::uint8_t* const*>(plugin + kPluginPlayers) + kPlayersFirst);
            if (player < first || player - first >= kMaxPlayers)
                return false;
            ride.slot  = player - first;
            ride.rider = *reinterpret_cast<std::uint8_t**>(plugin + kPluginWorks) + static_cast<std::size_t>(ride.slot) * kWorkSize;
            std::uint8_t* info = *reinterpret_cast<std::uint8_t**>(ride.rider + kWorkHorseInfo);
            const std::uint8_t* impl  = *reinterpret_cast<std::uint8_t* const*>(plugin + kPluginImpl);
            const std::uint8_t* owner = impl ? *reinterpret_cast<std::uint8_t* const*>(impl + kImplOwner) : nullptr;
            ride.controller = owner ? *reinterpret_cast<void* const*>(owner + kOwnerController) : nullptr;
            ride.horse      = info ? info - kInfoInHorseWork : nullptr;
            return ride.horse && ride.controller && *reinterpret_cast<void* const*>(ride.controller);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool ReadHorseIdle_SEH(const std::uint8_t* horse, HorseIdle& idle)
    {
        __try
        {
            idle.state     = horse[kHorseState];
            idle.next      = horse[kHorseNextState];
            idle.mode      = *reinterpret_cast<const std::uint32_t*>(horse + kHorseIdleMode);
            idle.afterDung = horse[kHorseAfterDung];
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void* PlayerUi_SEH()
    {
        auto* system = static_cast<std::uint8_t*>(ResolveGameAddress(gAddr.Player2System));
        if (!system)
            return nullptr;
        __try
        {
            const std::uint8_t* player = *reinterpret_cast<std::uint8_t* const*>(system + kSystemPlayer);
            void* ui = player ? *reinterpret_cast<void* const*>(player + kPlayerUi) : nullptr;
            return ui && *reinterpret_cast<void* const*>(ui) ? ui : nullptr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    void OnPatGaugeFilled(std::uint32_t, std::uint32_t, std::uint64_t)   // called by UiControllerImpl::Update, which drops the icon right after
    {
        g_GaugeFilled.store(true, std::memory_order_relaxed);
    }

    void ShowPatPrompt(bool show)   // the game's own action icon: Reload, hearts, long press
    {
        if (show == g_PromptShown)
            return;
        void* ui = PlayerUi_SEH();
        if (!ui)
            return;
        void** vtable = *reinterpret_cast<void***>(ui);
        if (show)
            reinterpret_cast<ShowIcon_t>(vtable[kShowIconSlot])(ui, kIconTypeReload, kIconHearts, true, kIconRequestKind, 0, 0, 0,
                                                                reinterpret_cast<void*>(&OnPatGaugeFilled), 0);
        else
            reinterpret_cast<HideIcon_t>(vtable[kHideIconSlot])(ui, kIconTypeReload, kIconHearts);
        g_PromptShown = show;
    }

    bool SetAfterDung_SEH(std::uint8_t* horse)
    {
        __try
        {
            horse[kHorseAfterDung] = 1;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool FollowPat_SEH(std::uint8_t* rider)   // true once his idle switch reads the pat
    {
        __try
        {
            if (*reinterpret_cast<const std::uint32_t*>(rider + kWorkIdleMode) != kPatMode)
                return false;
            auto& next = *reinterpret_cast<std::uint64_t*>(rider + kWorkNextTag);
            if ((next & kTagMask) == kIdleTag)
                next = 0;   // his own idle-mode-2 step then requests D-Horse's state, as it does after a dung
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void ForgetEarlyPats()
    {
        for (std::uint64_t& filledMs : g_EarlyPatMs)
            filledMs = 0;
    }

    void WatchForPat(std::uint8_t* plugin, std::uint32_t player)
    {
        Ride ride;
        if (!ReadRide_SEH(plugin, player, ride))
        {
            g_LoggedNoRide = true;
            ShowPatPrompt(false);
            return;
        }
        HorseIdle idle;
        const bool idleRead = ReadHorseIdle_SEH(ride.horse, idle);
        const bool plainIdle = idleRead && idle.state == kRideOnIdle && idle.next == 0 && idle.mode == 0 && idle.afterDung == 0;
        const bool patPlaying = idleRead && idle.state == kRideOnIdle && idle.mode == kPatMode;
        const auto getButtons = reinterpret_cast<GetButtons_t>((*reinterpret_cast<void***>(ride.controller))[kHeldButtonsSlot]);
        const std::uint32_t buttons = getButtons(ride.controller);
        const bool aiming = (buttons & kAimButton) != 0;
        const bool gaugeFilled = g_GaugeFilled.exchange(false, std::memory_order_relaxed);
        if (gaugeFilled)
        {
            g_PromptShown = false;               // the game drops a long-press icon once its gauge fills
            g_PattedThisHold[ride.slot] = true;  // no new prompt until Reload is let go
        }
        else if (!(buttons & kReloadButton))
            g_PattedThisHold[ride.slot] = false;
        const std::uint64_t now = GetTickCount64();
        if (gaugeFilled)
            g_EarlyPatMs[ride.slot] = now;
        else if (aiming || now - g_EarlyPatMs[ride.slot] > kEarlyPatWaitMs)
            g_EarlyPatMs[ride.slot] = 0;
        const bool patWaiting = g_EarlyPatMs[ride.slot] != 0;
        ShowPatPrompt(!aiming && !patPlaying && !patWaiting && !g_PattedThisHold[ride.slot]
                      && g_PatRider.load(std::memory_order_relaxed) != ride.rider);
        if (g_PatRider.load(std::memory_order_relaxed) == ride.rider)
        {
            if (FollowPat_SEH(ride.rider))
            {
                g_PatRider.store(nullptr, std::memory_order_relaxed);
            }
            else if (now - g_PatMs.load(std::memory_order_relaxed) > kFollowMs)
            {
                g_PatRider.store(nullptr, std::memory_order_relaxed);
            }
        }

        if (buttons != g_LoggedButtons[ride.slot])
        {
            g_LoggedButtons[ride.slot] = buttons;
        }
        if (!patWaiting || !plainIdle)
            return;
        g_EarlyPatMs[ride.slot] = 0;
        if (!SetAfterDung_SEH(ride.horse))
        {
            return;
        }
        g_PatMs.store(now, std::memory_order_relaxed);
        g_PatRider.store(ride.rider, std::memory_order_relaxed);
        g_PatHorse.store(ride.horse, std::memory_order_relaxed);
    }

    void __fastcall hkStateIdle(std::uint8_t* plugin, std::uint32_t player, std::uint32_t proc, void* param)
    {
        if (plugin && !MissionCodeGuard::ShouldBypassHooks())
        {
            if (proc == kProcEnter)
            {
                g_PromptShown = false;   // whatever showed it before is gone
                g_GaugeFilled.store(false, std::memory_order_relaxed);
                ForgetEarlyPats();
            }
            else if (proc == kProcLeave)
            {
                ShowPatPrompt(false);
                g_GaugeFilled.store(false, std::memory_order_relaxed);
                ForgetEarlyPats();
            }
            else if (proc == kProcMotion)
                WatchForPat(plugin, player);
        }
        g_Orig(plugin, player, proc, param);
    }
}

bool TakeHorsePatReentry(const std::uint8_t* horseWork)
{
    std::uint8_t* pending = g_PatHorse.load(std::memory_order_relaxed);
    return horseWork && pending == horseWork && g_PatHorse.compare_exchange_strong(pending, nullptr);
}

bool Install_RideHorseActionPluginImpl_State_Idle_Hook()
{
    if (g_Target)
        return true;
    if (!gAddr.RideHorseActionPluginImpl_State_Idle || !gAddr.HorseBasicAction2Impl_State_RideOnIdle)
    {
        LogWarn("HorsePat: riding idle unknown for %s, holding Reload on D-Horse does nothing", GetGameBuildName(gGameBuild));
        return true;
    }

    void* target = ResolveGameAddress(gAddr.RideHorseActionPluginImpl_State_Idle);
    const HookEntry state = CheckHookEntry(target, kStateIdleEntry, sizeof(kStateIdleEntry));
    if (state == HookEntry::Unknown)
    {
        LogError("HorsePat: riding idle differs on %s, holding Reload on D-Horse does nothing", GetGameBuildName(gGameBuild));
        return false;
    }
    if (state == HookEntry::ForeignHook)
        LogHookedOnTop("HorsePat: RideHorseActionPluginImpl idle state", target);

    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hkStateIdle), reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("HorsePat: riding idle hook failed at %p, holding Reload on D-Horse does nothing", target);
        g_Orig = nullptr;
        return false;
    }
    g_Target = target;
    return true;
}

void Uninstall_RideHorseActionPluginImpl_State_Idle_Hook()
{
    ShowPatPrompt(false);
    if (g_Target)
        DisableAndRemoveHook(g_Target);
    g_Target = nullptr;
    g_Orig   = nullptr;
    g_GaugeFilled.store(false, std::memory_order_relaxed);
    ForgetEarlyPats();
    g_PatHorse.store(nullptr, std::memory_order_relaxed);
    g_PatRider.store(nullptr, std::memory_order_relaxed);
}
