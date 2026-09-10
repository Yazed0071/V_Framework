#pragma once

#include <cstdint>
#include <string>

namespace outfit
{
    constexpr std::uint16_t kFovaNone = 0x7FFF;
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
    };

    struct FovaColumnInfo
    {
        std::uint32_t registered = 0;
        std::uint32_t capacity   = 0;
        std::uint32_t base       = 0;
        std::uint32_t widthLimit = 0;
    };

    struct FovaColumnDiagnostics
    {
        bool          present    = false;
        std::uint32_t index      = 0;
        std::uint32_t registered = 0;
        std::uint32_t capacity   = 0;
        std::uint64_t fv2PathId  = 0;
        std::uint64_t fpkPathId  = 0;
        bool          custom     = false;
        std::string   fv2Path;
        std::string   fpkPath;
    };

    struct FaceDiagnostics
    {
        bool          rowDefined      = false;
        std::uint32_t fovaWord        = 0;
        std::uint32_t flags           = 0;
        bool          resident        = false;
        std::int32_t  needIndex       = -1;
        std::uint8_t  needCounts[3]   = { 0, 0, 0 };
        std::uint64_t needFiles[4]    = { 0, 0, 0, 0 };
        std::uint32_t needCount       = 0;
        std::uint32_t needCapacity    = 0;
        bool          needBlacklisted = false;
        bool          special         = false;
        std::uint8_t  specialCount    = 0;
        std::uint16_t specialIds[4]   = { 0, 0, 0, 0 };
        bool          fovaTableReady  = false;
        FovaColumnDiagnostics columns[kHeadFovaColumnCount];
    };

    std::uint8_t* FaceSystemImpl();

    bool IsFaceSystemReady();

    bool IsFaceRowDefined(std::int32_t faceId);

    std::uint32_t GetRegisteredFovaCount(int column);

    bool ReadFaceDiagnostics(std::int32_t faceId, FaceDiagnostics& out);

    const char* WriteCustomFaceRow(std::int32_t faceId, const CustomFaceRow& row);

    const char* FovaColumnName(int column);

    int ParseFovaColumnName(const char* name);

    bool IsFovaFileTableReady();

    bool ReadFovaColumnInfo(int column, FovaColumnInfo& out);

    const char* FindFovaIndexByFv2(int column, const char* fv2Path, std::uint32_t& outIndex);

    const char* RegisterFovaPair(int column, const char* fv2Path, const char* fpkPath,
                                 std::uint32_t& outIndex);
}
