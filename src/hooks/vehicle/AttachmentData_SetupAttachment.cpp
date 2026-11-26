#include <pch.h>
#include "AttachmentData_SetupAttachment.h"

#include <Windows.h>
#include <cstdint>

#include "AddressSet.h"
#include "CustomVehicles.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "VehicleData.h"
#include "log.h"

namespace
{
    using SetupAttachment_t = void(__fastcall*)(std::uint8_t* dataBody);

    constexpr std::size_t kEntityAttachmentFile = 0x88;
    constexpr std::size_t kEntityTypeCode       = 0xB1;   // vehicleTypeCode: the vehicle type in the high 4 bits, the sub type in the low 4

    const std::uint8_t kSetupAttachmentEntry[] = {
        0x40, 0x55, 0x56, 0x48, 0x81, 0xEC, 0xF8, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x05, 0x5F, 0x05, 0x01,
        0x02, 0x48, 0x33, 0xC4, 0x48, 0x89, 0x84, 0x24, 0xE0, 0x00, 0x00, 0x00, 0x48, 0x8B, 0xE9, 0x48,
    };
    const std::uint8_t kSetupAttachmentEntryMask[] = {
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0,
        0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    };

    SetupAttachment_t g_OrigSetupAttachment = nullptr;
    void*             g_HookTarget          = nullptr;

    void PutAttachmentOnItsRegisteredVehicle(std::uint8_t* entity)
    {
        std::uint8_t typeCode = 0;
        if (!entity || !VehicleData::ReadByte(entity, kEntityTypeCode, typeCode))
            return;
        const std::uint8_t type = CustomVehicles::TypeOfAttachmentFile(VehicleData::PathOfFileField(entity, kEntityAttachmentFile));
        const std::uint8_t typeInPack = typeCode >> 4;
        const bool gameType = typeInPack != 0 && typeInPack < CustomVehicles::kFirstType;   // a relief pack names the game vehicle it deploys as: left alone
        if (type != 0 && !gameType)
            VehicleData::WriteByte(entity, kEntityTypeCode, static_cast<std::uint8_t>((type << 4) | (typeCode & 0xF)));
    }

    void __fastcall hk_SetupAttachment(std::uint8_t* dataBody)
    {
        MISSION_GUARD_ORIGINAL_VOID(g_OrigSetupAttachment, dataBody);
        PutAttachmentOnItsRegisteredVehicle(VehicleData::EntityOfDataBody(dataBody));
        g_OrigSetupAttachment(dataBody);
    }
}

bool Install_AttachmentData_SetupAttachment_Hook()
{
    static_assert(sizeof(kSetupAttachmentEntry) == sizeof(kSetupAttachmentEntryMask), "one mask byte per entry byte");
    if (g_HookTarget)
        return true;
    auto* target = static_cast<std::uint8_t*>(ResolveGameAddress(gAddr.VehicleAttachmentData_SetupAttachment));
    if (!target)
    {
        LogError("CustomVehicles: no address for the vehicle attachment setup on %s, RegisterVehicle is off", GetGameBuildName(gGameBuild));
        return false;
    }
    const HookEntry entry = CheckHookEntryMasked(target, kSetupAttachmentEntry, kSetupAttachmentEntryMask, sizeof(kSetupAttachmentEntry));
    if (entry == HookEntry::Unknown)
    {
        char found[160] = {};
        DescribeFunctionEntry(target, found, sizeof(found));
        LogError("CustomVehicles: vehicle attachment setup does not match %s (%s), RegisterVehicle is off", GetGameBuildName(gGameBuild), found);
        return false;
    }
    if (entry == HookEntry::ForeignHook)
        LogHookedOnTop("CustomVehicles: AttachmentData::DataBody::SetupAttachment", target);
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hk_SetupAttachment), reinterpret_cast<void**>(&g_OrigSetupAttachment)))
    {
        LogError("CustomVehicles: AttachmentData::DataBody::SetupAttachment hook failed, RegisterVehicle is off");
        return false;
    }
    g_HookTarget = target;
    return true;
}

void Uninstall_AttachmentData_SetupAttachment_Hook()
{
    if (!g_HookTarget)
        return;
    DisableAndRemoveHook(g_HookTarget);
    g_HookTarget = nullptr;
    g_OrigSetupAttachment = nullptr;
}
