#include <pch.h>
#include "TppPermanentGimmickUnit_CallNoise.h"

#include <Windows.h>
#include <array>
#include <cstdint>
#include <mutex>

#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "log.h"

namespace
{
    using CallNoise_t           = void(__fastcall*)(std::uint8_t* unit, std::uint8_t power, const float* position, std::uint16_t breaker);
    using GetQuarkSystemTable_t = std::uint8_t* (__fastcall*)();
    using GetCommunication_t    = const std::int32_t* (__fastcall*)(void* vehicles, std::int32_t* result, std::uint8_t** info,
                                                                    std::uint32_t index);
    using GetWalkerGear_t       = std::uint8_t* (__fastcall*)(void* walkerGears, std::uint16_t gameObjectId);

    constexpr std::uint8_t   kFacilityNoise        = 0x49;   // watch tower, bridge: a guard who hears it radios CPR0144
    constexpr std::uint8_t   kAntiAirGunNoise      = 0x48;   // these four: CPR0145
    constexpr std::uint8_t   kLoudEquipmentNoise   = 0x4A;
    constexpr std::uint8_t   kEquipmentNoise       = 0x34;
    constexpr std::uint8_t   kTimedEquipmentNoise  = 0x15;   // water: its break waits two seconds

    constexpr std::uint16_t  kTypeShift            = 9;
    constexpr std::uint16_t  kIndexMask            = 0x1FF;
    constexpr std::uint16_t  kNoGameObject         = 0xFFFF;
    constexpr std::uint16_t  kPlayerType           = 0x00;
    constexpr std::uint16_t  kQuietType            = 0x12;
    constexpr std::uint16_t  kPlayerOtherType      = 0x13;
    constexpr std::uint16_t  kSupportHeliType      = 0x0E;     // TppHeli2, only ever "SupportHeli"; enemy helis are 0x0F-0x11
    constexpr std::uint16_t  kWalkerGearType       = 0x2C;
    constexpr std::uint16_t  kVehicleType          = 0x32;
    constexpr std::uint16_t  kImportantBreakable   = 0x4F;     // antennas, AA radars, generators, switchboards, comms gear
    constexpr std::uint16_t  kSnake                = 0x0000;   // the player's own GameObjectId outside FOB

    constexpr std::size_t    kUnitGameObjectId     = 0x08;
    constexpr std::size_t    kUnitEspionage        = 0xD8;     // ImportantBreakable flag1, what GetEspionageType reads
    constexpr std::uint32_t  kEspionageMask        = 0xF0000;
    constexpr std::uint32_t  kCommsEquipment       = 0x10000;  // espionage type 6, the CMMN models
    constexpr std::uint32_t  kGenerator            = 0x20000;  // espionage type 7, GNRT
    constexpr std::uint32_t  kAntenna              = 0x40000;  // espionage type 9, ANTN

    constexpr std::size_t    kQuarkTableApplication = 0x98;
    constexpr std::size_t    kApplicationVehicles   = 0x2A8;   // as GimmickUtility::IsPlayerVehicle reaches them
    constexpr std::size_t    kGetCommunicationSlot  = 3;
    constexpr std::size_t    kVehicleDriver         = 0x30;
    constexpr std::size_t    kApplicationWalkers    = 0x2C0;   // as GimmickUtility::IsPlayerWalkerGeer reaches them
    constexpr std::size_t    kGetWalkerGearSlot     = 5;
    constexpr std::size_t    kWalkerGearPilot       = 0x2C8;

    constexpr std::size_t    kBreaksKept            = 8;
    constexpr std::uint64_t  kBreakHeardMs          = 60000;   // long enough for the guard who heard it to reach his radio step
    constexpr float          kSameNoiseRange        = 1.0f;    // his knowledge keeps the noise's own position

    struct EquipmentBreak
    {
        float              at[3] = {};
        std::uint64_t      ms    = 0;
        DestroyedEquipment what  = DestroyedEquipment::Other;
    };

