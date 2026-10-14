#include "pch.h"

#include "BuddyOutfitEquip.h"

#include <Windows.h>
#include <cstdint>

#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"

namespace buddyoutfit
{
    namespace
    {
        constexpr std::size_t kVtblIsEnableBuddyReturn   = 0x8;
        constexpr std::size_t kVtblIsEnableChangeEquip   = 0x20;
        constexpr std::size_t kVtblGetActiveBuddyType    = 0x128;
        constexpr std::size_t kVtblBuddyEquipSettings    = 0x1E8;
        constexpr std::size_t kVtblBuddyChangeEquipInGame = 0x1F8;
        constexpr std::size_t kVtblChangeQuietWeaponType = 0x510;
        constexpr std::size_t kVtblChangeQuietCostume    = 0x518;

        constexpr std::size_t kVarHorseColor = 0x2E62;
        constexpr std::size_t kVarHorseHead  = 0x2E63;
        constexpr std::size_t kVarHorseFoot  = 0x2E64;
        constexpr std::size_t kVarDogEye     = 0x2E4A;
        constexpr std::size_t kVarDogColor   = 0x2E4B;

        constexpr std::size_t kParamBuddyType = 0x14; // ModelViewerParam
        constexpr std::size_t kParamColor     = 0x18;
        constexpr std::size_t kParamValue     = 0x1C;
        constexpr std::size_t kParamSecond    = 0x20; // head / eye / Quiet costume
        constexpr std::size_t kParamFoot      = 0x24;

        constexpr std::uint16_t kHeliSpaceBand = 4; // missionCode / 10000 == 4 is the heli and ACC

        using NoArg_t        = std::uint64_t(__fastcall*)(void* svc);
        using EquipSettings_t = std::uint64_t(__fastcall*)(void* svc, std::uint8_t buddyType, void* param);
        using InGame_t       = void(__fastcall*)(void* svc, void* param);
        using U8Arg_t        = std::uint64_t(__fastcall*)(void* svc, std::uint8_t value);
        using U16Arg_t       = void(__fastcall*)(void* svc, std::uint16_t value);
        using ChangeHorse_t  = void(__fastcall*)(void* svc, std::uint8_t color, std::uint8_t type,
                                                 std::uint8_t head, std::uint8_t foot, bool resync);
        using ChangeDog_t    = void(__fastcall*)(void* svc, std::uint8_t color, std::uint8_t gear,
                                                 std::uint8_t eye, bool resync);

        template <typename Fn>
        Fn Slot(void** vtbl, std::size_t offset)
        {
            return reinterpret_cast<Fn>(vtbl[offset / sizeof(void*)]);
        }

        bool EquipSEH(void* svc, std::uint8_t* vars, Kind kind, std::uint8_t value,
                      ChangeHorse_t changeHorse, ChangeDog_t changeDog)
        {
            __try
            {
                void** vtbl = *reinterpret_cast<void***>(svc);
                if (!vtbl)
                    return false;

                const std::uint8_t buddyType = BuddyTypeOf(kind);
                const auto active = static_cast<std::uint8_t>(Slot<NoArg_t>(vtbl, kVtblGetActiveBuddyType)(svc));
                if (active == buddyType)
                {
                    if (!static_cast<std::uint8_t>(Slot<NoArg_t>(vtbl, kVtblIsEnableBuddyReturn)(svc))
                        || !static_cast<std::uint8_t>(Slot<NoArg_t>(vtbl, kVtblIsEnableChangeEquip)(svc)))
                        return false;

                    alignas(16) std::uint8_t p[0x100] = {};
                    p[kParamBuddyType] = buddyType;
                    switch (kind)
                    {
                    case Kind::HorseTack:
                        p[kParamColor]  = vars[kVarHorseColor];
                        p[kParamValue]  = value;
                        p[kParamSecond] = vars[kVarHorseHead];
                        p[kParamFoot]   = vars[kVarHorseFoot];
                        break;
                    case Kind::DogGear:
                        p[kParamColor]  = vars[kVarDogColor];
                        p[kParamValue]  = value;
                        p[kParamSecond] = vars[kVarDogEye];
                        break;
                    case Kind::QuietCostume:
                        *reinterpret_cast<std::uint16_t*>(p + kParamValue) =
                            *reinterpret_cast<std::uint16_t*>(vars + kVarQuietWeapon);
                        p[kParamSecond] = value;
                        break;
                    case Kind::QuietWeapon:
                        *reinterpret_cast<std::uint16_t*>(p + kParamValue) = value;
                        p[kParamSecond] = vars[kVarQuietCostume];
                        break;
                    default:
                        return false;
                    }

                    const std::uint16_t mission = *reinterpret_cast<std::uint16_t*>(vars + kVarMissionCode);
                    if (mission / 10000 == kHeliSpaceBand)
                        return static_cast<std::uint8_t>(
                            Slot<EquipSettings_t>(vtbl, kVtblBuddyEquipSettings)(svc, buddyType, p)) != 0;
                    Slot<InGame_t>(vtbl, kVtblBuddyChangeEquipInGame)(svc, p);
                    return true;
                }

                switch (kind)
                {
                case Kind::QuietCostume:
                    Slot<U8Arg_t>(vtbl, kVtblChangeQuietCostume)(svc, value);
                    return vars[kVarQuietCostume] == value;
                case Kind::QuietWeapon:
                    Slot<U16Arg_t>(vtbl, kVtblChangeQuietWeaponType)(svc, value);
                    return *reinterpret_cast<std::uint16_t*>(vars + kVarQuietWeapon) == value;
                case Kind::HorseTack:
                    if (!changeHorse)
                        return false;
                    changeHorse(svc, vars[kVarHorseColor], value, vars[kVarHorseHead], vars[kVarHorseFoot], false);
                    return vars[kVarHorseTack] == value;
                case Kind::DogGear:
                    if (!changeDog)
                        return false;
                    changeDog(svc, vars[kVarDogColor], value, vars[kVarDogEye], false);
                    return vars[kVarDogGear] == value;
                default:
                    return false;
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return false;
            }
        }
    }

    bool EquipNow(const Entry& entry)
    {
        if (MissionCodeGuard::ShouldBypassHooks())
            return false;
        std::uint8_t* vars = ReadScriptVars();
        void* svc = ResolveGameAddress(gAddr.BuddyServiceImpl_Instance);
        if (!vars || !svc)
            return false;
        return EquipSEH(svc, vars, entry.kind, entry.value,
                        reinterpret_cast<ChangeHorse_t>(ResolveGameAddress(gAddr.BuddyServiceImpl_ChangeHorseSettings)),
                        reinterpret_cast<ChangeDog_t>(ResolveGameAddress(gAddr.BuddyServiceImpl_ChangeDogSettings)));
    }
}
