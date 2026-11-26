#include <pch.h>
#include "VehicleCommunicationInfo_FetchVehicleSeatLocalMatrix.h"

#include <Windows.h>
#include <cmath>
#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "log.h"
#include "../soldier/JeepGunnerSeatMotion.h"
#include "../soldier/RideVehicleActionImpl_State_IdleAction.h"

namespace
{
    using FetchVehicleSeatLocalMatrix_t = float*(__fastcall*)(const void* commInfo, float* matrix, std::uint8_t seat);

    constexpr std::uint8_t  kFrontPassengerSeat  = 1;
    constexpr std::size_t   kCommInfoCode        = 0x6C;   // vehicle type << 4 | sub type
    constexpr std::uint8_t  kEasternLightVehicle = 2;

    struct StandSpot
    {
        float x, y, z;
        float boardBack; // getting on and off he stands this much further back: the get-off lands 1.5 m behind, clear of the bumper
    };
    constexpr StandSpot kEasternStand = { 0.0f, 0.577f, -0.889f, 0.30f }; // the gun's mount less (0, 1.158, -0.054), where the tripod's base sits under it; body ends at z -2.3
    constexpr StandSpot kWesternStand = { 0.0f, 0.870f, -0.535f, 0.45f }; // body (spare wheel) ends at z -2.1
    constexpr float     kStandToGunPivotZ = -0.054f;
    constexpr float     kBoardFromBehindYaw = -1.5707964f; // seat 1's door side (the jeep's right, -x) turned to face the jeep's rear

    const std::uint8_t kFetchSeatPrologue[] = {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x41,
        0x0F, 0xB6, 0xF8, 0x48, 0x8B, 0xDA, 0x48, 0x8B, 0xF1
    };

    FetchVehicleSeatLocalMatrix_t g_OrigFetchSeat = nullptr;
    void*                         g_HookTarget    = nullptr;

    bool ReadVehicleCodeSEH(const void* commInfo, std::uint8_t& code)
    {
        __try
        {
            code = static_cast<const std::uint8_t*>(commInfo)[kCommInfoCode];
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    // The gunner stands behind the gun and turns with it around the gun's pivot, as a soldier on an emplaced gun does. Getting
    // on and off he plays the front passenger's seat-1 clips from that spot, turned so he climbs over the jeep's back.
    float* __fastcall hk_FetchVehicleSeatLocalMatrix(const void* commInfo, float* matrix, std::uint8_t seat)
    {
        MISSION_GUARD_ORIGINAL_RET(g_OrigFetchSeat, commInfo, matrix, seat);
        const bool gunnerSeat = seat == kJeepGunnerSeat;
        const bool boarding = seat == kFrontPassengerSeat && JeepGunner_InStandScope();
        std::uint8_t code = 0;
        if ((!gunnerSeat && !boarding) || !matrix || !JeepGunner_IsArmedLightVehicle(commInfo) || !ReadVehicleCodeSEH(commInfo, code))
            return g_OrigFetchSeat(commInfo, matrix, seat);
        const float turretYaw = boarding ? kBoardFromBehindYaw : JeepGunner_GunYaw(commInfo);
        const StandSpot& spot = (code >> 4) == kEasternLightVehicle ? kEasternStand : kWesternStand;
        const float c = std::cos(turretYaw);
        const float s = std::sin(turretYaw);
        const float pivotZ = spot.z + kStandToGunPivotZ - (boarding ? spot.boardBack : 0.0f);
        const float aheadOfPivot = -kStandToGunPivotZ;
        const float local[16] = {
            c,    0.0f, -s,   0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            s,    0.0f, c,    0.0f,
            spot.x + s * aheadOfPivot, spot.y, pivotZ + c * aheadOfPivot, 1.0f,
        };
        for (int i = 0; i < 16; ++i)
            matrix[i] = local[i];
        return matrix;
    }
}

bool Install_VehicleCommunicationInfo_FetchVehicleSeatLocalMatrix_Hook()
{
    if (g_HookTarget)
        return true;
    auto* target = static_cast<std::uint8_t*>(ResolveGameAddress(gAddr.VehicleCommunicationInfo_FetchVehicleSeatLocalMatrix));
    if (!target)
        return false;
    const HookEntry entry = CheckHookEntry(target, kFetchSeatPrologue, sizeof(kFetchSeatPrologue));
    if (entry == HookEntry::Unknown)
    {
        LogError("JeepGunner: vehicle seat code does not match %s, armed jeeps carry no gunner", GetGameBuildName(gGameBuild));
        return false;
    }
    if (entry == HookEntry::ForeignHook)
        LogHookedOnTop("JeepGunner: VehicleCommunicationInfo::FetchVehicleSeatLocalMatrix", target);
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hk_FetchVehicleSeatLocalMatrix), reinterpret_cast<void**>(&g_OrigFetchSeat)))
    {
        LogError("JeepGunner: VehicleCommunicationInfo::FetchVehicleSeatLocalMatrix hook failed, armed jeeps carry no gunner");
        return false;
    }
    g_HookTarget = target;
    return true;
}

bool Uninstall_VehicleCommunicationInfo_FetchVehicleSeatLocalMatrix_Hook()
{
    if (g_HookTarget)
    {
        DisableAndRemoveHook(g_HookTarget);
        g_HookTarget = nullptr;
        g_OrigFetchSeat = nullptr;
    }
    return true;
}
