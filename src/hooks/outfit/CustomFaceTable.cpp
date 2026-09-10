#include "pch.h"

#include "CustomFaceTable.h"

#include <atomic>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#include <HookUtils.h>

#include "log.h"
#include "AddressSet.h"
#include "FoxHashes.h"
#include "FaceIdRegistry.h"

namespace outfit
{
    namespace
    {
        constexpr std::size_t kFaceUnitStride = 0x28;
        constexpr std::size_t kFaceUnitCount  = 900;

        constexpr std::size_t kOff_FaceUnitArray     = 0x90;
        constexpr std::size_t kOff_RegisteredCounts  = 0xF8;
        constexpr std::size_t kOff_NeedTable         = 0xA0;
        constexpr std::size_t kOff_NeedCapacity      = 0xD4;
        constexpr std::size_t kOff_NeedCount         = 0xDC;
        constexpr std::size_t kOff_SpecialFaceCount  = 0x615;
        constexpr std::size_t kOff_SpecialFaceIds    = 0x618;

        constexpr std::size_t kNeedStride  = 0x28;
        constexpr std::size_t kNeed_FaceId = 0x20;
        constexpr std::size_t kNeed_Counts = 0x22;

        constexpr std::size_t kSpecialFaceSlots = 4;

        constexpr std::size_t kRow_PortraitPath0 = 0x00;
        constexpr std::size_t kRow_PortraitPath1 = 0x08;
        constexpr std::size_t kRow_PortraitPath2 = 0x10;
        constexpr std::size_t kRow_Refs          = 0x18;
        constexpr std::size_t kRow_FovaWord      = 0x1C;
        constexpr std::size_t kRow_Flags         = 0x20;

        constexpr std::uint32_t kFlag_MbIneligible = 0x00000008u;
        constexpr std::uint32_t kFlag_ForceHair    = 0x00000010u;
        constexpr std::uint32_t kFlag_HasFaceFova  = 0x00020000u;
        constexpr std::uint32_t kFlag_HasFaceDeco  = 0x00040000u;
        constexpr std::uint32_t kFlag_HasHair      = 0x00080000u;
        constexpr std::uint32_t kFlag_HasHairDeco  = 0x00100000u;
        constexpr std::uint32_t kFlag_RowDefined   = 0x00800000u;
        constexpr std::uint32_t kFlag_Preserved    = 0x80000000u;

        constexpr std::size_t kOff_FovaFileTable = 0x88;
        constexpr std::size_t kOff_ColumnBases   = 0xE4;
        constexpr std::size_t kOff_FovaTotalMax  = 0x608;

        constexpr std::size_t kFovaFileStride     = 0x10;
        constexpr std::size_t kFovaFile_FpkPath   = 0x00;
        constexpr std::size_t kFovaFile_Fv2PathId = 0x08;

        constexpr std::uint32_t kFovaWidthLimit[kHeadFovaColumnCount] = { 0x100, 0x400, 0x40, 0x40 };
        constexpr const char*   kFovaColumnNames[kHeadFovaColumnCount] =
            { "faceFova", "faceDecoFova", "hairFova", "hairDecoFova" };

        constexpr const char* kAssetsPrefix = "/Assets/tpp/";
        constexpr const char* kHasherProbeFv2[] =
        {
            "/Assets/tpp/fova/common_source/chara/cm_head/face/cm_m0_h0_v000_eye0.fv2",
            "/Assets/tpp/fova/common_source/chara/cm_head/face/cm_m0_h0_v001_eye0.fv2",
            "/Assets/tpp/fova/common_source/chara/cm_head/face/cm_f0_h0_v000_eye0.fv2",
        };

        struct RegisteredFovaPair
        {
            int           column;
            std::uint32_t index;
            std::uint64_t fv2PathId;
            std::uint64_t fpkPathId;
            std::string   fv2Path;
            std::string   fpkPath;
        };

        struct FovaTableIdentity
        {
            std::uint8_t* impl  = nullptr;
            std::uint8_t* table = nullptr;
        };

        std::mutex g_Mutex;
        std::vector<RegisteredFovaPair> g_RegisteredPairs;
        FovaTableIdentity g_TableIdentity;
        bool g_HasherProbePassed = false;
        bool g_HasherProbeLogged = false;

        std::uint8_t* FaceUnitAt(std::uint8_t* impl, std::int32_t faceId)
        {
            std::uint8_t* array = *reinterpret_cast<std::uint8_t**>(impl + kOff_FaceUnitArray);
            if (!array)
                return nullptr;
            return array + static_cast<std::size_t>(faceId) * kFaceUnitStride;
        }

