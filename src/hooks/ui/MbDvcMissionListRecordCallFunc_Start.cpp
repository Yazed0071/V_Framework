#include <pch.h>

#include <Windows.h>
#include <cstdint>

#include "MbDvcMissionListRecordCallFunc_Start.h"
#include "LocationIconRegistry.h"

#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "log.h"

namespace
{
    using Start_t = std::uintptr_t(__fastcall*)(void* self, void* layout);

    Start_t g_OrigStart = nullptr;

    const std::uint8_t kPrologue[] = {
        0x48, 0x8B, 0xC4, 0x55, 0x56, 0x48, 0x81, 0xEC, 0x88, 0x00, 0x00, 0x00,
        0x48, 0x83, 0xB9, 0xD0, 0x00, 0x00, 0x00, 0x00, 0x48, 0x89, 0x58, 0x10
    };

    bool ReadRowSEH(void* self, void*& ui, void*& mesh, std::uint16_t& locationId)
    {
        __try
        {
            const auto* record = static_cast<const std::uint8_t*>(self);
            const auto* list = *reinterpret_cast<const std::uint8_t* const*>(record + 0xD8);
            const auto* rows = *reinterpret_cast<const std::uint8_t* const*>(record + 0xE0);
            const std::uint32_t rowIndex = *reinterpret_cast<const std::uint32_t*>(record + 0x08);
            if (!list || !rows)
                return false;
            ui = *reinterpret_cast<void* const*>(list + 0x20);
            mesh = *reinterpret_cast<void* const*>(record + 0x158);   // the location icon mesh
            locationId = *reinterpret_cast<const std::uint16_t*>(rows + rowIndex * 0x28 + 0x1E);   // rows are 0x28 bytes, the location code at +0x1E
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    std::uintptr_t __fastcall hk_Start(void* self, void* layout)
    {
        const std::uintptr_t result = g_OrigStart(self, layout);
        if (MissionCodeGuard::ShouldBypassHooks())
            return result;

        void* ui = nullptr;
        void* mesh = nullptr;
        std::uint16_t locationId = 0;
        if (ReadRowSEH(self, ui, mesh, locationId))
            Apply_LocationIcon(ui, mesh, locationId);
        return result;
    }
}

bool Install_MbDvcMissionListRecordCallFunc_Start_Hook()
{
    void* target = ResolveGameAddress(gAddr.MbDvcMissionListRecordCallFunc_Start);
    if (!target)
    {
        LogError("LocationIcon: no MbDvcMissionListRecordCallFunc::Start for %s, Missions list rows show no custom location icon",
            GetGameBuildName(gGameBuild));
        return false;
    }

    const HookEntry entry = CheckHookEntry(target, kPrologue, sizeof(kPrologue));
    if (entry == HookEntry::Unknown)
    {
        char found[96] = {};
        DescribeFunctionEntry(target, found, sizeof(found));
        LogError("LocationIcon: MbDvcMissionListRecordCallFunc::Start does not match (%s), Missions list rows show no custom location icon", found);
        return false;
    }

    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hk_Start), reinterpret_cast<void**>(&g_OrigStart)))
    {
        LogError("LocationIcon: MbDvcMissionListRecordCallFunc::Start hook failed, Missions list rows show no custom location icon");
        return false;
    }
    if (entry == HookEntry::ForeignHook)
        LogHookedOnTop("LocationIcon: MbDvcMissionListRecordCallFunc::Start", target);
    return true;
}

bool Uninstall_MbDvcMissionListRecordCallFunc_Start_Hook()
{
    if (gAddr.MbDvcMissionListRecordCallFunc_Start)
        DisableAndRemoveHook(ResolveGameAddress(gAddr.MbDvcMissionListRecordCallFunc_Start));
    g_OrigStart = nullptr;
    return true;
}
