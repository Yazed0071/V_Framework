#include <pch.h>
#include "CustomizeSelectorCallbackImpl_HoverHeliPart.h"

#include <Windows.h>
#include <cstdint>

#include "EquipDevelopControllerImpl_GetHeliPartsId.h"
#include "MissionCodeGuard.h"
#include "SupportHeliCustomizeMenu.h"
#include "SupportHeliHookInstall.h"

namespace
{
    using HoverHeliPart_t = std::uint64_t(__fastcall*)(void* customize, std::uint32_t listIndex);

    constexpr std::size_t   kCallbackOpenWindow = 0x71C;
    constexpr std::size_t   kCallbackListRows   = 0x730;
    constexpr std::uint32_t kSubWeaponWindow    = 0x100002;
    constexpr std::uint16_t kNoHoveredRow       = 0xFFFF;

    constexpr std::uint8_t kHoverEntry[] =
    {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x8B, 0xC2, 0x8B, 0x91, 0x1C, 0x07,
    };

    HoverHeliPart_t g_Orig   = nullptr;
    void*           g_Target = nullptr;

    std::uint16_t HoveredSubRowSEH(const std::uint8_t* customize, std::uint32_t listIndex)
    {
        __try
        {
            if (*reinterpret_cast<const std::uint32_t*>(customize + kCallbackOpenWindow) != kSubWeaponWindow)
                return kNoHoveredRow;
            return *reinterpret_cast<const std::uint16_t*>(customize + kCallbackListRows + listIndex * sizeof(std::uint16_t));
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return kNoHoveredRow;
        }
    }

    std::uint64_t __fastcall hkHoverHeliPart(void* customize, std::uint32_t listIndex)
    {
        if (!MissionCodeGuard::ShouldBypassHooks())
        {
            if (SupportHeliCustomizeMenu::HoverPart(customize))   // the game's own hover would preview a custom row as a body grade
                return 0;
            HeliParts_SetHoveredSubRow(HoveredSubRowSEH(static_cast<const std::uint8_t*>(customize), listIndex));
        }
        return g_Orig(customize, listIndex);
    }
}

bool Install_CustomizeSelectorCallbackImpl_HoverHeliPart_Hook()
{
    if (g_Target)
        return true;
    g_Target = InstallSupportHeliHook(
        "Pequod customize hover", gAddr.CustomizeSelectorCallbackImpl_HoverHeliPart,
        kHoverEntry, nullptr, sizeof(kHoverEntry),
        reinterpret_cast<void*>(&hkHoverHeliPart), reinterpret_cast<void**>(&g_Orig),
        "the customize panel shows the base row while a custom Pequod weapon is hovered");
    return g_Target != nullptr;
}

void Uninstall_CustomizeSelectorCallbackImpl_HoverHeliPart_Hook()
{
    UninstallSupportHeliHook(g_Target);
}
