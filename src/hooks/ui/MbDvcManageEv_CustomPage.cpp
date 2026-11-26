#include <pch.h>
#include "MbDvcManageEv_CustomPage.h"

#include <mutex>

#include "AddressSet.h"
#include "FoxHashes.h"
#include "HookUtils.h"
#include "UiPadLock.h"
#include "../../lua/WindowPages.h"
#include "LuaBroadcaster.h"
#include "MissionCodeGuard.h"
#include "MotherBaseDeviceTopMenuTableImpl_CustomItems.h"
#include "UixUtilityControl.h"
#include "UixUtilityDev.h"
#include "log.h"

namespace mbDvcCustomPage
{
    namespace
    {
        using ManagerUpdate_t       = void(__fastcall*)(void* manager);
        using SelectorUpdate_t      = void(__fastcall*)(void* selector);
        using SendTriggerToParent_t = void(__fastcall*)(void* window, std::uint64_t trigger);
        using FindWindow_t          = void*(__fastcall*)(void** utility, std::uint64_t name);
        using SendTrigger_t         = void(__fastcall*)(void** utility, void* window, std::uint64_t trigger, std::uint64_t param);
        using WindowMessage_t       = void(__fastcall*)(void** utility, void* window);
        using ComponentVisible_t    = void(__fastcall*)(void** utility, void* component, bool visible);

        constexpr std::size_t   kManagerIDroidWindow = 0xC0;    // MbDvcManageEv: the iDroid root window
        constexpr std::size_t   kManagerCommonData   = 0x100;   // MbDvcManageEv: the common data object, its +0xCA0 is the top-menu table
        constexpr std::size_t   kCommonDataTable     = 0xCA0;
        constexpr std::size_t   kCommonDataTopInput  = 0x820;   // a press clears it, only the return from a vanilla page sets it; while 0 the manager skips its tab input
        constexpr std::uint64_t kId48Mask           = 0xFFFFFFFFFFFFull;
        constexpr std::uint64_t kShowMenuTrigger     = 0xBCE1EF6ACF15ull;   // the trigger that reshows a hidden top menu at the same list and cursor
        constexpr ULONGLONG     kStaleMs             = 2000;                // no manager Update for this long: the iDroid is closed
        constexpr ULONGLONG     kPadReleaseMs        = 300;                 // the page gives the pad back this soon after the iDroid stops updating
        constexpr ULONGLONG     kHideGraceMs         = 1500;                // the leave animation takes about 500 ms, past this the list is hidden without it
        constexpr const char*   kPadHoldName         = "V_FrameWork_IDroidPage";

        constexpr std::size_t   kSelectorTable       = 0x1D0;   // MbTopMenuSelectorEv: the top-menu table
        constexpr std::size_t   kSelectorWindow      = 0x1D8;   // MbTopMenuSelectorEv: the window its triggers go out from
        constexpr std::size_t   kSelectorDepth       = 0x206;
        constexpr std::size_t   kSelectorState       = 0x220;
        constexpr std::size_t   kSelectorRequest     = 0x224;   // the state it moves to on its next Update
        constexpr std::size_t   kSelectorChanged     = 0x228;
        constexpr std::size_t   kSelectorPhaseAnim   = 0x2B8;   // the list's running phase animation, the hide waits until it stops
        constexpr std::size_t   kSelectorPhase       = 0x2D0;
        constexpr std::size_t   kSelectorListLayout  = 0x270;   // MbTopMenuSelectorEv: the parent of every row, sub list and help line
        constexpr std::size_t   kAnimationFlags      = 0x38;
        constexpr std::size_t   kTableRebuild        = 0xC38;   // the table asks the selector to rebuild its list
        constexpr std::uint32_t kMenuOpenList        = 2;
        constexpr std::uint32_t kMenuListIdle        = 5;
        constexpr std::uint32_t kMenuHide            = 6;       // leave animation, then hidden (7)

        const std::uint8_t kManagerUpdateEntry[]       = { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9 };
        const std::uint8_t kSelectorUpdateEntry[]      = { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9 };
        const std::uint8_t kSendTriggerToParentEntry[] = { 0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xDA, 0x48, 0x8B, 0xF9 };

