#pragma once

#include <cstdint>

namespace outfit::ddheadlayers
{
    struct PartsLoadProbe
    {
        void*         impl          = nullptr;
        void*         shell         = nullptr;
        std::uint32_t implSlot      = 0;
        std::int32_t  stateBefore   = 0;
        bool          matchedBefore = false;
        bool          valid         = false;
    };

    bool Install();
    void Uninstall();
    bool IsReady();

    bool CanReplaceFace();
    void ArmBranchFace(std::uint16_t faceId, std::uint64_t fv2, std::uint64_t fpk);
    void DisarmBranchFace();

    void* FindLocalHeadFile(std::uint64_t pathCode);
    void* FindLocalHeadBlock();

    void RecordLocalStaffFaceLoad(void* impl, std::uint32_t implSlot,
                                  const std::uint8_t* nativeTypeInfo,
                                  const std::uint8_t* visualTypeInfo);

    PartsLoadProbe BeginPartsLoad(void* impl, std::uint32_t implSlot,
                                  const std::uint8_t* typeInfo);
    void EndPartsLoad(const PartsLoadProbe& probe, const std::uint8_t* typeInfo);
}
