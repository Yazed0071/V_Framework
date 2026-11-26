#include <pch.h>
#include "ConstantImpl_GetVehicleTypeSpecs.h"

#include <Windows.h>
#include <cstdint>

#include "AddressSet.h"
#include "CustomVehicles.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "log.h"

namespace
{
    using GetVehicleTypeSpecs_t = void*(__fastcall*)(void* constant, std::uint8_t typeCode);

    const std::uint8_t kGetVehicleTypeSpecsEntry[] = {
        0x44, 0x0F, 0xB6, 0xC2, 0x41, 0x8B, 0xC0, 0xC1, 0xE8, 0x04, 0xFF, 0xC8, 0x83, 0xF8, 0x07, 0x0F,
        0x87, 0x85, 0x00, 0x00, 0x00, 0x4C, 0x8D, 0x0D, 0xE4, 0xAD, 0x90, 0xFE, 0x48, 0x98, 0x41, 0x8B,
        0x94, 0x81, 0xA0, 0x52, 0x6F, 0x01, 0x49, 0x03, 0xD1, 0xFF, 0xE2, 0x48, 0x8B, 0x41, 0x40, 0x48,
    };
    const std::uint8_t kGetVehicleTypeSpecsEntryMask[] = {
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1,
        1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    };

    GetVehicleTypeSpecs_t g_OrigGetVehicleTypeSpecs = nullptr;
    void*                 g_HookTarget              = nullptr;

    void* __fastcall hk_GetVehicleTypeSpecs(void* constant, std::uint8_t typeCode)
    {
        MISSION_GUARD_ORIGINAL_RET(g_OrigGetVehicleTypeSpecs, constant, typeCode);
        std::uint8_t proxyCode = 0;
        if (CustomVehicles::ProxyTypeCode(typeCode, proxyCode))
            return g_OrigGetVehicleTypeSpecs(constant, proxyCode);
        return g_OrigGetVehicleTypeSpecs(constant, typeCode);
    }
}

bool Install_ConstantImpl_GetVehicleTypeSpecs_Hook()
{
    static_assert(sizeof(kGetVehicleTypeSpecsEntry) == sizeof(kGetVehicleTypeSpecsEntryMask), "one mask byte per entry byte");
    if (g_HookTarget)
        return true;
    auto* target = static_cast<std::uint8_t*>(ResolveGameAddress(gAddr.VehicleConstantImpl_GetVehicleTypeSpecs));
    if (!target)
    {
        LogError("CustomVehicles: no address for the vehicle life table on %s, RegisterVehicle is off", GetGameBuildName(gGameBuild));
        return false;
    }
    const HookEntry entry = CheckHookEntryMasked(target, kGetVehicleTypeSpecsEntry, kGetVehicleTypeSpecsEntryMask, sizeof(kGetVehicleTypeSpecsEntry));
    if (entry == HookEntry::Unknown)
    {
        char found[160] = {};
        DescribeFunctionEntry(target, found, sizeof(found));
        LogError("CustomVehicles: vehicle life table lookup does not match %s (%s), RegisterVehicle is off", GetGameBuildName(gGameBuild), found);
        return false;
    }
    if (entry == HookEntry::ForeignHook)
        LogHookedOnTop("CustomVehicles: ConstantImpl::GetVehicleTypeSpecs", target);
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hk_GetVehicleTypeSpecs), reinterpret_cast<void**>(&g_OrigGetVehicleTypeSpecs)))
    {
        LogError("CustomVehicles: ConstantImpl::GetVehicleTypeSpecs hook failed, RegisterVehicle is off");
        return false;
    }
    g_HookTarget = target;
    return true;
}

void Uninstall_ConstantImpl_GetVehicleTypeSpecs_Hook()
{
    if (!g_HookTarget)
        return;
    DisableAndRemoveHook(g_HookTarget);
    g_HookTarget = nullptr;
    g_OrigGetVehicleTypeSpecs = nullptr;
}
