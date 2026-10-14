#include "pch.h"
#include "CallMenuImpl_UpdateCallMenu.h"

#include <Windows.h>
#include <cstddef>
#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "log.h"

namespace
{
    struct Addrs
    {
        std::uintptr_t updateCallMenu;
        std::uintptr_t setCommandText;
    };

    Addrs AddrsForBuild()
    {
        switch (gGameBuild)
        {
        case ::AddressSetRuntime::GameBuild::En_1_0_15_4:
        case ::AddressSetRuntime::GameBuild::En_1_0_15_4a:
            return {
                0x14179BB50ull, // tpp::ui::hud::impl::CallMenuImpl::UpdateCallMenu
                0x14179B340ull, // tpp::ui::hud::impl::CallMenuImpl::SetCommandText
            };
        case ::AddressSetRuntime::GameBuild::Jp_1_0_15_4:
        case ::AddressSetRuntime::GameBuild::Jp_1_0_15_4a:
            return {
                0x14179BA40ull, // tpp::ui::hud::impl::CallMenuImpl::UpdateCallMenu
                0x14179B230ull, // tpp::ui::hud::impl::CallMenuImpl::SetCommandText
            };
        case ::AddressSetRuntime::GameBuild::En_1_0_15_3:
            return {
                0x14A903400ull, // tpp::ui::hud::impl::CallMenuImpl::UpdateCallMenu
                0x14179BC90ull, // tpp::ui::hud::impl::CallMenuImpl::SetCommandText
            };
        case ::AddressSetRuntime::GameBuild::Jp_1_0_15_3:
            return {
                0x14B2846E0ull, // tpp::ui::hud::impl::CallMenuImpl::UpdateCallMenu
                0x14179BDC0ull, // tpp::ui::hud::impl::CallMenuImpl::SetCommandText
            };
        default:
            return {};
        }
    }

    using UpdateCallMenu_t = void(__fastcall*)(std::uint8_t* callMenu);
    using SetCommandText_t = void(__fastcall*)(std::uint8_t* callMenu, std::uint8_t* row, std::uint8_t* label);

    constexpr std::size_t  kRows        = 0x270;   // the 10 rows the HUD draws
    constexpr std::size_t  kRowSize     = 0xD8;
    constexpr int          kRowCount    = 10;
    constexpr std::size_t  kRowNumber   = 0x02;    // what the wheel and the number keys select by, drawn as "%d:" on keyboard
    constexpr std::size_t  kRowCommand  = 0x08;
    constexpr std::size_t  kCommandShown = 0x11;
    constexpr std::size_t  kNumberCount = 0x10D;   // shown rows + 1; the wheel steps through 1 .. this - 1
    constexpr std::uint8_t kNoNumber    = 0xFF;    // never reached by the wheel or a number key

    Addrs            g_Addrs{};
    UpdateCallMenu_t g_Orig         = nullptr;
    SetCommandText_t g_SetText      = nullptr;
    void*            g_HookTarget   = nullptr;

    template <typename T>
    T& At(std::uint8_t* base, std::size_t offset)
    {
        return *reinterpret_cast<T*>(base + offset);
    }

    // The game numbers a row only when its command changes, so a hidden row keeps its old number. Once our rows
    // renumber the shown ones, two rows can share a number; the wheel then stops on the hidden one forever.
    void RenumberRowsSeh(std::uint8_t* callMenu)
    {
        __try
        {
            std::uint8_t next = 1;
            for (int i = 0; i < kRowCount; ++i)
            {
                std::uint8_t* row = callMenu + kRows + i * kRowSize;
                std::uint8_t* command = At<std::uint8_t*>(row, kRowCommand);
                const bool shown = command && At<std::uint8_t>(command, kCommandShown) != 0;
                const std::uint8_t number = shown ? next++ : kNoNumber;
                if (At<std::uint8_t>(row, kRowNumber) == number)
                    continue;
                At<std::uint8_t>(row, kRowNumber) = number;
                if (shown)
                    g_SetText(callMenu, row, row);
            }
            At<std::uint8_t>(callMenu, kNumberCount) = next;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    void __fastcall hk_UpdateCallMenu(std::uint8_t* callMenu)
    {
        g_Orig(callMenu);
        if (callMenu && !MissionCodeGuard::ShouldBypassHooks())
            RenumberRowsSeh(callMenu);
    }
}

bool Install_CallMenuImpl_UpdateCallMenu_Hook()
{
    if (g_HookTarget)
        return true;

    g_Addrs = AddrsForBuild();
    if (!g_Addrs.updateCallMenu)
    {
        LogError("CallMenu: CallMenuImpl::UpdateCallMenu address missing for %s, a hidden row can freeze the menu's wheel scroll",
                 GetGameBuildName(gGameBuild));
        return true;
    }

    g_SetText = reinterpret_cast<SetCommandText_t>(ResolveGameAddress(g_Addrs.setCommandText));
    void* target = ResolveGameAddress(g_Addrs.updateCallMenu);
    if (!g_SetText || !CreateAndEnableHook(target, reinterpret_cast<void*>(&hk_UpdateCallMenu),
                                           reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("CallMenu: CallMenuImpl::UpdateCallMenu hook failed at %p, a hidden row can freeze the menu's wheel scroll", target);
        return false;
    }

    g_HookTarget = target;
    return true;
}

void Uninstall_CallMenuImpl_UpdateCallMenu_Hook()
{
    if (g_HookTarget)
    {
        DisableAndRemoveHook(g_HookTarget);
        g_HookTarget = nullptr;
        g_Orig       = nullptr;
    }
}
