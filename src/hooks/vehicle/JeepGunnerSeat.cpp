#include <pch.h>
#include "JeepGunnerSeat.h"

#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <iterator>

#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "log.h"
#include "../soldier/RideVehicleActionImpl_State_IdleAction.h"

namespace
{
    using TryToRide_t              = bool(__fastcall*)(void* vehicleSystem, void* commInfo, std::uint8_t seat, std::uint16_t rider);
    using GetRiderOff_t            = void(__fastcall*)(void* vehicleSystem, void* commInfo, std::uint16_t rider);
    using ExtendedRiderCall_t      = std::int32_t*(__fastcall*)(void* vehicleSystem, std::int32_t* error, void* commInfo, std::uint16_t rider);
    using TransmitDamageToRiders_t = void(__fastcall*)(void* commInfo, std::uint16_t attacker, std::uint32_t attackId, std::uint32_t damage);
    using AddPassenger_t           = bool(__fastcall*)(void* passengers, std::uint16_t vehicle, std::uint16_t rider, std::uint8_t* seat,
                                                       bool slotFlag1, bool slotFlag2, bool slotFlag4);
    using UpdateReserveSeatIndex_t = bool(__fastcall*)(void* passengers, std::uint16_t vehicle, std::uint16_t rider, std::uint8_t* seat);
    using GetQuarkSystemTable_t    = std::uint8_t*(__fastcall*)();
    using SendGameObjectMessage_t  = void(__fastcall*)(void* gameObjects, std::uint64_t* result, std::uint16_t target, void* message);

    constexpr std::size_t   kQuarkApplication       = 0x98;
    constexpr std::size_t   kApplicationVehicles    = 0x2A8;  // the vehicle SystemImpl
    constexpr std::size_t   kQuarkGameObjects       = 0x60;
    constexpr std::size_t   kGameObjectsSendMessage = 0x38;
    constexpr std::size_t   kSystemVehicleObjects   = 0xB0;   // the vehicles' GameObjectImpl
    constexpr std::size_t   kObjectsCommInfos       = 0x10;
    constexpr std::size_t   kCommInfoSize           = 0x80;
    constexpr std::size_t   kCommInfoVehicleIndex   = 0x78;
    constexpr std::size_t   kExtendedRiders         = 0x1EE;  // { u16 vehicle id, u16 rider id } x 8 for the whole mission
    constexpr std::size_t   kExtendedRiderCount     = 0x211;
    constexpr std::uint32_t kMaxExtendedRiders      = 8;
    constexpr std::uint16_t kVehicleIdBase          = 0x6400;
    constexpr std::uint16_t kIndexMask              = 0x1FF;
    constexpr std::uint16_t kTypeMask               = 0xFE00;
    constexpr std::uint16_t kSoldierType            = 0x400;
    constexpr std::uint16_t kNoRider                = 0xFFFF;
    constexpr std::int32_t  kNoError                = 0;
    constexpr std::int32_t  kFailed                 = -1;
    constexpr std::uint64_t kRiderDamageMessage     = 0x721148E385F2; // what TransmitDamageToRiders sends each seat rider

    constexpr std::size_t   kPassengerEntries       = 0x18;   // 0x22 per vehicle: u16 vehicle id, 5 slots from +2, u8 slot count at +0x20
    constexpr std::size_t   kPassengerEntryCount    = 0x20;
    constexpr std::size_t   kPassengerEntrySize     = 0x22;
    constexpr std::size_t   kEntrySlots             = 0x02;
    constexpr std::size_t   kEntrySlotCount         = 0x20;
    constexpr std::size_t   kSlotSize               = 6;      // u16 rider, u8 state, u8 seat, u8 flags
    constexpr std::size_t   kSlotState              = 2;
    constexpr std::size_t   kSlotSeat               = 3;
    constexpr std::size_t   kSlotFlags              = 4;
    constexpr std::uint8_t  kSlotTaken              = 1;
    constexpr std::uint8_t  kSlotFlagsKept          = 0xE7;
    constexpr std::uint8_t  kGunnerSlot             = 4;      // the fifth slot: vanilla only ever fills it with seat 4, "no seat"
    constexpr std::uint8_t  kSeatSlots              = 4;
    constexpr std::uint8_t  kAnySeat                = 4;

