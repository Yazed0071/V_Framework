#include <pch.h>
#include "SupportHeliBuiltInWeapons.h"

#include <cstdint>
#include <iterator>

#include "FeatureModule.h"
#include "SupportHeliWeapon.h"
#include "V_FrameWorkState.h"
#include "log.h"
#include "../equip/EquipDevelop_AddToEquipDevelopTable.h"

namespace
{
    struct RocketGrade
    {
        int          grade;
        int          developGmpCost;
        int          usageGmpCost;
        int          sectionLvForDevelop;
        int          developTimeMinute;
        std::int32_t ammo;       // volleys
        int          podCount;
    };

    constexpr int kDevelopTypeHeli    = 31;      // TppMbDev.EQP_DEV_TYPE_Heli
    constexpr int kRocketDevelopGroup = 141;     // TppMbDev.EQP_DEV_GROUP_OTHER_050
    constexpr int kRocketDevelopId    = 34002;   // H-ROCKET, the row the first grade develops from
    constexpr int kNoBluePrint        = 65535;

    constexpr RocketGrade kFirstGrade  = { 2, 120000, 1000, 10, 0, 4, 2 };    // H-ROCKET 34002's cost and pods; lands one grade after it
    constexpr RocketGrade kSecondGrade = { 4, 400000, 3400, 34, 36, 8, 4 };   // H-ROCKET 34004's cost, develop time and pods

    bool RegisterRocket(const char* key, int baseDevelopId, const char* abilityText, std::int32_t blastId, const RocketGrade& grade)
    {
        const EquipDevelopAdd::NativeDevelopNumber constNumbers[] =
        {
            { "p02", kDevelopTypeHeli },
            { "p03", baseDevelopId },
            { "p05", kNoBluePrint },
            { "p09", kRocketDevelopGroup },
            { "p34", 1 },
        };
        const EquipDevelopAdd::NativeDevelopString constStrings[] =
        {
            { "p06", "name_hl_34002" },
            { "p07", "info_hl_34002" },
            { "p08", "/Assets/tpp/ui/texture/EquipIcon/mecha/ui_hl_uth0_rckt_00_10_alp" },
            { "p10", abilityText },
            { "p30", "name_hl_34002" },
        };
        const EquipDevelopAdd::NativeDevelopNumber flowNumbers[] =
        {
            { "p52", grade.grade },
            { "p53", grade.developGmpCost },
            { "p54", grade.usageGmpCost },
            { "p55", grade.sectionLvForDevelop },
            { "p71", grade.developTimeMinute },
        };

        if (!EquipDevelopAdd::QueueNativeDevelopRequest(key,
                constNumbers, std::size(constNumbers),
                constStrings, std::size(constStrings),
                flowNumbers, std::size(flowNumbers)))
        {
            LogWarn("SupportHeliWeapon: built-in '%s' got no R&D row, it is off", key);
            return false;
        }
        if (!SupportHeliWeapon::RegisterRocket(key, grade.ammo, grade.podCount, blastId))
            LogWarn("SupportHeliWeapon: built-in '%s' got no weapon id, it fires like H-ROCKET", key);
        return true;
    }

    void RegisterRocketLine(const char* firstKey, const char* secondKey, const char* abilityText, std::int32_t blastId)
    {
        if (!RegisterRocket(firstKey, kRocketDevelopId, abilityText, blastId, kFirstGrade))
            return;
        const std::int32_t firstDevelopId = V_FrameWorkState::GetDevelopIdByKey(firstKey);
        if (firstDevelopId <= 0)
        {
            LogWarn("SupportHeliWeapon: built-in '%s' has no develop id yet, '%s' is off", firstKey, secondKey);
            return;
        }
        RegisterRocket(secondKey, firstDevelopId, abilityText, blastId, kSecondGrade);
    }
}

namespace SupportHeliBuiltInWeapons
{
    void RegisterEarly()
    {
        if (FeatureIsDisabled("SupportHeliWeapon"))
            return;
        RegisterRocketLine("V_Framework:PequodSleepingGasRockets", "V_Framework:PequodSleepingGasRocketsGrade4",
                           "ability_0035", SupportHeliWeapon::kBlaSleepingGusGrenade);   // "Sleeping Gas Round Conversion (Attack Type: Non-lethal)"
        RegisterRocketLine("V_Framework:PequodSmokeRockets", "V_Framework:PequodSmokeRocketsGrade4",
                           "ability_0025", SupportHeliWeapon::kBlaSmokeGrenade);         // "Smoke Round Conversion (Attack Type: Non-lethal)"
    }
}
