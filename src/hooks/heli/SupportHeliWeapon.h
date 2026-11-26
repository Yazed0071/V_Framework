#pragma once

#include <cstdint>

struct lua_State;

namespace SupportHeliWeapon
{
    struct Weapon
    {
        std::uint8_t  customId      = 0;
        std::uint32_t developId     = 0;
        std::uint8_t  fallbackLevel = 0;
        std::uint8_t  fireLevel     = 0;
        bool          isMissile     = false;
        std::int32_t  ammo          = 0;
        std::uint16_t attackId      = 0;
        std::uint16_t shellId       = 0;
        std::int32_t  shotCount     = 0;
        float         shotInterval  = -1.0f;
        float         windup        = -1.0f;
        float         cooldown      = -1.0f;
        bool          hasPodMask    = false;
        std::uint8_t  podMask       = 0;
        std::int32_t  blastId       = -1;
        std::uint16_t cloudShell    = 0;   // non-zero: blastId is a gas or smoke row, the shell lands as this cloud shell
        bool          silentAttack  = false;
        std::uint32_t attackLine    = 0;   // 0: the pilot keeps the vanilla rocket or missile line
        std::uint32_t attackLineType = 0;
    };

    constexpr std::uint8_t  kFirstPodPart     = 8;
    constexpr std::uint8_t  kPodPartCount     = 8;
    constexpr std::uint16_t kRocketShell      = 0x134;   // EQP_BL_uth0_ammo1
    constexpr std::uint16_t kMissileShell     = 0x133;   // EQP_BL_uth0_ammo0
    constexpr std::uint16_t kSmokeCloudShell  = 0x111;   // EQP_BL_40mmSmoke
    constexpr std::uint16_t kSleepCloudShell  = 0x113;   // EQP_BL_40mmSleep
    constexpr std::int32_t  kBlaSmokeGrenade       = 14;   // TppEquip.BLA_SmokeGrenade
    constexpr std::int32_t  kBlaSleepingGusGrenade = 20;   // TppEquip.BLA_SleepingGusGrenade
    constexpr std::uint32_t kPilotVoice       = 0;       // the cabin voice event, SHV lines
    constexpr std::uint32_t kPilotRadio       = 1;       // the radio event, SHR lines
    constexpr std::uint32_t kRocketAttackLine  = 0x1E754998u;   // SHR0240
    constexpr std::uint32_t kMissileAttackLine = 0x1F754B0Bu;   // SHR0250
    constexpr std::uint32_t kCustomIdShift    = 17;
    constexpr std::uint32_t kCustomIdMask     = 0xFF;
    constexpr std::uint32_t kVanillaFieldMask = 0x1FFFF;

    int __cdecl Lua_RegisterSupportHeliWeapon(lua_State* L);
    bool        RegisterRocket(const char* key, std::int32_t ammo, int podCount, std::int32_t blastId);   // RegisterSupportHeliWeapon(key, { type = "Rocket", ammo, podCount, blastId }) for the rockets the DLL ships

    std::uint16_t ShellAttackId(const Weapon& weapon);
    bool          ReadWeaponBlastRow(const Weapon& weapon, float& radius, std::uint8_t& blastFlag);
    bool          SortieAttackLine(std::uint32_t& line, std::uint32_t& lineType);   // false: the pilot says nothing for this attack

    bool FindByDevelopId(std::uint32_t developId, Weapon& out);
    bool FindByCustomId(std::uint8_t customId, Weapon& out);

    void         NoteMenuChoice(std::uint32_t developId);
    std::uint8_t MenuChoiceForSortie(std::uint8_t menuLevel);
    std::uint8_t ReadMenuSubWeaponLevel();

    void         SetSortieWeapon(std::uint8_t customId);
    bool         GetSortieWeapon(Weapon& out);
    std::uint8_t ReadSortieCustomId();
}
