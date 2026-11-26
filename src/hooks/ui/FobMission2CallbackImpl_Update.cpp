#include <pch.h>
#include "FobMission2CallbackImpl_Update.h"

#include <Windows.h>
#include <cstddef>
#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "log.h"

namespace
{
    using Update_t = void(__fastcall*)(void* menu);

    const std::uint8_t kUpdateEntry[] = { 0x4C, 0x8B, 0xDC, 0x55, 0x41, 0x57, 0x49, 0x8D, 0xAB, 0x98, 0xFB, 0xFF, 0xFF,
                                          0x48, 0x81, 0xEC, 0x58, 0x05, 0x00, 0x00, 0x41, 0x0F, 0x29, 0x73, 0xC8 };

    constexpr std::size_t  kShownTab = 0x2D38;   // the tab whose list is on screen
    constexpr std::uint8_t kNoTab    = 0xFF;

    Update_t           g_OrigUpdate   = nullptr;
    void*              g_UpdateTarget = nullptr;
    thread_local void* t_UpdatingMenu = nullptr;

    void __fastcall hkUpdate(void* menu)
    {
        t_UpdatingMenu = menu;
        g_OrigUpdate(menu);
        t_UpdatingMenu = nullptr;
    }

    std::uint8_t ReadShownTab_SEH(const std::uint8_t* menu)
    {
        __try
        {
            return menu[kShownTab];
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return kNoTab;
        }
    }
}

bool Install_FobMission2CallbackImpl_Update_Hook()
{
    if (g_UpdateTarget)
        return true;

    void* target = ResolveGameAddress(gAddr.FobMission2CallbackImpl_Update);
    const HookEntry state = CheckHookEntry(target, kUpdateEntry, sizeof(kUpdateEntry));
    if (state == HookEntry::Unknown)
    {
        char entry[160] = {};
        DescribeFunctionEntry(target, entry, sizeof(entry));
        LogError("RetaliationLine: FobMission2CallbackImpl::Update entry mismatch on %s (%s), FOB menu tabs are not read",
                 GetGameBuildName(gGameBuild), entry);
        return true;
    }
    if (state == HookEntry::ForeignHook)
        LogHookedOnTop("RetaliationLine: FobMission2CallbackImpl::Update", target);
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hkUpdate), reinterpret_cast<void**>(&g_OrigUpdate)))
    {
        LogError("RetaliationLine: FobMission2CallbackImpl::Update hook failed at %p, FOB menu tabs are not read", target);
        return true;
    }
    g_UpdateTarget = target;
    return true;
}

void Uninstall_FobMission2CallbackImpl_Update_Hook()
{
    if (g_UpdateTarget)
        DisableAndRemoveHook(g_UpdateTarget);
    g_UpdateTarget = nullptr;
}

std::uint8_t SelectedFobMenuTab()
{
    if (!t_UpdatingMenu)
        return kNoTab;
    return ReadShownTab_SEH(static_cast<const std::uint8_t*>(t_UpdatingMenu));
}