        bool ColumnAccepts(std::uint8_t* impl, int column, std::uint16_t value, std::uint32_t widthLimit)
        {
            if (value == kFovaNone)
                return true;
            if (value >= widthLimit)
                return false;
            const std::uint32_t registered =
                *reinterpret_cast<std::uint32_t*>(impl + kOff_RegisteredCounts + column * 4);
            return value < registered;
        }

        bool IsHeadColumn(int column)
        {
            return column >= kFovaColumn_Face && column <= kFovaColumn_HairDeco;
        }

        std::uint8_t* FovaFileTable(std::uint8_t* impl)
        {
            return *reinterpret_cast<std::uint8_t**>(impl + kOff_FovaFileTable);
        }

        std::uint32_t FovaTotalMax(std::uint8_t* impl)
        {
            return *reinterpret_cast<std::uint32_t*>(impl + kOff_FovaTotalMax);
        }

        std::uint32_t ColumnBase(std::uint8_t* impl, int column)
        {
            return *reinterpret_cast<std::uint32_t*>(impl + kOff_ColumnBases + column * 4);
        }

        std::uint32_t ColumnRegistered(std::uint8_t* impl, int column)
        {
            return *reinterpret_cast<std::uint32_t*>(impl + kOff_RegisteredCounts + column * 4);
        }

        std::uint32_t ColumnCeiling(std::uint8_t* impl, int column)
        {
            if (column + 1 < kFovaColumnCount)
                return ColumnBase(impl, column + 1);
            return FovaTotalMax(impl);
        }

        std::uint32_t ColumnCapacity(std::uint8_t* impl, int column)
        {
            const std::uint32_t base    = ColumnBase(impl, column);
            const std::uint32_t ceiling = ColumnCeiling(impl, column);
            return ceiling > base ? ceiling - base : 0;
        }

        std::uint8_t* FovaFileEntry(std::uint8_t* impl, int column, std::uint32_t index)
        {
            const std::size_t slot = static_cast<std::size_t>(ColumnBase(impl, column)) + index;
            return FovaFileTable(impl) + slot * kFovaFileStride;
        }

        bool IsFovaFileTableCarved(std::uint8_t* impl)
        {
            if (!FovaFileTable(impl))
                return false;
            if (FovaTotalMax(impl) == 0)
                return false;
            return ColumnRegistered(impl, kFovaColumn_Face) != 0;
        }

        std::int32_t FindFv2InColumn_NoLock(std::uint8_t* impl, int column, std::uint64_t fv2PathId)
        {
            const std::uint32_t registered = ColumnRegistered(impl, column);
            for (std::uint32_t i = 0; i < registered; ++i)
            {
                std::uint8_t* entry = FovaFileEntry(impl, column, i);
                if (*reinterpret_cast<std::uint64_t*>(entry + kFovaFile_Fv2PathId) == fv2PathId)
                    return static_cast<std::int32_t>(i);
            }
            return -1;
        }

        bool RegisteredPairsStillStored_NoLock(std::uint8_t* impl)
        {
            for (const RegisteredFovaPair& pair : g_RegisteredPairs)
            {
                if (pair.index >= ColumnRegistered(impl, pair.column))
                    return false;
                std::uint8_t* entry = FovaFileEntry(impl, pair.column, pair.index);
                if (*reinterpret_cast<std::uint64_t*>(entry + kFovaFile_Fv2PathId) != pair.fv2PathId)
                    return false;
            }
            return true;
        }

        void AdoptFovaTable_NoLock(std::uint8_t* impl)
        {
            std::uint8_t* table = FovaFileTable(impl);
            const bool samePointer = g_TableIdentity.impl == impl && g_TableIdentity.table == table;
            if (samePointer && (!table || RegisteredPairsStillStored_NoLock(impl)))
                return;

            if (!g_RegisteredPairs.empty())
                Log("[CustomFace] WARN: the fova file table was %s (impl %p table %p -> %p) - %zu "
                    "custom fova pairs are gone, so faces built on them wear a substitute until the "
                    "game is restarted\n",
                    samePointer ? "re-filled by TppSoldierFace.SetFovaFileTable" : "re-created",
                    impl, g_TableIdentity.table, table, g_RegisteredPairs.size());

            g_RegisteredPairs.clear();
            g_HasherProbePassed = false;
            g_HasherProbeLogged = false;
            g_TableIdentity = { impl, table };
        }

