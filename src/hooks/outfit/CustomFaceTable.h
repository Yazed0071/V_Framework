#pragma once

#include <cstdint>
#include <string>

namespace outfit
{
    constexpr std::uint16_t kFovaNone = 0xFFFF;
    constexpr std::uint16_t kFovaLegacyNone = 0x7FFF;
    constexpr int kHeadFovaColumnCount = 4;

    enum FovaColumn
    {
        kFovaColumn_Face     = 0,
        kFovaColumn_FaceDeco = 1,
        kFovaColumn_Hair     = 2,
        kFovaColumn_HairDeco = 3,
        kFovaColumn_Body     = 4,
        kFovaColumnCount     = 5,
    };

    struct CustomFaceRow
    {
        std::uint16_t faceFova      = kFovaNone;
        std::uint16_t faceDecoFova  = kFovaNone;
        std::uint16_t hairFova      = kFovaNone;
        std::uint16_t hairDecoFova  = kFovaNone;
        std::uint16_t eyeFova       = kFovaNone;
        std::uint16_t skinFova      = kFovaNone;
        std::uint8_t  gender        = 0;
        std::uint8_t  race          = 0;
        std::uint8_t  eyeMeshType   = 0;
        bool          forceHairFova = false;
        bool          mbEligible    = false;
        bool          unique        = false;
        std::uint16_t outsideFaceRef1 = 0;
        std::uint16_t outsideFaceRef2 = 0;
        std::uint16_t outsideFaceRef3 = 0;
        std::uint8_t  outsideFaceCount = 0;
        std::string   uiTextureName;
        std::string   uiTexBasePath = "/Assets/tpp/ui/texture/StaffImage/";
        std::uint8_t  uiTextureCount = 0;
    };

    struct FovaColumnInfo
    {
        std::uint32_t registered = 0;
        std::uint32_t capacity   = 0;
        std::uint32_t base       = 0;
        std::uint32_t widthLimit = 0;
    };

    struct FaceDiagnostics
    {
        bool          rowDefined      = false;
        std::uint32_t fovaWord        = 0;
        std::uint32_t flags           = 0;
        std::uint8_t  uiTextureCount  = 0;
        std::uint64_t uiTexturePaths[3] = { 0, 0, 0 };
    };

    std::uint8_t* FaceSystemImpl();

    bool IsFaceSystemReady();

    bool ReadFaceTableIdentity(std::uintptr_t& implId, std::uintptr_t& rowsId);

    bool IsFaceRowDefined(std::int32_t faceId);

    bool IsFaceRowDefinedByOtherMod(std::int32_t faceId);

    bool HasFrameworkFaceMark(std::int32_t faceId);

    bool ReadFaceDiagnostics(std::int32_t faceId, FaceDiagnostics& out);

    bool ValidateCustomFaceRow(const CustomFaceRow& row, std::string& reason);

    bool WriteCustomFaceRow(std::int32_t faceId, const CustomFaceRow& row, std::string& reason);

    bool WriteMissingCustomFaceFallback(std::int32_t faceId, std::int32_t gender,
                                       std::int32_t race, std::int32_t preferredDonorId,
                                       std::int32_t& outDonorId, std::string& reason);

    bool IsCustomFaceFallback(std::int32_t faceId);

    void RevokeMbEligibility();

    std::int32_t PickVanillaDonorFaceId(std::uint32_t sourceFaceId);

    std::int32_t PickCompatibleVanillaDonorFaceId(std::uint32_t sourceFaceId,
                                                std::int32_t gender, std::int32_t race,
                                                std::int32_t preferredDonorId = -1);

    const char* FovaColumnName(int column);

    bool ReadFovaColumnInfo(int column, FovaColumnInfo& out);

    const char* FindFovaIndexByFv2(int column, const char* fv2Path, const char* fpkPath,
                                   std::uint32_t& outIndex);
}
