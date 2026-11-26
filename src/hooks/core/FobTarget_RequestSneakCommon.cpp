#include <pch.h>
#include "FobTarget_RequestSneakCommon.h"

#include <Windows.h>
#include <atomic>
#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"
#include "MissionCodeGuard.h"
#include "../outfit/MissionPreparationSystemImpl_IsEnableHeadOptionSuit.h"
#include "../ui/FobMission2CallbackImpl_Update.h"
#include "../ui/MbDvcSoundControllerImpl_RequestVoice.h"

namespace
{
    using RequestSneakCommon_t = std::uint8_t (__fastcall*)(void* self, std::uint32_t a2, std::uint32_t a3,
                                                            std::uint32_t a4, std::uint8_t a5, std::uint8_t a6,
                                                            std::uint8_t isEvent, std::uint32_t mode,
                                                            std::uint32_t a9, std::uint8_t a10);
    using OpenErrorPopup_t = std::uint64_t (__fastcall*)(void* self, std::uint32_t code);
    using HudGetInstance_t      = void* (__fastcall*)();
    using SetPopupText_t        = void (__fastcall*)(void* hud, const char* text);
    using SetPopupErrorParam_t  = void (__fastcall*)(void* hud, int value, int format, int decimals);
    using GetLangText_t         = const char* (__fastcall*)(std::uint64_t key);
    using PopupSetErrorType_t   = void (__fastcall*)(void* popup, int type, std::uint32_t code, std::uint8_t flag);
    using PopupStart_t          = std::uint64_t (__fastcall*)(void* popup);

    constexpr std::uint8_t  kFobModeVisit         = 2;
    constexpr std::uint8_t  kRetaliationTab       = 2;                 // FOB menu tab "Retaliation Targets"
    constexpr std::uint8_t  kIndirectTab          = 3;                 // FOB menu tab "Indirect Retaliation Targets"
    constexpr std::uint8_t  kIntruderTab          = 7;                 // FOB menu tab "Intruder", its targets get "Retaliate against this player?"
    constexpr std::uint64_t kFobGearLangKey       = 0x45C697D13F30ull; // V_Framework_dialog_sortie_fob_cnnt
    constexpr DWORD         kRefusalPopupMs       = 2000;
    constexpr std::size_t   kCallbackPopupOffset  = 0x80;
    constexpr std::size_t   kPopupSetErrorTypeOff = 0x310;
    constexpr std::size_t   kPopupStartOff        = 0x300;
    constexpr int           kPopupTypeError       = 3;

    RequestSneakCommon_t g_OrigRequestSneakCommon = nullptr;
    OpenErrorPopup_t     g_OrigOpenErrorPopup     = nullptr;
    bool                 g_SneakHooked            = false;
    bool                 g_PopupHooked            = false;
    std::atomic<DWORD>   g_RefusedAt{ 0 };

    std::uint8_t __fastcall hkRequestSneakCommon(void* self, std::uint32_t a2, std::uint32_t a3, std::uint32_t a4,
                                                 std::uint8_t a5, std::uint8_t a6, std::uint8_t isEvent,
                                                 std::uint32_t mode, std::uint32_t a9, std::uint8_t a10)
    {
        if (mode == kFobModeVisit && outfit::IsFobGearBanned())
        {
            LogWarn("FobGuard: FOB visit refused, custom gear equipped");
            g_RefusedAt.store(GetTickCount() | 1u, std::memory_order_relaxed);
            return 0; // same as vanilla's not-connected return: the FOB screen shows its error popup
        }
        const std::uint8_t sent = g_OrigRequestSneakCommon(self, a2, a3, a4, a5, a6, isEvent, mode, a9, a10);
        const std::uint8_t tab  = SelectedFobMenuTab();
        LogDebug("FobSneakProbe: sent=%u mode=%u wormhole=%u event=%u target=%u/%u/%u a5=%u tab=%u mission=%u", sent, mode, a9,
                 isEvent, a2, a3, a4, a5, tab, MissionCodeGuard::GetCurrentMissionCode());
        const bool retaliationTab = tab == kRetaliationTab || tab == kIndirectTab || tab == kIntruderTab;
        SetRetaliationSortie(sent != 0 && (a9 != 0 || retaliationTab)); // a9 = the request's wormhole_player_id: going in through a retaliation wormhole
        return sent;
    }