        bool HasherReproducesEngineTable_NoLock(std::uint8_t* impl)
        {
            if (g_HasherProbePassed)
                return true;

            for (const char* probePath : kHasherProbeFv2)
            {
                const std::uint64_t probe = FoxHashes::PathCode64Ext(probePath);
                if (probe != 0 && FindFv2InColumn_NoLock(impl, kFovaColumn_Face, probe) >= 0)
                {
                    g_HasherProbePassed = true;
                    return true;
                }
            }

            if (!g_HasherProbeLogged)
            {
                g_HasherProbeLogged = true;
                Log("[CustomFace] ERROR: PathCode64Ext('%s')=0x%016llX matches none of the %u "
                    "engine faceFova rows - custom fova pairs are refused this session because "
                    "their hashes would never resolve\n",
                    kHasherProbeFv2[0],
                    static_cast<unsigned long long>(FoxHashes::PathCode64Ext(kHasherProbeFv2[0])),
                    ColumnRegistered(impl, kFovaColumn_Face));
            }
            return false;
        }

        bool PathEndsWith(const std::string& path, const char* extension)
        {
            const std::size_t extLength = std::strlen(extension);
            if (path.size() <= extLength)
                return false;
            return path.compare(path.size() - extLength, extLength, extension) == 0;
        }

        bool IsAssetPathWith(const std::string& normalized, const char* extension)
        {
            return normalized.rfind(kAssetsPrefix, 0) == 0 && PathEndsWith(normalized, extension);
        }

        const RegisteredFovaPair* FindRegisteredPair_NoLock(int column, std::uint32_t index)
        {
            for (const RegisteredFovaPair& pair : g_RegisteredPairs)
                if (pair.column == column && pair.index == index)
                    return &pair;
            return nullptr;
        }

        void ReadFovaColumnDiagnostics_NoLock(std::uint8_t* impl, const FaceDiagnostics& face,
                                              int column, FovaColumnDiagnostics& out)
        {
            constexpr std::uint32_t kShift[kHeadFovaColumnCount] = { 0, 8, 18, 24 };
            constexpr std::uint32_t kMask[kHeadFovaColumnCount]  = { 0xFF, 0x3FF, 0x3F, 0x3F };

            out.present = (face.flags & (kFlag_HasFaceFova << column)) != 0;
            out.index   = (face.fovaWord >> kShift[column]) & kMask[column];

            if (!IsFovaFileTableCarved(impl))
                return;

            out.registered = ColumnRegistered(impl, column);
            out.capacity   = ColumnCapacity(impl, column);
            if (!out.present || out.index >= out.registered)
                return;

            std::uint8_t* entry = FovaFileEntry(impl, column, out.index);
            out.fv2PathId = *reinterpret_cast<std::uint64_t*>(entry + kFovaFile_Fv2PathId);
            out.fpkPathId = *reinterpret_cast<std::uint64_t*>(entry + kFovaFile_FpkPath);
            if (const RegisteredFovaPair* pair = FindRegisteredPair_NoLock(column, out.index))
            {
                out.custom  = true;
                out.fv2Path = pair->fv2Path;
                out.fpkPath = pair->fpkPath;
            }
        }
    }

    std::uint8_t* FaceSystemImpl()
    {
        void* slot = ResolveGameAddress(gAddr.Soldier2FaceSystem_Instance);
        if (!slot)
            return nullptr;
        return *reinterpret_cast<std::uint8_t**>(slot);
    }

    bool IsFaceSystemReady()
    {
        std::uint8_t* impl = FaceSystemImpl();
        if (!impl)
            return false;
        return *reinterpret_cast<std::uint8_t**>(impl + kOff_FaceUnitArray) != nullptr;
    }

    std::uint32_t GetRegisteredFovaCount(int column)
    {
        if (column < 0 || column >= kFovaColumnCount)
            return 0;
        std::uint8_t* impl = FaceSystemImpl();
        if (!impl)
            return 0;
        return *reinterpret_cast<std::uint32_t*>(impl + kOff_RegisteredCounts + column * 4);
    }

    bool IsFaceRowDefined(std::int32_t faceId)
    {
        if (faceId < 0 || static_cast<std::size_t>(faceId) >= kFaceUnitCount)
            return false;

        std::uint8_t* impl = FaceSystemImpl();
        if (!impl)
            return false;

        std::uint8_t* row = FaceUnitAt(impl, faceId);
        if (!row)
            return false;

        return (*reinterpret_cast<std::uint32_t*>(row + kRow_Flags) & kFlag_RowDefined) != 0;
    }

