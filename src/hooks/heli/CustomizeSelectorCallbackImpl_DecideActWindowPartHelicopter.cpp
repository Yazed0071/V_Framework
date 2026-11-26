#include <pch.h>
#include "CustomizeSelectorCallbackImpl_DecideActWindowPartHelicopter.h"

#include <Windows.h>
#include <cstdint>

#include "EquipDevelopControllerImpl_GetHeliPartsId.h"
#include "MissionCodeGuard.h"
#include "SupportHeliCustomizeMenu.h"
#include "SupportHeliHookInstall.h"
#include "SupportHeliWeapon.h"

namespace
{
    using Decide_t = std::uint64_t(__fastcall*)(void* callback, std::uint32_t itemIndex);

    constexpr std::size_t   kCallbackOpenWindow   = 0x71C;
    constexpr std::uint32_t kSubWeaponWindow      = 0x100002;

    constexpr std::uint8_t kDecideEntry[] =
    {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57,
    };

    Decide_t g_OrigDecide = nullptr;
    void*    g_Target     = nullptr;

    std::uint32_t OpenWindowSEH(void* callback)
    {
        __try
        {
            return *reinterpret_cast<const std::uint32_t*>(static_cast<std::uint8_t*>(callback) + kCallbackOpenWindow);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    std::uint64_t __fastcall hkDecide(void* callback, std::uint32_t itemIndex)
    {
        if (MissionCodeGuard::ShouldBypassHooks())
            return g_OrigDecide(callback, itemIndex);
        if (SupportHeliCustomizeMenu::DecidePart(callback, itemIndex))
            return 0;
        if (OpenWindowSEH(callback) != kSubWeaponWindow)
            return g_OrigDecide(callback, itemIndex);

        HeliParts_BeginDecideWatch();
        const std::uint64_t result = g_OrigDecide(callback, itemIndex);
        SupportHeliWeapon::NoteMenuChoice(HeliParts_EndDecideWatch());
        return result;
    }
}

bool Install_CustomizeSelectorCallbackImpl_DecideActWindowPartHelicopter_Hook()
{
    if (g_Target)
        return true;
    g_Target = InstallSupportHeliHook(
        "Pequod customize decide", gAddr.CustomizeSelectorCallbackImpl_DecideActWindowPartHelicopter,
        kDecideEntry, nullptr, sizeof(kDecideEntry),
        reinterpret_cast<void*>(&hkDecide), reinterpret_cast<void**>(&g_OrigDecide),
        "a custom Pequod weapon picked in the menu is not taken into the sortie");
    return g_Target != nullptr;
}

void Uninstall_CustomizeSelectorCallbackImpl_DecideActWindowPartHelicopter_Hook()
{
    UninstallSupportHeliHook(g_Target);
}
