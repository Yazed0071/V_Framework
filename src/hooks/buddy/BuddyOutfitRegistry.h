#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace buddyoutfit
{
    enum class Kind : std::uint8_t
    {
        QuietCostume,
        HorseTack,
        DogGear,
        QuietWeapon,
        Count
    };

    constexpr std::uint8_t kBuddyTypeHorse = 1;
    constexpr std::uint8_t kBuddyTypeDog   = 2;
    constexpr std::uint8_t kBuddyTypeQuiet = 3;

    constexpr std::size_t kValueBitWords     = 256 / 32;
    constexpr std::size_t kDevelopIdBitWords = 65536 / 32;

    constexpr std::size_t kVarQuietWeapon  = 0x2E44;
    constexpr std::size_t kVarQuietCostume = 0x2E46;
    constexpr std::size_t kVarDogGear      = 0x2E49;
    constexpr std::size_t kVarHorseTack    = 0x2E61;
    constexpr std::size_t kVarMissionCode  = 0x12;

    constexpr std::int32_t kDevTypeQuiet = 23;

    constexpr std::uint8_t  kHorseVanillaTackCount = 7;
    constexpr std::uint16_t kHorseKeepDamageRate   = 0xFFFF;
    constexpr std::int32_t  kHorseMaxDamageRate    = 0xFF;
    constexpr std::int32_t  kAttackIdLimit         = 0x400;

    struct HorseTackEffects
    {
        std::int32_t damageRate      = -1;    // hit damage byte, 200 = full damage
        float        gallopSpeedRate = -1.0f;
        std::int32_t bodyAttackId    = -1;
        std::int32_t soundType       = -1;
    };

    constexpr std::uint8_t kDogVanillaGearCount = 7;

    struct DogGearEffects
    {
        std::int32_t commandType        = -1;    // vanilla gear whose "Do it" order D-Dog gets
        float        damageRate         = -1.0f;
        std::int32_t bulletFlinch       = -1;    // -1 unset, 0 false, 1 true
        std::int32_t armorHitEffect     = -1;
        std::int32_t armorHitSound      = -1;
        float        escapeDistance     = -1.0f;
        float        hitRetreatDistance = -1.0f;
        float        retargetRange      = -1.0f;
        std::int32_t soldierThreat      = -1;
    };

    struct Entry
    {
        std::string   key;
        Kind          kind      = Kind::QuietCostume;
        std::uint16_t developId = 0;
        std::uint8_t  value     = 0;
        std::uint64_t fpkPath   = 0;
        bool          bruise    = false;
        std::uint64_t bruiseFpk = 0;
        std::uint64_t bruiseFv2 = 0;
        std::uint16_t equipId   = 0;
        HorseTackEffects horse;
        DogGearEffects   dog;
    };

    std::uint8_t BuddyTypeOf(Kind kind);
    bool         IsSuitKind(Kind kind);
    std::uint8_t FirstCustomValue(Kind kind);
    const char*  KindName(Kind kind);
    bool         KindFromBuddyType(std::uint8_t buddyType, bool isSuit, Kind* out);

    bool Register(Entry& entry);
    void NoteDevelopRow(const std::string& key, std::uint16_t developId,
                        std::int32_t equipId, std::int32_t developType);
    bool TryGetByDevelopId(std::uint16_t developId, Entry* out);
    bool TryGetByValue(Kind kind, std::uint8_t value, Entry* out);
    bool TryGetByKey(const char* key, Entry* out);
    bool HasEntries();

    std::uint32_t* ValueBits(Kind kind);
    std::uint32_t* DevelopIdBits(Kind kind);
    std::uint32_t* BruiseValueBits();

    std::uint16_t* HorseDamageRates();
    float*         HorseGallopRates();
    std::uint16_t* HorseBodyAttackIds();
    std::uint8_t*  HorseSoundCodes();

    std::uint32_t* DogCommands();
    std::uint8_t*  DogCommandGears();
    float*         DogDamageRates();
    float*         DogEscapeDistances();
    float*         DogHitRetreatDistances();
    float*         DogRetargetRanges();
    std::uint32_t* DogHoldsBiteBits();
    std::uint32_t* DogNoBulletFlinchBits();
    std::uint32_t* DogArmorHitEffectBits();
    std::uint32_t* DogArmorHitSoundBits();
    std::uint32_t* DogSoldierThreatBits();

    std::uint8_t* ReadScriptVars();
}