    bool ReadFaceDiagnostics(std::int32_t faceId, FaceDiagnostics& out)
    {
        if (faceId < 0 || static_cast<std::size_t>(faceId) >= kFaceUnitCount)
            return false;

        std::uint8_t* impl = FaceSystemImpl();
        if (!impl)
            return false;

        std::uint8_t* row = FaceUnitAt(impl, faceId);
        if (!row)
            return false;

        out.fovaWord   = *reinterpret_cast<std::uint32_t*>(row + kRow_FovaWord);
        out.flags      = *reinterpret_cast<std::uint32_t*>(row + kRow_Flags);
        out.rowDefined = (out.flags & kFlag_RowDefined) != 0;

        out.needBlacklisted = (faceId == 622 || faceId == 630);
        out.needCount       = *reinterpret_cast<std::uint32_t*>(impl + kOff_NeedCount);
        out.needCapacity    = *reinterpret_cast<std::uint32_t*>(impl + kOff_NeedCapacity);

        std::uint8_t* need = *reinterpret_cast<std::uint8_t**>(impl + kOff_NeedTable);
        if (need)
        {
            for (std::uint32_t i = 0; i < out.needCount; ++i)
            {
                std::uint8_t* entry = need + static_cast<std::size_t>(i) * kNeedStride;
                if (*reinterpret_cast<std::uint16_t*>(entry + kNeed_FaceId) != faceId)
                    continue;
                out.resident     = true;
                out.needIndex    = static_cast<std::int32_t>(i);
                out.needCounts[0] = *(entry + kNeed_Counts + 0);
                out.needCounts[1] = *(entry + kNeed_Counts + 1);
                out.needCounts[2] = *(entry + kNeed_Counts + 2);
                for (std::size_t f = 0; f < 4; ++f)
                    out.needFiles[f] = *reinterpret_cast<std::uint64_t*>(entry + f * 8);
                break;
            }
        }

        out.specialCount = *(impl + kOff_SpecialFaceCount);
        for (std::size_t i = 0; i < kSpecialFaceSlots; ++i)
        {
            const std::uint16_t id =
                *reinterpret_cast<std::uint16_t*>(impl + kOff_SpecialFaceIds + i * 2);
            out.specialIds[i] = id;
            if (i < out.specialCount && id == faceId)
                out.special = true;
        }

        std::lock_guard<std::mutex> lock(g_Mutex);
        AdoptFovaTable_NoLock(impl);
        out.fovaTableReady = IsFovaFileTableCarved(impl);
        for (int column = 0; column < kHeadFovaColumnCount; ++column)
            ReadFovaColumnDiagnostics_NoLock(impl, out, column, out.columns[column]);

        return true;
    }

    const char* FovaColumnName(int column)
    {
        if (!IsHeadColumn(column))
            return "?";
        return kFovaColumnNames[column];
    }

    int ParseFovaColumnName(const char* name)
    {
        if (!name || !name[0])
            return -1;
        for (int column = 0; column < kHeadFovaColumnCount; ++column)
            if (_stricmp(name, kFovaColumnNames[column]) == 0)
                return column;
        return -1;
    }

    bool IsFovaFileTableReady()
    {
        std::uint8_t* impl = FaceSystemImpl();
        return impl && IsFovaFileTableCarved(impl);
    }

    bool ReadFovaColumnInfo(int column, FovaColumnInfo& out)
    {
        if (!IsHeadColumn(column))
            return false;

        std::uint8_t* impl = FaceSystemImpl();
        if (!impl)
            return false;

        std::lock_guard<std::mutex> lock(g_Mutex);
        AdoptFovaTable_NoLock(impl);
        if (!IsFovaFileTableCarved(impl))
            return false;

        out.registered = ColumnRegistered(impl, column);
        out.base       = ColumnBase(impl, column);
        out.capacity   = ColumnCapacity(impl, column);
        out.widthLimit = kFovaWidthLimit[column];
        return true;
    }

