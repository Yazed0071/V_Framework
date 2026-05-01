#include "pch.h"

#include <Windows.h>
#include <atomic>
#include <cstdint>

#include "HookUtils.h"
#include "log.h"
#include "AddressSet.h"
#include "MbCommonPopupHook.h"


using SetupOnce_t = void(__fastcall*)(void* self, void* popupWindow);

using OpenPopupHelper_t = void(__fastcall*)(
    void* window,
    std::uint64_t closeAction,
    const char* text,
    std::uint64_t popupType,
    std::uint64_t translation,
    unsigned int selectedButton,
    unsigned int disableCancel);


static SetupOnce_t                  g_OrigSetupOnce = nullptr;
static OpenPopupHelper_t            g_OpenPopupHelper = nullptr;
static std::atomic<void*>           g_PopupWindow{ nullptr };


static void __fastcall hkSetupOnce(void* self, void* popupWindow)
{
    if (g_OrigSetupOnce)
    {
        g_OrigSetupOnce(self, popupWindow);
    }

    if (popupWindow)
    {
        void* previous = g_PopupWindow.exchange(popupWindow);
        if (previous != popupWindow)
        {
            Log("[MbCommonPopup] SetupOnce captured popup window=%p (was %p)\n",
                popupWindow, previous);
        }
    }
}


bool Install_MbCommonPopupHook()
{
    void* addrSetupOnce = ResolveGameAddress(gAddr.MbCommonPopupAct_SetupOnce);
    void* addrOpenHelper = ResolveGameAddress(gAddr.MbCommonPopupAct_OpenPopupHelper);

    if (!addrSetupOnce || !addrOpenHelper)
    {
        Log("[MbCommonPopup] Install skipped: setupOnce=%p openHelper=%p\n",
            addrSetupOnce, addrOpenHelper);
        return false;
    }

    g_OpenPopupHelper = reinterpret_cast<OpenPopupHelper_t>(addrOpenHelper);

    const bool ok = CreateAndEnableHook(
        addrSetupOnce,
        reinterpret_cast<void*>(&hkSetupOnce),
        reinterpret_cast<void**>(&g_OrigSetupOnce));

    Log("[MbCommonPopup] Hook SetupOnce: %s target=%p orig=%p\n",
        ok ? "OK" : "FAIL", addrSetupOnce, reinterpret_cast<void*>(g_OrigSetupOnce));

    return ok;
}


bool Uninstall_MbCommonPopupHook()
{
    void* addr = ResolveGameAddress(gAddr.MbCommonPopupAct_SetupOnce);
    if (addr)
    {
        DisableAndRemoveHook(addr);
    }

    g_OrigSetupOnce = nullptr;
    g_OpenPopupHelper = nullptr;
    g_PopupWindow.store(nullptr);
    return true;
}


static bool TryCallOpenPopupHelper(void* window, const char* text, unsigned int popupType)
{
    __try
    {
        g_OpenPopupHelper(
            window,
            0ull,
            text,
            static_cast<std::uint64_t>(popupType),
            0ull,
            0u,
            0u);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}


namespace MbCommonPopup
{
    void* GetCapturedPopupWindow()
    {
        return g_PopupWindow.load();
    }

    bool ShowPopup(const char* text, unsigned int popupType)
    {
        if (!text || !*text)
        {
            Log("[MbCommonPopup] ShowPopup: empty or missing text\n");
            return false;
        }

        if (popupType < 1 || popupType > 6)
        {
            Log("[MbCommonPopup] ShowPopup: popupType %u out of range 1..6, clamped to 1\n", popupType);
            popupType = 1;
        }

        if (!g_OpenPopupHelper)
        {
            Log("[MbCommonPopup] ShowPopup: OpenPopupHelper address not resolved (build unsupported)\n");
            return false;
        }

        void* window = g_PopupWindow.load();
        if (!window)
        {
            Log("[MbCommonPopup] ShowPopup: popup window not captured yet — open the iDroid menu first\n");
            return false;
        }

        if (!TryCallOpenPopupHelper(window, text, popupType))
        {
            Log("[MbCommonPopup] ShowPopup: helper raised access violation — captured window=%p is stale; clearing capture so subsequent calls fail fast until SetupOnce re-fires\n",
                window);
            g_PopupWindow.store(nullptr);
            return false;
        }

        return true;
    }
}