    bool OpenRefusalPopup(void* self, std::uint32_t code)
    {
        const auto getHud    = reinterpret_cast<HudGetInstance_t>(ResolveGameAddress(gAddr.HudCommonDataManager_GetInstance));
        const auto setText   = reinterpret_cast<SetPopupText_t>(ResolveGameAddress(gAddr.HudCommonDataManager_SetPopupText));
        const auto setParam  = reinterpret_cast<SetPopupErrorParam_t>(ResolveGameAddress(gAddr.HudCommonDataManager_SetPopupErrorParam));
        const auto langText  = reinterpret_cast<GetLangText_t>(ResolveGameAddress(gAddr.Ui_GetLangText));
        if (!getHud || !setText || !setParam || !langText)
            return false;
        __try
        {
            const char* text = langText(kFobGearLangKey);
            void* hud = getHud();
            void* popup = *reinterpret_cast<void**>(static_cast<std::uint8_t*>(self) + kCallbackPopupOffset);
            if (!hud || !popup || !text || !text[0])
                return false;
            void** vtbl = *reinterpret_cast<void***>(popup);
            setParam(hud, 0, 0, 0); // with a parameter set the popup drops its own system_<code> text, which would win over ours
            reinterpret_cast<PopupSetErrorType_t>(vtbl[kPopupSetErrorTypeOff / sizeof(void*)])(popup, kPopupTypeError, code, 0);
            setText(hud, text); // between the two calls the original OpenErrorPopup makes
            reinterpret_cast<PopupStart_t>(vtbl[kPopupStartOff / sizeof(void*)])(popup);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    std::uint64_t __fastcall hkOpenErrorPopup(void* self, std::uint32_t code)
    {
        const DWORD refusedAt = g_RefusedAt.exchange(0, std::memory_order_relaxed);
        if (refusedAt != 0 && GetTickCount() - refusedAt < kRefusalPopupMs && OpenRefusalPopup(self, code))
            return 0;
        return g_OrigOpenErrorPopup(self, code);
    }
}

bool Install_FobTarget_RequestSneakCommon()
{
    if (!g_SneakHooked)
    {
        void* target = ResolveGameAddress(gAddr.FobTarget_RequestSneakCommon);
        if (!target)
        {
            LogError("FobVisitGuard: RequestSneakCommon address missing for this build, custom gear is not stopped from FOB visits");
            return false;
        }
        g_SneakHooked = CreateAndEnableHook(target, reinterpret_cast<void*>(&hkRequestSneakCommon),
                                            reinterpret_cast<void**>(&g_OrigRequestSneakCommon));
        if (!g_SneakHooked)
        {
            LogError("FobVisitGuard: RequestSneakCommon hook failed, custom gear is not stopped from FOB visits");
            return false;
        }
    }

    if (!g_PopupHooked)
    {
        void* target = ResolveGameAddress(gAddr.FobMission2CallbackImpl_OpenErrorPopup);
        if (target)
            g_PopupHooked = CreateAndEnableHook(target, reinterpret_cast<void*>(&hkOpenErrorPopup),
                                                reinterpret_cast<void**>(&g_OrigOpenErrorPopup));
        if (!g_PopupHooked)
            LogError("FobVisitGuard: OpenErrorPopup hook failed, a refused FOB visit shows the game's error code only");
    }
    return true;
}

void Uninstall_FobTarget_RequestSneakCommon()
{
    if (g_PopupHooked)
    {
        if (void* target = ResolveGameAddress(gAddr.FobMission2CallbackImpl_OpenErrorPopup))
            DisableAndRemoveHook(target);
        g_OrigOpenErrorPopup = nullptr;
        g_PopupHooked = false;
    }
    if (g_SneakHooked)
    {
        if (void* target = ResolveGameAddress(gAddr.FobTarget_RequestSneakCommon))
            DisableAndRemoveHook(target);
        g_OrigRequestSneakCommon = nullptr;
        g_SneakHooked = false;
    }
    g_RefusedAt.store(0, std::memory_order_relaxed);
}