    const char* FindFovaIndexByFv2(int column, const char* fv2Path, std::uint32_t& outIndex)
    {
        outIndex = 0;

        if (!IsHeadColumn(column))
            return "column must be faceFova, faceDecoFova, hairFova or hairDecoFova";
        if (!fv2Path || !fv2Path[0])
            return "fv2 path is empty";

        const std::string fv2 = FoxHashes::NormalizeAssetPath(fv2Path);
        if (!IsAssetPathWith(fv2, ".fv2"))
            return "fv2 must be a /Assets/tpp/... path ending in .fv2";

        const std::uint64_t fv2PathId = FoxHashes::PathCode64Ext(fv2);
        if (fv2PathId == 0)
            return "the fox path hasher is not resolved on this build";

        std::uint8_t* impl = FaceSystemImpl();
        if (!impl)
            return "the soldier face system is not created yet";

        std::lock_guard<std::mutex> lock(g_Mutex);
        AdoptFovaTable_NoLock(impl);
        if (!IsFovaFileTableCarved(impl))
            return "the fova file table is not filled yet - call this from LoadLibraries or later";
        if (!HasherReproducesEngineTable_NoLock(impl))
            return "the DLL path hasher does not reproduce the engine fova table";

        const std::int32_t found = FindFv2InColumn_NoLock(impl, column, fv2PathId);
        if (found < 0)
            return "the fv2 path is not in the fova file table";

        outIndex = static_cast<std::uint32_t>(found);
        return nullptr;
    }

    const char* RegisterFovaPair(int column, const char* fv2Path, const char* fpkPath,
                                 std::uint32_t& outIndex)
    {
        outIndex = 0;

        if (!IsHeadColumn(column))
            return "column must be faceFova, faceDecoFova, hairFova or hairDecoFova";
        if (!fv2Path || !fv2Path[0])
            return "fv2 path is empty";
        if (!fpkPath || !fpkPath[0])
            return "fpk path is empty";

        const std::string fv2 = FoxHashes::NormalizeAssetPath(fv2Path);
        const std::string fpk = FoxHashes::NormalizeAssetPath(fpkPath);
        if (!IsAssetPathWith(fv2, ".fv2"))
            return "fv2 must be a /Assets/tpp/... path ending in .fv2";
        if (!IsAssetPathWith(fpk, ".fpk"))
            return "fpk must be a /Assets/tpp/... path ending in .fpk";

        const std::uint64_t fv2PathId = FoxHashes::PathCode64Ext(fv2);
        const std::uint64_t fpkPathId = FoxHashes::PathCode64Ext(fpk);
        if (fv2PathId == 0 || fpkPathId == 0)
            return "the fox path hasher is not resolved on this build";

        std::uint8_t* impl = FaceSystemImpl();
        if (!impl)
            return "the soldier face system is not created yet";

        std::lock_guard<std::mutex> lock(g_Mutex);
        AdoptFovaTable_NoLock(impl);
        if (!IsFovaFileTableCarved(impl))
            return "the fova file table is not filled yet - call this from LoadLibraries or later";
        if (!HasherReproducesEngineTable_NoLock(impl))
            return "the DLL path hasher does not reproduce the engine fova table";

        const std::int32_t existing = FindFv2InColumn_NoLock(impl, column, fv2PathId);
        if (existing >= 0)
        {
            std::uint8_t* entry = FovaFileEntry(impl, column, static_cast<std::uint32_t>(existing));
            const std::uint64_t storedFpk =
                *reinterpret_cast<std::uint64_t*>(entry + kFovaFile_FpkPath);
            if (storedFpk != fpkPathId)
                Log("[CustomFace] WARN: %s '%s' is already row %d with fpk 0x%016llX - the "
                    "requested fpk '%s' is ignored and that row is reused\n",
                    kFovaColumnNames[column], fv2.c_str(), existing,
                    static_cast<unsigned long long>(storedFpk), fpk.c_str());
            outIndex = static_cast<std::uint32_t>(existing);
            return nullptr;
        }

        const std::uint32_t registered = ColumnRegistered(impl, column);
        if (registered >= ColumnCapacity(impl, column))
            return "the column has no free row left";
        if (registered >= kFovaWidthLimit[column])
            return "the next row index does not fit the face record field";

        std::uint8_t* entry = FovaFileEntry(impl, column, registered);
        *reinterpret_cast<std::uint64_t*>(entry + kFovaFile_Fv2PathId) = fv2PathId;
        *reinterpret_cast<std::uint64_t*>(entry + kFovaFile_FpkPath)   = fpkPathId;
        std::atomic_thread_fence(std::memory_order_release);
        *reinterpret_cast<std::uint32_t*>(impl + kOff_RegisteredCounts + column * 4) = registered + 1;

        g_RegisteredPairs.push_back({ column, registered, fv2PathId, fpkPathId, fv2, fpk });
        outIndex = registered;
        return nullptr;
    }

