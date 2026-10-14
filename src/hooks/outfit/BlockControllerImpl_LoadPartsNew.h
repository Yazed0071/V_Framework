#pragma once

#include <cstddef>
#include <cstdint>

namespace outfit
{
    struct OutfitEntry;
    struct OutfitColor;

    const OutfitColor* GetServablePackColor(const OutfitEntry& entry, std::uint8_t playerType, std::uint8_t variant);


    std::uint32_t GetLocalPartsSlot();

    std::uint8_t GetRealizedLocalHeadSlot();

    void RequestPartsRestream();
    std::uint64_t GetServedColorFpk();
    bool IsLocalFovaModelReady();

    bool ApplyFv2OnTop(const char* fv2Path);
    bool CanApplyFv2OnTop();

    enum class HeadFv2 { Applied, NotReady, NotLoaded, NoHeadModel, Failed };

    enum class IrisFv2 { Applied, NotReady, NotLoaded, NoIrisGroup, OtherGroups, NoStaffIris, Failed };
    IrisFv2 ApplyIrisFv2(std::uint64_t fv2Code, std::uint8_t eyeColor);
    HeadFv2 ApplyFv2OnHead(std::uint64_t fv2Code, const std::uint64_t* headFv2Codes, std::size_t headFv2Count,
                           bool resetHead);

    bool Install_OutfitRuntimeParts_Hooks();
    void Uninstall_OutfitRuntimeParts_Hooks();
}