        struct State
        {
            void*         manager = nullptr;
            void*         iDroidWindow = nullptr;
            void*         selectorWindow = nullptr;   // the window the top menu sends its triggers from, seen when one of our items is pressed
            ULONGLONG     lastUpdate = 0;
            std::uint32_t session = 0;                // counts iDroid openings, a pointer alone can repeat
            bool          selectorHeld = false;       // our press hid the top menu and it has not been given back
            std::uint32_t heldSession = 0;
            ULONGLONG     hideAskedAt = 0;            // when the menu was last asked to hide, its leave animation started then
            std::uint64_t openPage = 0;
            void*         openWindow = nullptr;
            std::uint32_t openHandle = 0;
            std::uint32_t openSession = 0;
            bool          padHeld = false;
        };

        std::mutex g_Lock;
        State      g_State;

        ManagerUpdate_t       g_OrigManagerUpdate = nullptr;
        SelectorUpdate_t      g_OrigSelectorUpdate = nullptr;
        SendTriggerToParent_t g_OrigSendTriggerToParent = nullptr;
        void*                 g_ManagerUpdateTarget = nullptr;
        void*                 g_SelectorUpdateTarget = nullptr;
        void*                 g_SendTriggerToParentTarget = nullptr;

        bool FreshLocked(ULONGLONG now)
        {
            return g_State.manager && now - g_State.lastUpdate <= kStaleMs;
        }

        bool FreshIDroid(void*& window, std::uint32_t& session)
        {
            std::lock_guard<std::mutex> lock(g_Lock);
            if (!FreshLocked(GetTickCount64()) || !g_State.iDroidWindow)
                return false;
            window = g_State.iDroidWindow;
            session = g_State.session;
            return true;
        }

        bool SessionIs(std::uint32_t session)
        {
            std::lock_guard<std::mutex> lock(g_Lock);
            return FreshLocked(GetTickCount64()) && g_State.session == session;
        }

        void* ReadPointer(const void* base, std::size_t offset)
        {
            void* value = nullptr;
            return base && ReadEntryBytesSEH(static_cast<const std::uint8_t*>(base) + offset, &value, sizeof(value)) ? value : nullptr;
        }

        void NoteManager(void* manager)
        {
            void* window = ReadPointer(manager, kManagerIDroidWindow);
            void* common = ReadPointer(manager, kManagerCommonData);
            const ULONGLONG now = GetTickCount64();
            {
                std::lock_guard<std::mutex> lock(g_Lock);
                if (g_State.manager != manager || g_State.iDroidWindow != window || now - g_State.lastUpdate > kStaleMs)
                {
                    ++g_State.session;
                    g_State.selectorWindow = nullptr;
                    g_State.selectorHeld = false;
                    g_State.openPage = 0;
                }
                g_State.manager = manager;
                g_State.iDroidWindow = window;
                g_State.lastUpdate = now;
            }
            if (void* table = ReadPointer(common, kCommonDataTable))
                mbTopMenuCustomItems::NoteTable(table);   // a table made before the factory hook went in
        }

