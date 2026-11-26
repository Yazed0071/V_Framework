#include <pch.h>

#include "CustomizeSelectorCallbackImpl_SetupItemList.h"

#include <cstdint>

#include "../buddy/BattleGearCustomizePage.h"
#include "../heli/SupportHeliCustomizeMenu.h"
#include "AddressSet.h"
#include "EquipDevelop_SetEquipUndeveloped.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "log.h"

namespace
{
    using SetupItemList_t = std::uint64_t(__fastcall*)(std::uintptr_t callback);

    constexpr std::uint8_t kSetupItemListEntry[] =
    {
        0x4C, 0x8B, 0xDC, 0x49, 0x89, 0x5B, 0x20, 0x55, 0x56, 0x57, 0x41, 0x55, 0x41, 0x57,
    };

    SetupItemList_t g_Orig   = nullptr;
    void*           g_Target = nullptr;

    std::uint64_t __fastcall hkSetupItemList(std::uintptr_t callback)
    {
        if (!MissionCodeGuard::ShouldBypassHooks())
            EquipDevelop_DrainIfRestorePending();
        const std::uint64_t result = g_Orig(callback);
        battleGearCustomizePage::AfterSetupItemList(callback);
        SupportHeliCustomizeMenu::AfterSetupItemList(callback);
        return result;
    }
}

namespace equip
{
    bool Install_CustomizeSelectorCallbackImpl_SetupItemList_Hook()
    {
        if (g_Target)
            return true;
        void* target = gAddr.CustomizeSelectorCallbackImpl_SetupItemList
            ? ResolveGameAddress(gAddr.CustomizeSelectorCallbackImpl_SetupItemList) : nullptr;
        if (!target)
        {
            LogError("EquipDevelop: Customize item list address missing on this build, "
                     "custom R&D rows stay out of Customize until R&D is opened");
            return false;
        }
        const HookEntry state = CheckHookEntry(target, kSetupItemListEntry, sizeof(kSetupItemListEntry));
        if (state == HookEntry::Unknown)
        {
            char desc[160] = {};
            DescribeFunctionEntry(target, desc, sizeof(desc));
            LogError("EquipDevelop: Customize item list code differs on this build (%s), "
                     "custom R&D rows stay out of Customize until R&D is opened", desc);
            return false;
        }
        if (state == HookEntry::ForeignHook)
            LogHookedOnTop("EquipDevelop: Customize item list", target);
        if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hkSetupItemList), reinterpret_cast<void**>(&g_Orig)))
        {
            LogError("EquipDevelop: Customize item list hook failed, "
                     "custom R&D rows stay out of Customize until R&D is opened");
            return false;
        }
        g_Target = target;
        return true;
    }

    void Uninstall_CustomizeSelectorCallbackImpl_SetupItemList_Hook()
    {
        if (!g_Target)
            return;
        DisableAndRemoveHook(g_Target);
        g_Target = nullptr;
    }
}