    const std::uint8_t kTryToRidePrologue[] = {
        0x66, 0x44, 0x89, 0x4C, 0x24, 0x20, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x48, 0xC7, 0x44, 0x24, 0x20,
        0xFE, 0xFF, 0xFF, 0xFF, 0x48, 0x89, 0x5C, 0x24, 0x48
    };
    const std::uint8_t kGetRiderOffPrologue[] = {
        0x40, 0x56, 0x57, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x30, 0x48, 0xC7, 0x44, 0x24, 0x20, 0xFE, 0xFF,
        0xFF, 0xFF, 0x48, 0x89, 0x5C, 0x24, 0x58
    };
    const std::uint8_t kTransmitDamagePrologue[] = {
        0x48, 0x89, 0x5C, 0x24, 0x18, 0x66, 0x89, 0x54, 0x24, 0x10, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41,
        0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x40
    };
    const std::uint8_t kAddPassengerPrologue[] = {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x20, 0x66, 0x44, 0x89, 0x44, 0x24, 0x18,
        0x66, 0x89, 0x54, 0x24, 0x10, 0x57, 0x44, 0x0F, 0xB6, 0x59, 0x20
    };
    const std::uint8_t kUpdateReserveSeatPrologue[] = {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x48,
        0x89, 0x7C, 0x24, 0x20, 0x41, 0x56, 0x41, 0x57, 0x44, 0x0F, 0xB6, 0x59, 0x20
    };
    const std::uint8_t kRegisterExtendedRiderPrologue[] = {
        0x66, 0x44, 0x89, 0x4C, 0x24, 0x20, 0x55, 0x56, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x48, 0xC7, 0x44,
        0x24, 0x20, 0xFE, 0xFF, 0xFF, 0xFF, 0x48, 0x89, 0x5C, 0x24, 0x58, 0x49, 0x8B, 0xE8, 0x48, 0x8B,
        0xFA, 0x48, 0x8B, 0xF1, 0x48, 0x83, 0xB9, 0xB0, 0x00, 0x00, 0x00, 0x00, 0x75, 0x08, 0xC7, 0x02,
        0xFF, 0xFF, 0xFF, 0xFF
    };
    const std::uint8_t kUnregisterExtendedRiderPrologue[] = {
        0x66, 0x44, 0x89, 0x4C, 0x24, 0x20, 0x55, 0x56, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x48, 0xC7, 0x44,
        0x24, 0x20, 0xFE, 0xFF, 0xFF, 0xFF, 0x48, 0x89, 0x5C, 0x24, 0x58, 0x49, 0x8B, 0xE8, 0x48, 0x8B,
        0xFA, 0x48, 0x8B, 0xF1, 0x48, 0x83, 0xB9, 0xB0, 0x00, 0x00, 0x00, 0x00, 0x75, 0x08, 0xC7, 0x02,
        0x00, 0x00, 0x00, 0x00
    };

    TryToRide_t              g_OrigTryToRide              = nullptr;
    GetRiderOff_t            g_OrigGetRiderOff            = nullptr;
    TransmitDamageToRiders_t g_OrigTransmitDamage         = nullptr;
    AddPassenger_t           g_OrigAddPassenger           = nullptr;
    UpdateReserveSeatIndex_t g_OrigUpdateReserveSeatIndex = nullptr;
    ExtendedRiderCall_t      g_RegisterExtendedRider      = nullptr;
    ExtendedRiderCall_t      g_UnregisterExtendedRider    = nullptr;
    GetQuarkSystemTable_t    g_GetQuarkSystemTable        = nullptr;
#ifdef _DEBUG
    constexpr std::size_t      kCommInfoRiders = 0x30; // 4 rider game object ids by seat, 0xFFFF = empty
    std::atomic<std::uint32_t> g_ProbeRefused  = 0;
#endif