    const char* WriteCustomFaceRow(std::int32_t faceId, const CustomFaceRow& row)
    {
        if (faceId < kFaceIdBandFirst || faceId > kFaceIdBandLast)
            return "faceId outside the managed band";
        if (static_cast<std::size_t>(faceId) >= kFaceUnitCount)
            return "faceId outside the engine face table";

        std::lock_guard<std::mutex> lock(g_Mutex);

        std::uint8_t* impl = FaceSystemImpl();
        if (!impl)
            return "the soldier face system is not created yet";

        std::uint8_t* target = FaceUnitAt(impl, faceId);
        if (!target)
            return "the engine face table is not carved yet";

        if (row.eyeMeshType > 1)
            return "eyeMeshType must be 0 or 1";
        if (row.race > 3)
            return "race must be 0..3";
        if (row.gender > 1)
            return "gender must be 0 or 1";
        if (!ColumnAccepts(impl, kFovaColumn_Face, row.faceFova, kFovaWidthLimit[kFovaColumn_Face]))
            return "faceFova is not a registered fova index";
        if (!ColumnAccepts(impl, kFovaColumn_FaceDeco, row.faceDecoFova, kFovaWidthLimit[kFovaColumn_FaceDeco]))
            return "faceDecoFova is not a registered fova index";
        if (!ColumnAccepts(impl, kFovaColumn_Hair, row.hairFova, kFovaWidthLimit[kFovaColumn_Hair]))
            return "hairFova is not a registered fova index";
        if (!ColumnAccepts(impl, kFovaColumn_HairDeco, row.hairDecoFova, kFovaWidthLimit[kFovaColumn_HairDeco]))
            return "hairDecoFova is not a registered fova index";
        if (row.eyeFova != kFovaNone && row.eyeFova >= 0x20)
            return "eyeFova must be 0..31";
        if (row.skinFova != kFovaNone && row.skinFova >= 0x20)
            return "skinFova must be 0..31";

        std::uint32_t fovaWord = 0;
        std::uint32_t flags    = *reinterpret_cast<std::uint32_t*>(target + kRow_Flags) & kFlag_Preserved;

        if (row.faceFova != kFovaNone)
        {
            fovaWord |= static_cast<std::uint32_t>(row.faceFova) & 0xFFu;
            flags    |= kFlag_HasFaceFova;
        }
        if (row.faceDecoFova != kFovaNone)
        {
            fovaWord |= (static_cast<std::uint32_t>(row.faceDecoFova) & 0x3FFu) << 8;
            flags    |= kFlag_HasFaceDeco;
        }
        if (row.hairFova != kFovaNone)
        {
            fovaWord |= (static_cast<std::uint32_t>(row.hairFova) & 0x3Fu) << 18;
            flags    |= kFlag_HasHair;
        }
        if (row.hairDecoFova != kFovaNone)
        {
            fovaWord |= (static_cast<std::uint32_t>(row.hairDecoFova) & 0x3Fu) << 24;
            flags    |= kFlag_HasHairDeco;
        }
        if (row.gender != 0)
            fovaWord |= 0x40000000u;

        flags |= static_cast<std::uint32_t>(row.race) & 0x7u;
        flags |= kFlag_MbIneligible;
        if (row.forceHairFova)
            flags |= kFlag_ForceHair;

        if (row.skinFova != kFovaNone)
            flags |= ((static_cast<std::uint32_t>(row.skinFova) & 0x1Fu) | 0x10000u) << 5;
        if (row.eyeFova != kFovaNone)
            flags |= ((static_cast<std::uint32_t>(row.eyeFova) & 0x1Fu) | 0x1000u) << 10;

        flags |= (static_cast<std::uint32_t>(row.eyeMeshType) & 0x3u) << 15;

        *reinterpret_cast<std::uint64_t*>(target + kRow_PortraitPath0) = 0;
        *reinterpret_cast<std::uint64_t*>(target + kRow_PortraitPath1) = 0;
        *reinterpret_cast<std::uint64_t*>(target + kRow_PortraitPath2) = 0;
        *reinterpret_cast<std::uint32_t*>(target + kRow_Refs)          = 0;
        *reinterpret_cast<std::uint32_t*>(target + kRow_FovaWord)      = fovaWord;
        *reinterpret_cast<std::uint32_t*>(target + kRow_Flags)         = flags;

        *reinterpret_cast<std::uint32_t*>(target + kRow_Flags) = flags | kFlag_RowDefined;

        return nullptr;
    }
}
