#include "pch.h"
#include "CallMenuManagerImpl_UpdateInShowing.h"

#include <Windows.h>
#include <cstddef>
#include <cstdint>

#include "AddressSet.h"
#include "CallMenuCommandSet_UpdateState.h"
#include "HookUtils.h"
#include "log.h"
#include "MissionCodeGuard.h"
#include "../ui/CallMenuImpl_UpdateOption.h"

namespace
{
    using UpdateInShowing_t = void(__fastcall*)(std::uint8_t* callMenuManager);
    using GetHeldButtons_t  = std::uint32_t(__fastcall*)(void* pad);
    using GetCallMenuCommand_t = std::uint8_t(__fastcall*)(void* uiController);

    constexpr std::size_t   kManagerPlayerSystems     = 0x38;
    constexpr std::size_t   kPlayerSystemsPad         = 0xB0;
    constexpr std::size_t   kManagerUiController      = 0x48;
    constexpr std::size_t   kManagerMenuShown         = 0x55;
    constexpr std::size_t   kHeldButtonsIgnoreMaskVtblIndex = 0x28 / 8; // the menu masks the pad while it is open
    constexpr std::size_t   kGetCallMenuCommandVtblIndex    = 0x128 / 8;
    constexpr std::uint8_t  kFirstPickedCommand       = 2;               // 0 is a cancel, 1 is nothing picked
    constexpr std::uint32_t kNextPageButton           = 1u << 6;         // PlayerPad.RELOAD
    constexpr std::uint32_t kPadPreviousPageButton    = 1u << 18;        // PlayerPad.EVADE
    constexpr std::uint32_t kKeyboardPreviousPageButton = 1u << 5;       // PlayerPad.ACTION; EVADE is Space there, which also picks the option
    constexpr int           kFirstPage                = 1;
    constexpr std::uint64_t kQuickReshowMs            = 250;   // a hold-up starting or ending hides the menu and shows it again while the button stays held

    UpdateInShowing_t g_Orig              = nullptr;
    void*             g_HookTarget        = nullptr;
    int               g_Page              = kFirstPage;
    std::uint32_t     g_PageButtonsHeld   = 0;
    bool              g_MenuWasOpen       = false;
    std::uint64_t     g_LastShownTick     = 0;

    bool ShownMomentsAgo()
    {
        return GetTickCount64() - g_LastShownTick <= kQuickReshowMs;
    }

    template <typename T>
    T& At(std::uint8_t* base, std::size_t offset)
    {
        return *reinterpret_cast<T*>(base + offset);
    }

    std::uint32_t HeldPageButtons(std::uint8_t* callMenuManager)
    {
        std::uint8_t* playerSystems = At<std::uint8_t*>(callMenuManager, kManagerPlayerSystems);
        void* pad = At<void*>(playerSystems, kPlayerSystemsPad);
        auto getHeld = (*reinterpret_cast<GetHeldButtons_t**>(pad))[kHeldButtonsIgnoreMaskVtblIndex];
        return getHeld(pad) & (kNextPageButton | kPadPreviousPageButton | kKeyboardPreviousPageButton);
    }

    void UpdatePage(std::uint8_t* callMenuManager)
    {
        const std::uint32_t held = HeldPageButtons(callMenuManager);
        const std::uint32_t pressed = held & ~g_PageButtonsHeld;
        g_PageButtonsHeld = held;

        const bool menuOpen = At<std::uint8_t>(callMenuManager, kManagerMenuShown) != 0;
        const bool justOpened = menuOpen && !g_MenuWasOpen && !ShownMomentsAgo();
        g_MenuWasOpen = menuOpen;
        if (!menuOpen)
            return;   // still closing: the picked row runs from the page it was picked on
        g_LastShownTick = GetTickCount64();

        const int pageCount = CallMenuItems::PageCount();
        if (justOpened || pageCount <= kFirstPage || g_Page > pageCount)
        {
            g_Page = kFirstPage;
            return;
        }

        if (pressed & kNextPageButton)
            g_Page = (g_Page % pageCount) + 1;
        else if (pressed & (CallMenuGuide::IsUsingKeyboard() ? kKeyboardPreviousPageButton : kPadPreviousPageButton))
            g_Page = (g_Page + pageCount - 2) % pageCount + 1;
    }

    void __fastcall hk_UpdateInShowing(std::uint8_t* callMenuManager)
    {
        if (!MissionCodeGuard::ShouldBypassHooks())
            UpdatePage(callMenuManager);
        else
            g_Page = kFirstPage;

        g_Orig(callMenuManager);
    }

    bool PickWaitingToRun(std::uint8_t* callMenuManager)
    {
        void* ui = At<void*>(callMenuManager, kManagerUiController);
        if (!ui)
            return false;
        auto getCommand = (*reinterpret_cast<GetCallMenuCommand_t**>(ui))[kGetCallMenuCommandVtblIndex];
        return getCommand(ui) >= kFirstPickedCommand;
    }
}

int CallMenuPaging::CurrentPage()
{
    return g_Page;
}

void CallMenuPaging::OpenOnFirstPage(std::uint8_t* callMenuManager)
{
    if (g_Page == kFirstPage || !g_Orig || PickWaitingToRun(callMenuManager) || ShownMomentsAgo())
        return;

    g_Page = kFirstPage;
    g_Orig(callMenuManager);   // the HUD takes the columns as they are when it shows; a frame later the last page's icons stay up
}

bool Install_CallMenuManagerImpl_UpdateInShowing_Hook()
{
    if (g_HookTarget)
        return true;

    if (!gAddr.CallMenuManagerImpl_UpdateInShowing)
    {
        LogError("CallMenu: UpdateInShowing address missing for %s, call menu has one page",
                 GetGameBuildName(gGameBuild));
        return true;
    }

    void* target = ResolveGameAddress(gAddr.CallMenuManagerImpl_UpdateInShowing);
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hk_UpdateInShowing),
                             reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("CallMenu: UpdateInShowing hook failed at %p, call menu has one page", target);
        return false;
    }

    g_HookTarget = target;
    return true;
}

void Uninstall_CallMenuManagerImpl_UpdateInShowing_Hook()
{
    if (g_HookTarget)
    {
        DisableAndRemoveHook(g_HookTarget);
        g_HookTarget = nullptr;
        g_Orig       = nullptr;
    }
    g_Page              = kFirstPage;
    g_PageButtonsHeld   = 0;
    g_MenuWasOpen       = false;
    g_LastShownTick     = 0;
}
