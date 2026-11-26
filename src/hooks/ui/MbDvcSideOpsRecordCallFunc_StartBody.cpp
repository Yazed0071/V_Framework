#include <pch.h>

#include <Windows.h>
#include <cstdint>

#include "MbDvcSideOpsRecordCallFunc_StartBody.h"
#include "LocationIconRegistry.h"

#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "log.h"

namespace
{
    using StartBody_t = std::uintptr_t(__fastcall*)(void* self);
    using GetMenu_t = std::uint8_t* (__fastcall*)(void* sideOpsList);
    using GetSideOp_t = std::uint8_t* (__fastcall*)(void* sideOpsList, std::uint32_t index);

    constexpr std::size_t kSlotGetMenu = 0x30 / sizeof(void*);
    constexpr std::size_t kSlotGetSideOp = 0x40 / sizeof(void*);

    StartBody_t g_OrigStartBody = nullptr;

    const std::uint8_t kPrologue[] = {
        0x48, 0x8B, 0xC4, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x70,
        0x48, 0x89, 0x58, 0x10, 0x48, 0x89, 0x68, 0x18, 0x48, 0x89, 0x70, 0xE0
    };

    bool ReadSideOpRowSEH(void* self, void*& ui, void*& mesh, std::int16_t& locationId)
    {
        __try
        {
            const auto* record = static_cast<const std::uint8_t*>(self);
            void* sideOpsList = *reinterpret_cast<void* const*>(record + 0xC8);
            if (!sideOpsList)
                return false;
            void** vtable = *static_cast<void***>(sideOpsList);
            const std::uint8_t* menu = reinterpret_cast<GetMenu_t>(vtable[kSlotGetMenu])(sideOpsList);
            const std::uint8_t* sideOp = reinterpret_cast<GetSideOp_t>(vtable[kSlotGetSideOp])(
                sideOpsList, *reinterpret_cast<const std::uint32_t*>(record + 0x08));
            if (!menu || !sideOp)
                return false;
            ui = *reinterpret_cast<void* const*>(menu + 0x20);
            mesh = *reinterpret_cast<void* const*>(record + 0xE8);   // the location icon mesh
            locationId = *reinterpret_cast<const std::int16_t*>(sideOp + 0x10);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    std::uintptr_t __fastcall hk_StartBody(void* self)
    {
        const std::uintptr_t result = g_OrigStartBody(self);
        if (MissionCodeGuard::ShouldBypassHooks())
            return result;

        void* ui = nullptr;
        void* mesh = nullptr;
        std::int16_t locationId = -1;
        if (ReadSideOpRowSEH(self, ui, mesh, locationId) && locationId >= 0)
            Apply_LocationIcon(ui, mesh, static_cast<std::uint16_t>(locationId));
        return result;
    }
}

bool Install_MbDvcSideOpsRecordCallFunc_StartBody_Hook()
{
    void* target = ResolveGameAddress(gAddr.MbDvcSideOpsRecordCallFunc_StartBody);
    if (!target)
    {
        LogError("LocationIcon: no Side Ops row function for %s, Side Ops rows show no custom location icon",
            GetGameBuildName(gGameBuild));
        return false;
    }

    const HookEntry entry = CheckHookEntry(target, kPrologue, sizeof(kPrologue));
    if (entry == HookEntry::Unknown)
    {
        char found[96] = {};
        DescribeFunctionEntry(target, found, sizeof(found));
        LogError("LocationIcon: Side Ops row function does not match (%s), Side Ops rows show no custom location icon", found);
        return false;
    }

    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hk_StartBody), reinterpret_cast<void**>(&g_OrigStartBody)))
    {
        LogError("LocationIcon: Side Ops row hook failed, Side Ops rows show no custom location icon");
        return false;
    }
    if (entry == HookEntry::ForeignHook)
        LogHookedOnTop("LocationIcon: Side Ops row function", target);
    return true;
}

bool Uninstall_MbDvcSideOpsRecordCallFunc_StartBody_Hook()
{
    if (gAddr.MbDvcSideOpsRecordCallFunc_StartBody)
        DisableAndRemoveHook(ResolveGameAddress(gAddr.MbDvcSideOpsRecordCallFunc_StartBody));
    g_OrigStartBody = nullptr;
    return true;
}