        bool ButtonPressSEH(std::uint64_t name)
        {
            __try
            {
                void* fn = UixUtilityControl::Method(UixUtilityControl::dev::IsButtonPressByName);
                return fn && reinterpret_cast<bool(__fastcall*)(void**, std::uint64_t)>(fn)(UixUtilityControl::Utility(), name);
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }

        bool BackPressed()
        {
            const std::uint64_t name = FoxHashes::StrCode64("UI_CANCEL") & kId48Mask;   // the game also takes a mouse button for this exact 48-bit name
            bool on = false;
            if (uipad::HoldsOwnDisableRequest())
                return uipad::Press(static_cast<std::uint32_t>(name), on) && on;   // the page holds the pad: read past its own lock
            uipad::OwnQuery own;
            return ButtonPressSEH(name);
        }

        void HoldPad(bool hold)   // the page owns the pad while open, like the outfit palette: the iDroid menu reads nothing
        {
            {
                std::lock_guard<std::mutex> lock(g_Lock);
                if (g_State.padHeld == hold)
                    return;
                g_State.padHeld = hold;
            }
            const std::uint32_t id = static_cast<std::uint32_t>(FoxHashes::StrCode64(kPadHoldName));
            void* set = UixUtilityControl::Method(hold ? UixUtilityControl::dev::SetDisablePad : UixUtilityControl::dev::ResetDisablePad);
            void* ask = UixUtilityControl::Method(UixUtilityControl::dev::IsDisablePad);
            bool held = false;
            auto call = [&]() {
                reinterpret_cast<void(__fastcall*)(void**, std::uint32_t)>(set)(UixUtilityControl::Utility(), id);
                held = reinterpret_cast<bool(__fastcall*)(void**, std::uint32_t)>(ask)(UixUtilityControl::Utility(), id);
            };
            if (set && ask)
                UixUtilityControl::Guarded(call);
            uipad::NoteOwnDisableRequest(id, held);
        }

        void CheckBack()   // once per iDroid frame: Back asks the page's OnBack, else closes the page
        {
            std::uint64_t page = 0;
            void* window = nullptr;
            {
                std::lock_guard<std::mutex> lock(g_Lock);
                if (!g_State.openPage || g_State.openSession != g_State.session)
                    return;
                page = g_State.openPage;
                window = g_State.openWindow;
            }
            if (!BackPressed())
                return;
            if (WindowPages::HasPage(page))
                WindowPages::Note(page, window, WindowPages::Event::Back);
            else
                ClosePage(page);
        }

        void __fastcall hkManagerUpdate(void* manager)
        {
            NoteManager(manager);
            CheckBack();
            g_OrigManagerUpdate(manager);
        }

        void __fastcall hkSendTriggerToParent(void* window, std::uint64_t trigger)
        {
            std::uint8_t itemId = 0;
            if (mbTopMenuCustomItems::FindItemByTrigger(trigger & kId48Mask, itemId) && mbTopMenuCustomItems::ItemsAllowed())
            {
                {
                    std::lock_guard<std::mutex> lock(g_Lock);
                    g_State.selectorWindow = window;
                    g_State.selectorHeld = true;
                    g_State.heldSession = g_State.session;
                    g_State.hideAskedAt = GetTickCount64();
                }
                V_FrameWork::QueueMessage("UI", "IDroidMenuItemSelected", static_cast<int>(itemId));
                return;   // the manager would drop an unknown trigger anyway
            }
            g_OrigSendTriggerToParent(window, trigger);
        }

        struct MenuState
        {
            void*         window = nullptr;
            std::uint32_t state = 0;
            std::uint32_t request = 0;
            std::uint8_t  changed = 0;
            std::uint8_t  rebuild = 0;
            std::uint8_t  depth = 0;
            std::uint8_t  phase = 0;
            void*         phaseAnim = nullptr;
            std::uint32_t animFlags = 0;
        };

        bool ReadMenuState(void* selector, MenuState& out)
        {
            const auto* base = static_cast<const std::uint8_t*>(selector);
            void* table = ReadPointer(selector, kSelectorTable);
            out.window = ReadPointer(selector, kSelectorWindow);
            out.phaseAnim = ReadPointer(selector, kSelectorPhaseAnim);
            return out.window
                && ReadEntryBytesSEH(base + kSelectorState, &out.state, sizeof(out.state))
                && ReadEntryBytesSEH(base + kSelectorRequest, &out.request, sizeof(out.request))
                && ReadEntryBytesSEH(base + kSelectorChanged, &out.changed, sizeof(out.changed))
                && ReadEntryBytesSEH(base + kSelectorDepth, &out.depth, sizeof(out.depth))
                && ReadEntryBytesSEH(base + kSelectorPhase, &out.phase, sizeof(out.phase))
                && (!out.phaseAnim || ReadEntryBytesSEH(static_cast<const std::uint8_t*>(out.phaseAnim) + kAnimationFlags, &out.animFlags, sizeof(out.animFlags)))
                && (!table || ReadEntryBytesSEH(static_cast<const std::uint8_t*>(table) + kTableRebuild, &out.rebuild, sizeof(out.rebuild)));
        }

        bool WriteDwordSEH(void* at, std::uint32_t value)
        {
            __try
            {
                *static_cast<volatile std::uint32_t*>(at) = value;
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }

        bool WriteByteSEH(void* at, std::uint8_t value)
        {
            __try
            {
                *static_cast<volatile std::uint8_t*>(at) = value;
                return true;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        }

        void KeepMenuHidden(void* selector)   // our page is open: the menu our press hid stays hidden, whatever asks it back
        {
            void* pressedFrom = nullptr;
            ULONGLONG hideAskedAt = 0;
            const ULONGLONG now = GetTickCount64();
            {
                std::lock_guard<std::mutex> lock(g_Lock);
                const bool pageIsOpen = g_State.openPage && g_State.openSession == g_State.session;
                if (!g_State.selectorHeld || g_State.heldSession != g_State.session || !pageIsOpen || !FreshLocked(now))
                    return;
                pressedFrom = g_State.selectorWindow;
                hideAskedAt = g_State.hideAskedAt;
            }
            MenuState menu;
            if (!ReadMenuState(selector, menu) || menu.window != pressedFrom)
                return;

            // the map tab's trigger 0x2D782E561A73 must not end this animation: it left every tab's list off screen
            if (menu.state == kMenuHide)
            {
                if (menu.changed || !menu.phaseAnim || now - hideAskedAt < kHideGraceMs)
                    return;
                {
                    std::lock_guard<std::mutex> lock(g_Lock);
                    g_State.hideAskedAt = now;
                }
                LogDebug("iDroid page: the menu's leave animation is still running (flags 0x%X, phase %u), hiding its list", menu.animFlags, menu.phase);
                void* list = ReadPointer(selector, kSelectorListLayout);
                auto hide = [&]()   // what the hide does when the animation ends, the reshow undoes it
                {
                    reinterpret_cast<ComponentVisible_t>(UixUtilityControl::Method(UixUtilityControl::dev::SetVisible + sizeof(void*)))(UixUtilityControl::Utility(), list, false);
                };
                if (list)
                    UixUtilityControl::Guarded(hide);
                return;
            }

            if (menu.state < kMenuOpenList || menu.state > kMenuListIdle || menu.request == kMenuHide)
                return;
            LogDebug("iDroid page: the menu is back in state %u (request %u) with the page open, hiding it again", menu.state, menu.request);
            if (WriteDwordSEH(static_cast<std::uint8_t*>(selector) + kSelectorRequest, kMenuHide))
            {
                std::lock_guard<std::mutex> lock(g_Lock);
                g_State.hideAskedAt = now;
            }
        }

#ifdef _DEBUG
        void ProbeMenu(void* selector, int phase)   // TEMP probe: what brings the menu back over our page
        {
            static const char* const kWhen[2] = { "before", "after" };
            static MenuState seen[2];
            void* pressedFrom = nullptr;
            bool held = false;
            bool pageOpen = false;
            {
                std::lock_guard<std::mutex> lock(g_Lock);
                pressedFrom = g_State.selectorWindow;
                held = g_State.selectorHeld;
                pageOpen = g_State.openPage && g_State.openSession == g_State.session;
            }
            MenuState menu;
            if (!pressedFrom || !ReadMenuState(selector, menu) || menu.window != pressedFrom)
                return;
            MenuState& last = seen[phase];
            if (menu.state == last.state && menu.request == last.request && menu.changed == last.changed && menu.rebuild == last.rebuild
                && menu.depth == last.depth && menu.phase == last.phase && menu.phaseAnim == last.phaseAnim && menu.animFlags == last.animFlags)
                return;
            last = menu;
            LogDebug("iDroid page: menu %s Update: state %u, request %u, changed %u, rebuild %u, depth %u, phase %u, anim %p flags 0x%X, held %u, page open %u",
                kWhen[phase], menu.state, menu.request, menu.changed, menu.rebuild, menu.depth, menu.phase, menu.phaseAnim, menu.animFlags,
                held ? 1u : 0u, pageOpen ? 1u : 0u);
        }
#endif

        void __fastcall hkSelectorUpdate(void* selector)
        {
#ifdef _DEBUG
            ProbeMenu(selector, 0);
#endif
            KeepMenuHidden(selector);
            g_OrigSelectorUpdate(selector);
#ifdef _DEBUG
            ProbeMenu(selector, 1);
#endif
        }

        void* FindWindowNamed(std::uint64_t name)
        {
            void* found = nullptr;
            auto find = [&]()
            {
                found = reinterpret_cast<FindWindow_t>(UixUtilityControl::Method(UixUtilityControl::dev::FindWindow))(UixUtilityControl::Utility(), name);
            };
            UixUtilityControl::Guarded(find);
            return found;
        }

        void QueueWindowMessage(std::uint32_t windowHandle, std::size_t devOffset)
        {
            UixUtilityControl::Queue(windowHandle, [devOffset](const UixUtilityControl::Object& page)
            {
                reinterpret_cast<WindowMessage_t>(UixUtilityControl::Method(devOffset))(UixUtilityControl::Utility(), page.ptr);
            });
        }
    }

    bool OpenPage(std::uint64_t windowName)
    {
        using namespace UixUtilityControl;

        windowName &= kId48Mask;
        if (!windowName || !IsSupportedOnThisBuild() || !mbTopMenuCustomItems::ItemsAllowed())
            return false;

        void* iDroid = nullptr;
        std::uint32_t session = 0;
        if (!FreshIDroid(iDroid, session))
        {
            LogWarn("V_UixUtility.OpenIDroidPage: the iDroid is not open");
            return false;
        }

        void* page = FindWindowNamed(windowName);
        const std::uint32_t pageHandle = HandleFor(Kind::Window, page, page);
        Object pageObject;
        if (!pageHandle || !Lookup(pageHandle, pageObject))
        {
            LogWarn("V_UixUtility.OpenIDroidPage: no window with that name, make it with V_UixUtility.CreateWindow first");
            return false;
        }

        // not a child of the iDroid window: the iDroid shows its whole tree on its own page changes, which re-showed
        // the hidden top menu and a closed page; Tick hides the page when the iDroid closes instead
        QueueWindowMessage(pageHandle, dev::PostShowAndStartMessage);

        {
            std::lock_guard<std::mutex> lock(g_Lock);
            g_State.openPage = windowName;
            g_State.openWindow = pageObject.ptr;
            g_State.openHandle = pageHandle;
            g_State.openSession = session;
        }
        HoldPad(true);
        return true;
    }

    bool ClosePage(std::uint64_t windowName)
    {
        using namespace UixUtilityControl;

        windowName &= kId48Mask;
        void* selector = nullptr;
        void* manager = nullptr;
        std::uint32_t session = 0;
        bool giveMenuBack = false;
        {
            std::lock_guard<std::mutex> lock(g_Lock);
            const bool fresh = FreshLocked(GetTickCount64());
            manager = g_State.manager;
            const bool pageIsOpen = windowName && g_State.openPage == windowName && g_State.openSession == g_State.session && fresh;
            if (windowName && !pageIsOpen)
                return false;
            session = g_State.session;
            if (g_State.selectorHeld && g_State.heldSession == session && fresh && g_State.selectorWindow)
            {
                giveMenuBack = true;
                selector = g_State.selectorWindow;
            }
            g_State.openPage = 0;
            g_State.openHandle = 0;
            g_State.selectorHeld = false;
        }
        HoldPad(false);

        if (windowName)
        {
            void* page = FindWindowNamed(windowName);
            if (const std::uint32_t pageHandle = HandleFor(Kind::Window, page, page))
                QueueWindowMessage(pageHandle, dev::PostHideAndStopMessage);
        }

        if (giveMenuBack)
        {
            if (auto* common = static_cast<std::uint8_t*>(ReadPointer(manager, kManagerCommonData)))
                WriteByteSEH(common + kCommonDataTopInput, 1);
            QueueNoHandle([selector, session]()
            {
                if (!SessionIs(session))
                    return;
                reinterpret_cast<SendTrigger_t>(Method(dev::SendTrigger))(Utility(), selector, kShowMenuTrigger, 0);
            });
        }
        return windowName != 0 || giveMenuBack;
    }

    void Tick()   // every UI frame: the iDroid closed with a page open hides the page and gives the pad back
    {
        std::uint32_t pageHandle = 0;
        {
            std::lock_guard<std::mutex> lock(g_Lock);
            if (!g_State.padHeld || (g_State.openPage && g_State.openSession == g_State.session && GetTickCount64() - g_State.lastUpdate <= kPadReleaseMs))
                return;
            pageHandle = g_State.openHandle;
            g_State.openPage = 0;
            g_State.openHandle = 0;
            g_State.selectorHeld = false;
        }
        if (pageHandle)
            QueueWindowMessage(pageHandle, UixUtilityControl::dev::PostHideAndStopMessage);
        HoldPad(false);
    }

    bool IsPageOpen(std::uint64_t windowName)
    {
        windowName &= kId48Mask;
        std::lock_guard<std::mutex> lock(g_Lock);
        return windowName && FreshLocked(GetTickCount64()) && g_State.openPage == windowName && g_State.openSession == g_State.session;
    }

    bool Install(HMODULE)
    {
        if (!gAddr.MbDvcManageEv_Update || !gAddr.tpp_ui_utility_SendTriggerToParent)
        {
            LogError("iDroid page: no address for this build, iDroid pages and custom item presses are off");
            return true;
        }

        bool ok = true;

        g_ManagerUpdateTarget = ResolveGameAddress(gAddr.MbDvcManageEv_Update);
        HookEntry entry = CheckHookEntry(g_ManagerUpdateTarget, kManagerUpdateEntry, sizeof(kManagerUpdateEntry));
        if (entry == HookEntry::Unknown)
        {
            LogError("iDroid page: MbDvcManageEv::Update is not the expected code, iDroid pages are off");
            g_ManagerUpdateTarget = nullptr;
            ok = false;
        }
        else
        {
            if (entry == HookEntry::ForeignHook)
                LogHookedOnTop("iDroid manager Update", g_ManagerUpdateTarget);
            if (!CreateAndEnableHook(g_ManagerUpdateTarget, &hkManagerUpdate, reinterpret_cast<void**>(&g_OrigManagerUpdate)))
            {
                g_ManagerUpdateTarget = nullptr;
                ok = false;
            }
        }

        g_SendTriggerToParentTarget = ResolveGameAddress(gAddr.tpp_ui_utility_SendTriggerToParent);
        entry = CheckHookEntry(g_SendTriggerToParentTarget, kSendTriggerToParentEntry, sizeof(kSendTriggerToParentEntry));
        if (entry == HookEntry::Unknown)
        {
            LogError("iDroid page: SendTriggerToParent is not the expected code, custom iDroid item presses are off");
            g_SendTriggerToParentTarget = nullptr;
            ok = false;
        }
        else
        {
            if (entry == HookEntry::ForeignHook)
                LogHookedOnTop("SendTriggerToParent", g_SendTriggerToParentTarget);
            if (!CreateAndEnableHook(g_SendTriggerToParentTarget, &hkSendTriggerToParent, reinterpret_cast<void**>(&g_OrigSendTriggerToParent)))
            {
                g_SendTriggerToParentTarget = nullptr;
                ok = false;
            }
        }

        if (!gAddr.MbTopMenuSelectorEv_Update)
        {
            LogError("iDroid page: no address for the top menu Update on this build, the menu can come back over an open page");
            return ok;
        }
        g_SelectorUpdateTarget = ResolveGameAddress(gAddr.MbTopMenuSelectorEv_Update);
        entry = CheckHookEntry(g_SelectorUpdateTarget, kSelectorUpdateEntry, sizeof(kSelectorUpdateEntry));
        if (entry == HookEntry::Unknown)
        {
            LogError("iDroid page: the top menu Update is not the expected code, the menu can come back over an open page");
            g_SelectorUpdateTarget = nullptr;
            ok = false;
        }
        else
        {
            if (entry == HookEntry::ForeignHook)
                LogHookedOnTop("iDroid top menu Update", g_SelectorUpdateTarget);
            if (!CreateAndEnableHook(g_SelectorUpdateTarget, &hkSelectorUpdate, reinterpret_cast<void**>(&g_OrigSelectorUpdate)))
            {
                g_SelectorUpdateTarget = nullptr;
                ok = false;
            }
        }
        return ok;
    }

    void Uninstall()
    {
        if (g_SelectorUpdateTarget)
        {
            DisableAndRemoveHook(g_SelectorUpdateTarget);
            g_SelectorUpdateTarget = nullptr;
        }
        if (g_SendTriggerToParentTarget)
        {
            DisableAndRemoveHook(g_SendTriggerToParentTarget);
            g_SendTriggerToParentTarget = nullptr;
        }
        if (g_ManagerUpdateTarget)
        {
            DisableAndRemoveHook(g_ManagerUpdateTarget);
            g_ManagerUpdateTarget = nullptr;
        }
    }
}