    CallNoise_t                                g_OrigCallNoise = nullptr;
    std::mutex                                 g_BreaksMutex;
    std::array<EquipmentBreak, kBreaksKept>    g_Breaks{};
    std::size_t                                g_NextBreak = 0;

    bool ReportedAsDestroyed(std::uint8_t power)   // Soldier2NoticeThreatUtil::UpdateNotice makes CPR0144 / CPR0145 of these
    {
        return power == kFacilityNoise || power == kAntiAirGunNoise || power == kLoudEquipmentNoise || power == kEquipmentNoise
            || power == kTimedEquipmentNoise;
    }

    std::uint8_t* Application()
    {
        const auto quarkTable = reinterpret_cast<GetQuarkSystemTable_t>(ResolveGameAddress(gAddr.Fox_GetQuarkSystemTable));
        std::uint8_t* table = quarkTable ? quarkTable() : nullptr;
        return table ? *reinterpret_cast<std::uint8_t* const*>(table + kQuarkTableApplication) : nullptr;
    }

    std::uint16_t VehicleDriver(std::uint16_t vehicle)
    {
        __try
        {
            std::uint8_t* application = Application();
            void* vehicles = application ? *reinterpret_cast<void* const*>(application + kApplicationVehicles) : nullptr;
            if (!vehicles)
                return kNoGameObject;
            const auto getCommunication = (*reinterpret_cast<GetCommunication_t* const*>(vehicles))[kGetCommunicationSlot];
            std::int32_t result = -1;
            std::uint8_t* info = nullptr;
            const std::int32_t* found = getCommunication(vehicles, &result, &info, vehicle & kIndexMask);
            if (!found || *found < 0 || !info)
                return kNoGameObject;
            return *reinterpret_cast<const std::uint16_t*>(info + kVehicleDriver);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return kNoGameObject;
        }
    }

    std::uint16_t WalkerGearPilot(std::uint16_t walkerGear)
    {
        __try
        {
            std::uint8_t* application = Application();
            void* walkerGears = application ? *reinterpret_cast<void* const*>(application + kApplicationWalkers) : nullptr;
            if (!walkerGears)
                return kNoGameObject;
            const auto getWalkerGear = (*reinterpret_cast<GetWalkerGear_t* const*>(walkerGears))[kGetWalkerGearSlot];
            const std::uint8_t* walker = getWalkerGear(walkerGears, walkerGear);
            return walker ? *reinterpret_cast<const std::uint16_t*>(walker + kWalkerGearPilot) : kNoGameObject;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return kNoGameObject;
        }
    }

    std::uint16_t PlayerBehind(std::uint16_t breaker, const char*& how)   // user: his vehicle, his D-Walker, his support heli count as him
    {
        const std::uint16_t type = breaker >> kTypeShift;
        how = nullptr;
        if (type == kPlayerType || type == kQuietType || type == kPlayerOtherType)
            return breaker;
        if (type == kVehicleType)
        {
            const std::uint16_t driver = VehicleDriver(breaker);
            if (driver != kNoGameObject && (driver >> kTypeShift) == kPlayerType)
            {
                how = "the vehicle he drives";
                return driver;
            }
        }
        else if (type == kWalkerGearType)
        {
            const std::uint16_t pilot = WalkerGearPilot(breaker);
            if (pilot != kNoGameObject && (pilot >> kTypeShift) == kPlayerType)
            {
                how = "his D-Walker";
                return pilot;
            }
        }
        else if (type == kSupportHeliType)
        {
            how = "his support heli";
            return kSnake;
        }
        return breaker;
    }

