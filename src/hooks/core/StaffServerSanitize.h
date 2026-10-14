#pragma once

#include <cstddef>
#include <cstdint>

namespace staffServer
{
    constexpr int kSectionCount = 7;   // section_soldier combat..security = staff sections 1..7
    constexpr int kPfSkillCount = 18;  // FOB skills 0x45..0x56 counted in pf_skill_staff
    constexpr std::uint32_t kStaffCapacity = 0xDAC;

    struct SectionTally
    {
        std::uint32_t total[kSectionCount + 1] = {};
        std::uint32_t custom[kSectionCount + 1] = {};
        std::uint32_t staffCount = 0;
        std::uint32_t customAll = 0;
        std::uint32_t skillTotal[kPfSkillCount] = {};
        std::uint32_t skillCustom[kPfSkillCount] = {};
    };

    struct StaffWords
    {
        std::uint32_t header;
        std::uint32_t seed;
        std::uint32_t status;
    };

    constexpr int kSecurityBuckets = 10;

    struct SectionPoints
    {
        std::uint32_t full[kSectionCount + 1] = {};
        std::uint32_t custom[kSectionCount + 1] = {};
        std::uint32_t customCount[kSectionCount + 1] = {};
        std::uint32_t bucketFull[kSecurityBuckets] = {};
        std::uint32_t bucketCustom[kSecurityBuckets] = {};
    };

    std::uint8_t* LiveMotherBaseManager();
    std::uint8_t* LiveStaffController();

    bool TallySectionPoints(SectionPoints& out);

    bool ReadSectionSumAndLevel(const std::uint8_t* ctrl, int section, std::uint32_t& sum, std::uint8_t& level);
    std::uint8_t SectionLevelOfSum(const std::uint8_t* ctrl, std::uint32_t sum);
    std::uint8_t UnitLevelOfSectionLevel(const std::uint8_t* ctrl, int unit, std::uint8_t sectionLevel);

    void FixSectionLevels(std::uint32_t* levels, const SectionPoints& points, const char* command);

    bool IsCustomUniqueStaff(std::uint32_t header, std::uint32_t seed);

    std::uint32_t SectionOfStatusWord(std::uint32_t status);

    int PfSkillIndexOfHeader(std::uint32_t header);

    bool TallyLiveRoster(SectionTally& out);

    std::size_t CollectLiveCustomStaff(StaffWords* out, std::size_t capacity);

    void SubtractCustomFromSectionCounts(std::uint32_t* counts, const SectionTally& tally,
                                         const char* command);
}