    std::uint8_t* VehicleSystemSEH()
    {
        __try
        {
            auto* application = *reinterpret_cast<std::uint8_t**>(g_GetQuarkSystemTable() + kQuarkApplication);
            return *reinterpret_cast<std::uint8_t**>(application + kApplicationVehicles);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    const void* CommInfoOfVehicleSEH(std::uint16_t vehicleId)
    {
        __try
        {
            const std::uint8_t* system = VehicleSystemSEH();
            if (!system)
                return nullptr;
            const auto* objects = *reinterpret_cast<const std::uint8_t* const*>(system + kSystemVehicleObjects);
            const auto* commInfos = *reinterpret_cast<const std::uint8_t* const*>(objects + kObjectsCommInfos);
            const std::uint16_t index = vehicleId & kIndexMask;
            const std::uint8_t* commInfo = commInfos + index * kCommInfoSize;
            return (*reinterpret_cast<const std::uint16_t*>(commInfo + kCommInfoVehicleIndex) & kIndexMask) == index ? commInfo : nullptr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    // The armed jeep's gunner, found in the mission's extended rider table (where APC and tank passengers ride)
    std::uint16_t GunnerOfVehicleSEH(const void* vehicleSystem, const void* commInfo)
    {
        __try
        {
            const auto* objects = *reinterpret_cast<const std::uint8_t* const*>(static_cast<const std::uint8_t*>(vehicleSystem) + kSystemVehicleObjects);
            const std::uint16_t vehicle = static_cast<std::uint16_t>(
                (*reinterpret_cast<const std::uint16_t*>(static_cast<const std::uint8_t*>(commInfo) + kCommInfoVehicleIndex) & kIndexMask) | kVehicleIdBase);
            const std::uint32_t count = objects[kExtendedRiderCount];
            for (std::uint32_t i = 0; i < count && i < kMaxExtendedRiders; ++i)
            {
                const auto* pair = reinterpret_cast<const std::uint16_t*>(objects + kExtendedRiders + i * 4);
                if (pair[0] == vehicle)
                    return pair[1];
            }
            return kNoRider;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return kNoRider;
        }
    }

#ifdef _DEBUG
    // TEMP: one line per boarding on an armed jeep, to read the five riders off a test log
    void ProbeRiders(const void* vehicleSystem, const void* commInfo, std::uint8_t seat, std::uint16_t rider, bool aboard)
    {
        std::uint16_t seats[kSeatSlots] = { kNoRider, kNoRider, kNoRider, kNoRider };
        __try
        {
            std::memcpy(seats, static_cast<const std::uint8_t*>(commInfo) + kCommInfoRiders, sizeof(seats));
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
        const std::uint16_t gunner = GunnerOfVehicleSEH(vehicleSystem, commInfo);
        const std::uint32_t riders = (seats[0] != kNoRider) + (seats[1] != kNoRider) + (seats[2] != kNoRider) + (seats[3] != kNoRider)
            + (gunner != kNoRider);
        LogDebug("JeepGunner probe: rider 0x%04X seat %u %s | seats 0x%04X 0x%04X 0x%04X 0x%04X gunner 0x%04X = %u aboard",
            rider, seat, aboard ? "boards" : "refused", seats[0], seats[1], seats[2], seats[3], gunner, riders);
    }
#endif

    // The rider list at +0x30 holds four seats and vanilla writes it by seat with no range check: the gunner (seat 5) rides
    // in the extended rider table instead, which the jeep's eject also empties
    bool __fastcall hk_TryToRide(void* vehicleSystem, void* commInfo, std::uint8_t seat, std::uint16_t rider)
    {
        if (seat != kJeepGunnerSeat)
        {
            const bool aboard = g_OrigTryToRide(vehicleSystem, commInfo, seat, rider);
#ifdef _DEBUG
            if (JeepGunner_IsArmedLightVehicle(commInfo))
                ProbeRiders(vehicleSystem, commInfo, seat, rider, aboard);
#endif
            return aboard;
        }
        if (!JeepGunner_IsArmedLightVehicle(commInfo))
            return false;
        std::int32_t error = kNoError;
        g_UnregisterExtendedRider(vehicleSystem, &error, commInfo, rider); // boarding twice with no get-off in between must not list him twice
        error = kFailed;
        g_RegisterExtendedRider(vehicleSystem, &error, commInfo, rider);
#ifdef _DEBUG
        ProbeRiders(vehicleSystem, commInfo, seat, rider, error == kNoError);
#endif
        return error == kNoError;
    }

    void __fastcall hk_GetRiderOff(void* vehicleSystem, void* commInfo, std::uint16_t rider)
    {
        g_OrigGetRiderOff(vehicleSystem, commInfo, rider);
        if (!JeepGunner_IsArmedLightVehicle(commInfo))
            return;
        std::int32_t error = kNoError;
        g_UnregisterExtendedRider(vehicleSystem, &error, commInfo, rider); // a seat rider has no entry there: the call then changes nothing
    }

    struct RiderDamageMessage
    {
        std::uint64_t id;
        std::uint16_t attacker;
        std::uint16_t vehicle;
        std::uint32_t attackId;
        std::uint32_t damage;
    };

    bool ReadMessageSenderSEH(void*& gameObjects, SendGameObjectMessage_t& send, std::uint16_t& vehicle, const void* commInfo)
    {
        __try
        {
            gameObjects = *reinterpret_cast<void**>(g_GetQuarkSystemTable() + kQuarkGameObjects);
            send = *reinterpret_cast<SendGameObjectMessage_t*>(*static_cast<std::uint8_t**>(gameObjects) + kGameObjectsSendMessage);
            vehicle = static_cast<std::uint16_t>(
                (*reinterpret_cast<const std::uint16_t*>(static_cast<const std::uint8_t*>(commInfo) + kCommInfoVehicleIndex) & kIndexMask) | kVehicleIdBase);
            return gameObjects && send;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    // The four seat riders get the jeep's blast damage passed on; the gunner stands in the open and gets it too
    void __fastcall hk_TransmitDamageToRiders(void* commInfo, std::uint16_t attacker, std::uint32_t attackId, std::uint32_t damage)
    {
        MISSION_GUARD_ORIGINAL_VOID(g_OrigTransmitDamage, commInfo, attacker, attackId, damage);
        g_OrigTransmitDamage(commInfo, attacker, attackId, damage);
        if (!JeepGunner_IsArmedLightVehicle(commInfo))
            return;
        const std::uint8_t* vehicleSystem = VehicleSystemSEH();
        const std::uint16_t gunner = vehicleSystem ? GunnerOfVehicleSEH(vehicleSystem, commInfo) : kNoRider;
        void* gameObjects = nullptr;
        SendGameObjectMessage_t send = nullptr;
        std::uint16_t vehicle = 0;
        if (gunner == kNoRider || !ReadMessageSenderSEH(gameObjects, send, vehicle, commInfo))
            return;
        RiderDamageMessage message{ kRiderDamageMessage, attacker, vehicle, attackId, damage };
        std::uint64_t result = 0;
        send(gameObjects, &result, gunner, &message);
    }

    enum class SeatChoice
    {
        Vanilla,
        Gunner,
        Refused,
    };

    // The first soldier after the driver takes the gun: passenger slot 4 with seat 5. Hostages ride seats 1-3 only.
    SeatChoice ChooseSeatSEH(std::uint8_t* passengers, std::uint16_t vehicle, std::uint16_t rider, std::uint8_t* seat, std::uint8_t slotFlags)
    {
        __try
        {
            if ((vehicle & kTypeMask) != kVehicleIdBase || (*seat != kAnySeat && *seat != kJeepGunnerSeat))
                return SeatChoice::Vanilla;
            auto* entries = *reinterpret_cast<std::uint8_t**>(passengers + kPassengerEntries);
            std::uint8_t* entry = nullptr;
            for (std::uint32_t i = 0; i < passengers[kPassengerEntryCount] && !entry; ++i)
            {
                if (*reinterpret_cast<std::uint16_t*>(entries + i * kPassengerEntrySize) == vehicle)
                    entry = entries + i * kPassengerEntrySize;
            }
            if (!entry || entry[kEntrySlotCount] <= kGunnerSlot)
                return SeatChoice::Vanilla;
            std::uint8_t* slots = entry + kEntrySlots;
            for (std::uint32_t slot = 0; slot < entry[kEntrySlotCount]; ++slot)
            {
                if (*reinterpret_cast<std::uint16_t*>(slots + slot * kSlotSize) == rider)
                    return SeatChoice::Vanilla; // aboard already: vanilla hands back his seat
            }
            if (!JeepGunner_IsArmedLightVehicle(CommInfoOfVehicleSEH(vehicle)))
                return SeatChoice::Vanilla;

            std::uint8_t* gunnerSlot = slots + kGunnerSlot * kSlotSize;
            const bool soldier = (rider & kTypeMask) == kSoldierType;
            const bool driverAboard = *reinterpret_cast<std::uint16_t*>(slots) != kNoRider;
            if (soldier && *reinterpret_cast<std::uint16_t*>(gunnerSlot) == kNoRider && (*seat == kJeepGunnerSeat || driverAboard))
            {
                *reinterpret_cast<std::uint16_t*>(gunnerSlot) = rider;
                gunnerSlot[kSlotState] = kSlotTaken;
                gunnerSlot[kSlotSeat] = kJeepGunnerSeat;
                gunnerSlot[kSlotFlags] = static_cast<std::uint8_t>((gunnerSlot[kSlotFlags] | slotFlags) & kSlotFlagsKept);
                *seat = kJeepGunnerSeat;
                return SeatChoice::Gunner;
            }
            if (*seat == kJeepGunnerSeat)
                return SeatChoice::Refused;
            if (soldier)
                return SeatChoice::Vanilla;
            for (std::uint32_t slot = 0; slot < kSeatSlots; ++slot)
            {
                if (*reinterpret_cast<std::uint16_t*>(slots + slot * kSlotSize) == kNoRider)
                    return SeatChoice::Vanilla;
            }
            return SeatChoice::Refused;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return SeatChoice::Vanilla;
        }
    }

    bool __fastcall hk_AddPassenger(void* passengers, std::uint16_t vehicle, std::uint16_t rider, std::uint8_t* seat,
                                    bool slotFlag1, bool slotFlag2, bool slotFlag4)
    {
        MISSION_GUARD_ORIGINAL_RET(g_OrigAddPassenger, passengers, vehicle, rider, seat, slotFlag1, slotFlag2, slotFlag4);
        const std::uint8_t slotFlags = static_cast<std::uint8_t>((slotFlag1 ? 1 : 0) | (slotFlag2 ? 2 : 0) | (slotFlag4 ? 4 : 0));
        switch (seat ? ChooseSeatSEH(static_cast<std::uint8_t*>(passengers), vehicle, rider, seat, slotFlags) : SeatChoice::Vanilla)
        {
        case SeatChoice::Gunner:
#ifdef _DEBUG
            LogDebug("JeepGunner probe: soldier 0x%04X gets the gun of vehicle 0x%04X (passenger slot 4, seat 5)", rider, vehicle);
#endif
            return true;
        case SeatChoice::Refused:
#ifdef _DEBUG
            if (g_ProbeRefused.exchange(static_cast<std::uint32_t>(vehicle) << 16 | rider, std::memory_order_relaxed)
                != (static_cast<std::uint32_t>(vehicle) << 16 | rider))
                LogDebug("JeepGunner probe: rider 0x%04X finds no free seat on vehicle 0x%04X", rider, vehicle);
#endif
            return false;
        default:
            return g_OrigAddPassenger(passengers, vehicle, rider, seat, slotFlag1, slotFlag2, slotFlag4);
        }
    }

    bool IsSeatedGunnerSEH(const std::uint8_t* passengers, std::uint16_t vehicle, std::uint16_t rider)
    {
        __try
        {
            const auto* entries = *reinterpret_cast<const std::uint8_t* const*>(passengers + kPassengerEntries);
            for (std::uint32_t i = 0; i < passengers[kPassengerEntryCount]; ++i)
            {
                const std::uint8_t* entry = entries + i * kPassengerEntrySize;
                if (*reinterpret_cast<const std::uint16_t*>(entry) != vehicle)
                    continue;
                const std::uint8_t* gunnerSlot = entry + kEntrySlots + kGunnerSlot * kSlotSize;
                return entry[kEntrySlotCount] > kGunnerSlot && *reinterpret_cast<const std::uint16_t*>(gunnerSlot) == rider
                    && gunnerSlot[kSlotSeat] == kJeepGunnerSeat;
            }
            return false;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    // Vanilla slides a passenger down when a lower slot frees up: the gunner keeps the gun
    bool __fastcall hk_UpdateReserveSeatIndex(void* passengers, std::uint16_t vehicle, std::uint16_t rider, std::uint8_t* seat)
    {
        MISSION_GUARD_ORIGINAL_RET(g_OrigUpdateReserveSeatIndex, passengers, vehicle, rider, seat);
        if (IsSeatedGunnerSEH(static_cast<const std::uint8_t*>(passengers), vehicle, rider))
            return false;
        return g_OrigUpdateReserveSeatIndex(passengers, vehicle, rider, seat);
    }

    struct SeatHook
    {
        const char*                                name;
        uintptr_t AddressSetRuntime::AddressSet::* address;
        const std::uint8_t*                        prologue;
        std::size_t                                size;
        void*                                      detour;
        void**                                     original;
        bool                                       required;
        void*                                      target;
    };

    SeatHook g_Hooks[] = {
        { "JeepGunner: vehicle SystemImpl::TryToRide", &AddressSetRuntime::AddressSet::VehicleSystemImpl_TryToRide,
          kTryToRidePrologue, sizeof(kTryToRidePrologue),
          reinterpret_cast<void*>(&hk_TryToRide), reinterpret_cast<void**>(&g_OrigTryToRide), true, nullptr },
        { "JeepGunner: vehicle SystemImpl::GetRiderOff", &AddressSetRuntime::AddressSet::VehicleSystemImpl_GetRiderOff,
          kGetRiderOffPrologue, sizeof(kGetRiderOffPrologue),
          reinterpret_cast<void*>(&hk_GetRiderOff), reinterpret_cast<void**>(&g_OrigGetRiderOff), true, nullptr },
        { "JeepGunner: PassengerInfoSystemImpl::UpdateReserveSeatIndex", &AddressSetRuntime::AddressSet::PassengerInfoSystemImpl_UpdateReserveSeatIndex,
          kUpdateReserveSeatPrologue, sizeof(kUpdateReserveSeatPrologue),
          reinterpret_cast<void*>(&hk_UpdateReserveSeatIndex), reinterpret_cast<void**>(&g_OrigUpdateReserveSeatIndex), true, nullptr },
        { "JeepGunner: VehicleCommunicationInfo::TransmitDamageToRiders", &AddressSetRuntime::AddressSet::VehicleCommunicationInfo_TransmitDamageToRiders,
          kTransmitDamagePrologue, sizeof(kTransmitDamagePrologue),
          reinterpret_cast<void*>(&hk_TransmitDamageToRiders), reinterpret_cast<void**>(&g_OrigTransmitDamage), false, nullptr },
        { "JeepGunner: PassengerInfoSystemImpl::AddPassenger", &AddressSetRuntime::AddressSet::PassengerInfoSystemImpl_AddPassenger,
          kAddPassengerPrologue, sizeof(kAddPassengerPrologue),
          reinterpret_cast<void*>(&hk_AddPassenger), reinterpret_cast<void**>(&g_OrigAddPassenger), true, nullptr },
    };
}

bool JeepGunnerSeat_Install(bool seatReadersHooked)
{
    if (!seatReadersHooked)
        return false;
    void* registerRider = ResolveGameAddress(gAddr.VehicleSystemImpl_RegisterExtendedRider);
    void* unregisterRider = ResolveGameAddress(gAddr.VehicleSystemImpl_UnregisterExtendedRider);
    g_GetQuarkSystemTable = reinterpret_cast<GetQuarkSystemTable_t>(ResolveGameAddress(gAddr.GetQuarkSystemTable));
    if (!g_GetQuarkSystemTable
        || CheckHookEntry(registerRider, kRegisterExtendedRiderPrologue, sizeof(kRegisterExtendedRiderPrologue)) == HookEntry::Unknown
        || CheckHookEntry(unregisterRider, kUnregisterExtendedRiderPrologue, sizeof(kUnregisterExtendedRiderPrologue)) == HookEntry::Unknown)
    {
        LogError("JeepGunner: vehicle extended rider code does not match %s, armed jeeps carry no gunner", GetGameBuildName(gGameBuild));
        return false;
    }
    g_RegisterExtendedRider = reinterpret_cast<ExtendedRiderCall_t>(registerRider);
    g_UnregisterExtendedRider = reinterpret_cast<ExtendedRiderCall_t>(unregisterRider);

    for (SeatHook& hook : g_Hooks)
    {
        if (hook.target)
            continue;
        void* target = ResolveGameAddress(gAddr.*hook.address);
        const HookEntry entry = target ? CheckHookEntry(target, hook.prologue, hook.size) : HookEntry::Unknown;
        const bool hooked = entry != HookEntry::Unknown && CreateAndEnableHook(target, hook.detour, hook.original);
        if (!hooked && hook.required)
        {
            LogError("%s: code does not match %s or the hook failed, armed jeeps carry no gunner", hook.name, GetGameBuildName(gGameBuild));
            JeepGunnerSeat_Uninstall();
            return false;
        }
        if (!hooked)
        {
            LogWarn("%s: code does not match %s or the hook failed, the jeep gunner takes no blast damage through the jeep",
                    hook.name, GetGameBuildName(gGameBuild));
            continue;
        }
        if (entry == HookEntry::ForeignHook)
            LogHookedOnTop(hook.name, target);
        hook.target = target;
    }
    return true;
}

void JeepGunnerSeat_Uninstall()
{
    for (auto it = std::rbegin(g_Hooks); it != std::rend(g_Hooks); ++it)
    {
        if (!it->target)
            continue;
        DisableAndRemoveHook(it->target);
        it->target = nullptr;
        *it->original = nullptr;
    }
}
