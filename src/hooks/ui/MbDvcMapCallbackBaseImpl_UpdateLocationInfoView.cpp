#include <pch.h>

#include <Windows.h>
#include <cstdint>

#include "MbDvcMapCallbackBaseImpl_UpdateLocationInfoView.h"
#include "LocationIconRegistry.h"

#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "log.h"

namespace
{
    using UpdateLocationInfoView_t = std::uintptr_t(__fastcall*)(void* self);

    UpdateLocationInfoView_t g_OrigUpdateLocationInfoView = nullptr;

    const std::uint8_t kPrologue[] = {
        0x48, 0x89, 0x5C, 0x24, 0x18, 0x48, 0x89, 0x6C, 0x24, 0x20, 0x56, 0x57, 0x41,
        0x54, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x40, 0x48, 0x8B, 0x41, 0x38
    };

    bool ReadLocationInfoSEH(void* self, void*& ui, void*& mesh, std::int16_t& locationId)
    {
        __try
        {
            const auto* map = static_cast<const std::uint8_t*>(self);
            const auto* menu = *reinterpret_cast<const std::uint8_t* const*>(map + 0x38);
            if (!menu)
                return false;
            ui = *reinterpret_cast<void* const*>(menu + 0x20);
            mesh = *reinterpret_cast<void* const*>(map + 0x3D8);   // the location icon mesh
            locationId = *reinterpret_cast<const std::int16_t*>(map + 0x3B8);   // the location shown, -2 while the view is hidden
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    std::uintptr_t __fastcall hk_UpdateLocationInfoView(void* self)
    {
        const std::uintptr_t result = g_OrigUpdateLocationInfoView(self);
        if (MissionCodeGuard::ShouldBypassHooks())
            return result;

        void* ui = nullptr;
        void* mesh = nullptr;
        std::int16_t locationId = -2;
        if (ReadLocationInfoSEH(self, ui, mesh, locationId) && locationId >= 0)
            Apply_LocationIcon(ui, mesh, static_cast<std::uint16_t>(locationId));
        return result;
    }
}

bool Install_MbDvcMapCallbackBaseImpl_UpdateLocationInfoView_Hook()
{
    void* target = ResolveGameAddress(gAddr.MbDvcMapCallbackBaseImpl_UpdateLocationInfoView);
    if (!target)
    {
        LogError("LocationIcon: no MbDvcMapCallbackBaseImpl::UpdateLocationInfoView for %s, the iDroid location header shows no custom location icon",
            GetGameBuildName(gGameBuild));
        return false;
    }

    const HookEntry entry = CheckHookEntry(target, kPrologue, sizeof(kPrologue));
    if (entry == HookEntry::Unknown)
    {
        char found[96] = {};
        DescribeFunctionEntry(target, found, sizeof(found));
        LogError("LocationIcon: MbDvcMapCallbackBaseImpl::UpdateLocationInfoView does not match (%s), the iDroid location header shows no custom location icon", found);
        return false;
    }

    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hk_UpdateLocationInfoView), reinterpret_cast<void**>(&g_OrigUpdateLocationInfoView)))
    {
        LogError("LocationIcon: MbDvcMapCallbackBaseImpl::UpdateLocationInfoView hook failed, the iDroid location header shows no custom location icon");
        return false;
    }
    if (entry == HookEntry::ForeignHook)
        LogHookedOnTop("LocationIcon: MbDvcMapCallbackBaseImpl::UpdateLocationInfoView", target);
    return true;
}

bool Uninstall_MbDvcMapCallbackBaseImpl_UpdateLocationInfoView_Hook()
{
    if (gAddr.MbDvcMapCallbackBaseImpl_UpdateLocationInfoView)
        DisableAndRemoveHook(ResolveGameAddress(gAddr.MbDvcMapCallbackBaseImpl_UpdateLocationInfoView));
    g_OrigUpdateLocationInfoView = nullptr;
    return true;
}
