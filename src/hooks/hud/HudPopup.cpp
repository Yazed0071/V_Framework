#include "pch.h"

#include <Windows.h>
#include <cstdint>
#include <cstring>

#include "AddressSet.h"
#include "FoxHashes.h"
#include "HookUtils.h"
#include "HudPopup.h"
#include "MbDvcCustomPopupHook.h"
#include "log.h"


namespace
{
    using GetInstance_t        = void* (__fastcall*)();
    using SetPopupType_t       = void (__fastcall*)(
        void* mgr, std::int32_t popupType,
        std::uint64_t stringIdHash, std::uint8_t isError);
    using SetPopupText_t       = void (__fastcall*)(
        void* mgr, const char* text);
    using SetPopupErrorType_t  = void (__fastcall*)(
        void* mgr, std::int32_t popupType,
        std::uint32_t errorParam, std::uint8_t isError);
    using StartPopup_t         = std::uint8_t (__fastcall*)(void* mgr);


    // popupType buttons: 1=none, 3=OK, 4=Cancel, 5/6=OK+Cancel.
    // 0 / 2 / >=7 are bad — fold to 1 (no-buttons popup).
    static std::int32_t ClampPopupType(std::uint32_t popupType)
    {
        if (popupType == 0 || popupType == 2 || popupType >= 7) return 1;
        return static_cast<std::int32_t>(popupType);
    }


    static GetInstance_t ResolveGetInstance()
    {
        const auto a = gAddr.HudCommonDataManager_GetInstance;
        if (!a) return nullptr;
        return reinterpret_cast<GetInstance_t>(ResolveGameAddress(a));
    }

    static SetPopupType_t ResolveSetPopupType()
    {
        const auto a = gAddr.HudCommonDataManager_SetPopupType;
        if (!a) return nullptr;
        return reinterpret_cast<SetPopupType_t>(ResolveGameAddress(a));
    }

    static SetPopupText_t ResolveSetPopupText()
    {
        const auto a = gAddr.HudCommonDataManager_SetPopupText;
        if (!a) return nullptr;
        return reinterpret_cast<SetPopupText_t>(ResolveGameAddress(a));
    }

    static SetPopupErrorType_t ResolveSetPopupErrorType()
    {
        const auto a = gAddr.HudCommonDataManager_SetPopupErrorType;
        if (!a) return nullptr;
        return reinterpret_cast<SetPopupErrorType_t>(ResolveGameAddress(a));
    }

    static StartPopup_t ResolveStartPopup()
    {
        const auto a = gAddr.HudCommonDataManager_StartPopup;
        if (!a) return nullptr;
        return reinterpret_cast<StartPopup_t>(ResolveGameAddress(a));
    }


    static void* SafeGetCommonDataManager()
    {
        auto fn = ResolveGetInstance();
        if (!fn) return nullptr;

        __try { return fn(); }
        __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    }


    static bool SafeCallSetPopupType(void* mgr,
                                     std::int32_t popupType,
                                     std::uint64_t titleHash)
    {
        auto fn = ResolveSetPopupType();
        if (!fn || !mgr) return false;

        __try
        {
            fn(mgr, popupType, titleHash, 0);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }


    static bool SafeCallSetPopupText(void* mgr, const char* text)
    {
        auto fn = ResolveSetPopupText();
        if (!fn || !mgr) return false;

        __try
        {
            fn(mgr, text ? text : "");
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }


    static bool SafeCallSetPopupErrorType(void* mgr,
                                          std::int32_t popupType,
                                          std::uint32_t errorParam)
    {
        auto fn = ResolveSetPopupErrorType();
        if (!fn || !mgr) return false;

        __try
        {
            fn(mgr, popupType, errorParam, 0);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }


    static bool SafeCallStartPopup(void* mgr)
    {
        auto fn = ResolveStartPopup();
        if (!fn || !mgr) return false;

        __try { return fn(mgr) != 0; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }


    // Internal: drive title StringId + body text into popup state.
    static bool DriveHudPopup(void*         mgr,
                              std::int32_t  popupType,
                              std::uint64_t titleHash,
                              const char*   bodyText)
    {
        // Body first — popup state does not clear +0x1508/0x1558.
        if (bodyText && *bodyText)
        {
            if (!SafeCallSetPopupText(mgr, bodyText))
            {
                Log("[HudPopup] DriveHudPopup: SetPopupText raised\n");
                return false;
            }
        }

        if (!SafeCallSetPopupType(mgr, popupType, titleHash))
        {
            Log("[HudPopup] DriveHudPopup: SetPopupType raised\n");
            return false;
        }

        return SafeCallStartPopup(mgr);
    }
}  // namespace


bool Show_HudPopup(const char* titleLabel,
                   const char* body,
                   std::uint32_t popupType)
{
    void* mgr = SafeGetCommonDataManager();
    if (!mgr)
    {
        Log("[HudPopup] Show: CommonDataManager not available\n");
        return false;
    }

    const std::uint64_t titleHash =
        (titleLabel && *titleLabel) ? FoxHashes::StrCode64(titleLabel) : 0;

    const std::int32_t  type     = ClampPopupType(popupType);
    const bool          started  = DriveHudPopup(mgr, type, titleHash,
                                                 body ? body : "");

    Log("[HudPopup] Show: title=\"%s\" body=\"%.40s%s\" type=%d started=%d\n",
        titleLabel ? titleLabel : "",
        body ? body : "",
        (body && std::strlen(body) > 40) ? "..." : "",
        type, started ? 1 : 0);
    return started;
}


bool Show_HudPopupLangId(const char* titleLabel,
                         const char* bodyLabel,
                         std::uint32_t popupType)
{
    void* mgr = SafeGetCommonDataManager();
    if (!mgr)
    {
        Log("[HudPopup] ShowLangId: CommonDataManager not available\n");
        return false;
    }

    const std::uint64_t titleHash =
        (titleLabel && *titleLabel) ? FoxHashes::StrCode64(titleLabel) : 0;

    // Resolve body label via cached lang manager (shared with iDroid hook).
    const char* bodyText = "";
    if (bodyLabel && *bodyLabel)
    {
        const std::uint64_t bodyHash = FoxHashes::StrCode64(bodyLabel);
        const char* resolved = MbDvcCustom_TryResolveLangText(bodyHash);
        if (resolved && *resolved)
        {
            bodyText = resolved;
        }
        else
        {
            Log("[HudPopup] ShowLangId: body lang resolve failed (label=\"%s\")\n",
                bodyLabel);
        }
    }

    const std::int32_t type    = ClampPopupType(popupType);
    const bool         started = DriveHudPopup(mgr, type, titleHash, bodyText);

    Log("[HudPopup] ShowLangId: title=\"%s\" body=\"%s\" type=%d started=%d\n",
        titleLabel ? titleLabel : "",
        bodyLabel  ? bodyLabel  : "",
        type, started ? 1 : 0);
    return started;
}


bool Show_HudErrorPopup(std::uint32_t errorParam, std::uint32_t popupType)
{
    void* mgr = SafeGetCommonDataManager();
    if (!mgr)
    {
        Log("[HudPopup] ShowError: CommonDataManager not available\n");
        return false;
    }

    const std::int32_t type = ClampPopupType(popupType);

    if (!SafeCallSetPopupErrorType(mgr, type, errorParam))
    {
        Log("[HudPopup] ShowError: SetPopupErrorType raised\n");
        return false;
    }

    const bool started = SafeCallStartPopup(mgr);
    Log("[HudPopup] ShowError: errorParam=%u type=%d started=%d\n",
        errorParam, type, started ? 1 : 0);
    return started;
}