    bool ReadUnit(const std::uint8_t* unit, std::uint16_t& gameObjectId, std::uint32_t& espionage)
    {
        __try
        {
            gameObjectId = *reinterpret_cast<const std::uint16_t*>(unit + kUnitGameObjectId);
            espionage = *reinterpret_cast<const std::uint32_t*>(unit + kUnitEspionage) & kEspionageMask;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    DestroyedEquipment EquipmentOf(std::uint16_t gameObjectId, std::uint32_t espionage)
    {
        if ((gameObjectId >> kTypeShift) != kImportantBreakable)
            return DestroyedEquipment::Other;
        if (espionage == kCommsEquipment || espionage == kAntenna)
            return DestroyedEquipment::Comms;
        if (espionage == kGenerator)
            return DestroyedEquipment::PowerSupply;
        return DestroyedEquipment::Other;
    }

    void NoteBreak(DestroyedEquipment what, std::uint16_t gameObjectId, const float* position)
    {
        {
            std::lock_guard<std::mutex> lock(g_BreaksMutex);
            EquipmentBreak& slot = g_Breaks[g_NextBreak];
            slot.at[0] = position[0];
            slot.at[1] = position[1];
            slot.at[2] = position[2];
            slot.ms = GetTickCount64();
            slot.what = what;
            g_NextBreak = (g_NextBreak + 1) % kBreaksKept;
        }
    }

    void __fastcall hk_CallNoise(std::uint8_t* unit, std::uint8_t power, const float* position, std::uint16_t breaker)
    {
        std::uint16_t gameObjectId = kNoGameObject;
        std::uint32_t espionage = 0;
        if (!ReportedAsDestroyed(power) || !position || MissionCodeGuard::ShouldBypassHooks() || !ReadUnit(unit, gameObjectId, espionage))
        {
            g_OrigCallNoise(unit, power, position, breaker);
            return;
        }
        const char* how = nullptr;
        const std::uint16_t source = PlayerBehind(breaker, how);
        const DestroyedEquipment what = EquipmentOf(gameObjectId, espionage);
        if (what != DestroyedEquipment::Other)
            NoteBreak(what, gameObjectId, position);
        g_OrigCallNoise(unit, power, position, source);
    }
}

DestroyedEquipment TppPermanentGimmickUnit_DestroyedAt(const float spot[3])
{
    const std::uint64_t now = GetTickCount64();
    std::lock_guard<std::mutex> lock(g_BreaksMutex);
    for (const EquipmentBreak& broken : g_Breaks)
    {
        if (!broken.ms || now - broken.ms > kBreakHeardMs)
            continue;
        const float dx = broken.at[0] - spot[0];
        const float dy = broken.at[1] - spot[1];
        const float dz = broken.at[2] - spot[2];
        if (dx * dx + dy * dy + dz * dz < kSameNoiseRange * kSameNoiseRange)
            return broken.what;
    }
    return DestroyedEquipment::Other;
}

void TppPermanentGimmickUnit_ClearForReload()
{
    std::lock_guard<std::mutex> lock(g_BreaksMutex);
    g_Breaks.fill(EquipmentBreak{});
    g_NextBreak = 0;
}

bool Install_TppPermanentGimmickUnit_CallNoise_Hook()
{
    void* target = ResolveGameAddress(gAddr.TppPermanentGimmickUnit_CallNoise);
    if (!target)
    {
        LogError("CpRadioVariants: TppPermanentGimmickUnit::CallNoise address missing on this build, no CPR0345 / CPR0346 and no destroyed-object call for the player's vehicle, D-Walker or support heli");
        return false;
    }
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hk_CallNoise), reinterpret_cast<void**>(&g_OrigCallNoise)))
    {
        LogError("CpRadioVariants: TppPermanentGimmickUnit::CallNoise hook failed, no CPR0345 / CPR0346 and no destroyed-object call for the player's vehicle, D-Walker or support heli");
        return false;
    }
    return true;
}

bool Uninstall_TppPermanentGimmickUnit_CallNoise_Hook()
{
    if (g_OrigCallNoise)
    {
        if (void* target = ResolveGameAddress(gAddr.TppPermanentGimmickUnit_CallNoise))
            DisableAndRemoveHook(target);
    }
    g_OrigCallNoise = nullptr;
    std::lock_guard<std::mutex> lock(g_BreaksMutex);
    g_Breaks.fill(EquipmentBreak{});
    g_NextBreak = 0;
    return true;
}
