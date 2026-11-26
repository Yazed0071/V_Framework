#include <pch.h>
#include "ButtonMapper_SetDefaultButtonMap.h"

#include <Windows.h>
#include <cstddef>
#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "TextIcons.h"
#include "log.h"

namespace
{
    using SetDefaultButtonMap_t = void(__fastcall*)(void* mapper, std::uint8_t* const* action, std::uint32_t padButton, std::uint32_t keyCode);

    constexpr std::size_t kStringHash = 0x10;   // the action name's StrCode32 inside the fox::String data, the key of the button map

    constexpr std::uint8_t kSetDefaultButtonMapEntry[] =
    {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57,
        0x48, 0x83, 0xEC, 0x50, 0x48, 0x8B, 0xF9, 0x48, 0x8B, 0xCA, 0x41, 0x8B, 0xF1, 0x41, 0x8B, 0xE8,
    };

    SetDefaultButtonMap_t g_Orig       = nullptr;
    void*                 g_HookTarget = nullptr;

    std::uint32_t ActionHashSEH(std::uint8_t* const* action)
    {
        __try
        {
            return action && *action ? *reinterpret_cast<const std::uint32_t*>(*action + kStringHash) : 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    void __fastcall hk_SetDefaultButtonMap(void* mapper, std::uint8_t* const* action, std::uint32_t padButton, std::uint32_t keyCode)
    {
        g_Orig(mapper, action, padButton, keyCode);
        if (const std::uint32_t hash = ActionHashSEH(action))
            TextIcons::AddForNewAction(hash);   // only queued: the icon is written outside FOB, in the game's own icon writer
    }
}

bool Install_ButtonMapper_SetDefaultButtonMap_Hook()
{
    if (g_HookTarget)
        return true;

    void* target = ResolveGameAddress(gAddr.ButtonMapper_SetDefaultButtonMap);
    const HookEntry entry = CheckHookEntry(target, kSetDefaultButtonMapEntry, sizeof(kSetDefaultButtonMapEntry));
    if (entry == HookEntry::Unknown)
    {
        char found[160] = {};
        DescribeFunctionEntry(target, found, sizeof(found));
        LogError("TextIcons: ButtonMapper::SetDefaultButtonMap is not the known code on %s (%s), actions added with UiDaemon.SetButtonMap get no text icon by themselves",
                 GetGameBuildName(gGameBuild), found);
        return false;
    }
    if (entry == HookEntry::ForeignHook)
        LogHookedOnTop("TextIcons: ButtonMapper::SetDefaultButtonMap", target);
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hk_SetDefaultButtonMap), reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("TextIcons: ButtonMapper::SetDefaultButtonMap hook failed at %p, actions added with UiDaemon.SetButtonMap get no text icon by themselves", target);
        return false;
    }
    g_HookTarget = target;
    return true;
}

void Uninstall_ButtonMapper_SetDefaultButtonMap_Hook()
{
    if (g_HookTarget)
    {
        DisableAndRemoveHook(g_HookTarget);
        g_HookTarget = nullptr;
        g_Orig       = nullptr;
    }
}
