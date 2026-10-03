#include "pch.h"

#include "CustomFaceTable.h"

#include <array>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>

#include <HookUtils.h>

#include "log.h"
#include "AddressSet.h"
#include "FoxHashes.h"
#include "FaceIdRegistry.h"
#include "FaceFovaWiden.h"

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

        constexpr std::size_t kNeedStride  = 0x28;
        constexpr std::size_t kNeed_FaceId = 0x20;

        constexpr std::size_t kRow_PortraitPath0 = 0x00;
        constexpr std::size_t kRow_PortraitPath1 = 0x08;
        constexpr std::size_t kRow_PortraitPath2 = 0x10;
        constexpr std::size_t kRow_Refs          = 0x18;
        constexpr std::size_t kRow_FovaWord      = 0x1C;
        constexpr std::size_t kRow_Flags         = 0x20;
        constexpr std::size_t kRow_Owner         = 0x24;

        constexpr std::uint32_t kOwnerMark = 0x57465624u;

        constexpr std::uint32_t kFlag_MbIneligible = 0x00000008u;
        constexpr std::uint32_t kFlag_ForceHair    = 0x00000010u;
        constexpr std::uint32_t kFlag_HasFaceFova  = 0x00020000u;
        constexpr std::uint32_t kFlag_HasFaceDeco  = 0x00040000u;
        constexpr std::uint32_t kFlag_HasHair      = 0x00080000u;
        constexpr std::uint32_t kFlag_HasHairDeco  = 0x00100000u;
        constexpr std::uint32_t kFlag_RowDefined   = 0x00800000u;
        constexpr std::uint32_t kFlag_Preserved    = 0x80000000u;
        constexpr std::uint32_t kFlag_RaceMask     = 0x00000007u;

        constexpr std::uint32_t kFovaWord_Female = 0x40000000u;
        constexpr std::uint32_t kFovaWord_Unique = 0x80000000u;

        constexpr std::int32_t kMbDiscoveryBound = 600;

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

        std::mutex g_Mutex;
        bool g_HasherProbePassed = false;
        bool g_HasherProbeLogged = false;

        using FaceRowBytes = std::array<std::uint8_t, kFaceUnitStride>;

        struct OwnedFaceRow
        {
            bool recorded = false;
            bool fallback = false;
            bool disowned = false;
            bool sidecar = false;
            std::int32_t donorId = -1;
            FaceRowBytes bytes{};
            std::array<std::uint16_t, kHeadFovaColumnCount> indices{};
        };

        std::uint8_t* g_OwnedFaceImpl = nullptr;
        std::uint8_t* g_OwnedFaceArray = nullptr;
        std::array<OwnedFaceRow, kFaceUnitCount> g_OwnedFaceRows{};

        std::uint8_t* FaceUnitAt(std::uint8_t* impl, std::int32_t faceId)
        {
            std::uint8_t* array = *reinterpret_cast<std::uint8_t**>(impl + kOff_FaceUnitArray);
            if (!array)
                return nullptr;
            return array + static_cast<std::size_t>(faceId) * kFaceUnitStride;
        }

        void AdoptFaceTable_NoLock(std::uint8_t* impl)
        {
            std::uint8_t* array = *reinterpret_cast<std::uint8_t**>(impl + kOff_FaceUnitArray);
            if (g_OwnedFaceImpl == impl && g_OwnedFaceArray == array)
                return;
            g_OwnedFaceImpl = impl;
            g_OwnedFaceArray = array;
            g_OwnedFaceRows.fill(OwnedFaceRow{});
            ResetWideFaceFova(array);
        }

        FaceRowBytes ReadFaceRowBytes(const std::uint8_t* row)
        {
            FaceRowBytes bytes{};
            std::memcpy(bytes.data(), row, bytes.size());
            const std::uint32_t flags =
                *reinterpret_cast<const std::uint32_t*>(row + kRow_Flags) & ~kFlag_Preserved;
            std::memcpy(bytes.data() + kRow_Flags, &flags, sizeof(flags));
            return bytes;
        }

        bool IsRowDefinedByOtherMod_NoLock(std::uint8_t* impl, std::int32_t faceId,
                                           std::uint8_t* row)
        {
            AdoptFaceTable_NoLock(impl);
            OwnedFaceRow& owned = g_OwnedFaceRows[static_cast<std::size_t>(faceId)];
            if ((*reinterpret_cast<std::uint32_t*>(row + kRow_Flags) & kFlag_RowDefined) == 0)
            {
                if (owned.recorded)
                    ClearWideFaceFova(row);
                if (*reinterpret_cast<const std::uint32_t*>(row + kRow_Owner) == kOwnerMark)
                    *reinterpret_cast<std::uint32_t*>(row + kRow_Owner) = 0;
                owned = {};
                return false;
            }
            if (!owned.recorded && !owned.disowned
                && *reinterpret_cast<const std::uint32_t*>(row + kRow_Owner) == kOwnerMark)
                return false;
            if (owned.recorded && owned.bytes == ReadFaceRowBytes(row))
            {
                std::uint16_t indices[4]{};
                const bool haveWide = ReadWideFaceFova(row, indices);
                if (!FaceFovaWidenReady() || (owned.fallback && !haveWide)
                    || (!owned.sidecar && !haveWide)
                    || (haveWide
                        && std::equal(std::begin(indices), std::end(indices), owned.indices.begin())))
                    return false;
            }
            if (owned.recorded)
            {
                std::uint16_t indices[4]{};
                if (!ReadWideFaceFova(row, indices))
                    ClearWideFaceFova(row);
            }
            const bool wasOwned = owned.recorded || owned.disowned
                || *reinterpret_cast<const std::uint32_t*>(row + kRow_Owner) == kOwnerMark;
            owned = {};
            owned.disowned = wasOwned;
            return true;
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

        std::uint32_t ColumnUsableRows(std::uint8_t* impl, int column)
        {
            const std::uint32_t registered = ColumnRegistered(impl, column);
            const std::uint32_t capacity   = ColumnCapacity(impl, column);
            return registered < capacity ? registered : capacity;
        }

        bool ColumnAccepts(std::uint8_t* impl, int column, std::uint16_t value, std::string& reason)
        {
            if (value == kFovaNone)
                return true;

            const char*         name       = kFovaColumnNames[column];
            const std::uint32_t width      = FaceFovaWidenReady() ? kFovaNone : kFovaWidthLimit[column];
            const std::uint32_t registered = ColumnRegistered(impl, column);
            const std::uint32_t usable     = ColumnUsableRows(impl, column);

            char text[640];
            if (value >= width)
                std::snprintf(text, sizeof(text),
                    "%s row %u is past the face record field limit for this column (the highest it can hold is %u), "
                    "so no face can point at that row",
                    name, value, width - 1);
            else if (value < usable)
                return true;
            else if (value < registered)
                std::snprintf(text, sizeof(text),
                    "%s row %u is registered but past the %u rows the fova file table has room for - Infinite Heaven "
                    "stopped before sizing the table at boot (look for an InfSoldierFaceAndBody.Setup error in "
                    "ih_log.txt), so that slot now holds another column's pair",
                    name, value, usable);
            else if (registered == 0)
                std::snprintf(text, sizeof(text),
                    "%s row %u cannot be used because that list holds no rows at all - declare the pair as "
                    "{\"<fv2 path>\", \"<fpk path>\"} in a %s list in a mod/fovaInfo/*.lua file, which Infinite Heaven "
                    "appends to the table at boot, then restart the game",
                    name, value, name);
            else
                std::snprintf(text, sizeof(text),
                    "%s row %u is out of range - that list holds rows 0..%u; give the fv2 path string instead of a "
                    "number and the row is looked up by path, or declare the pair as {\"<fv2 path>\", \"<fpk path>\"} "
                    "in a %s list in a mod/fovaInfo/*.lua file (Infinite Heaven appends it as row %u at boot) and "
                    "restart the game",
                    name, value, registered - 1, name, registered);

            reason = text;
            return false;
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
            const std::uint32_t usable = ColumnUsableRows(impl, column);
            for (std::uint32_t i = 0; i < usable; ++i)
            {
                std::uint8_t* entry = FovaFileEntry(impl, column, i);
                if (*reinterpret_cast<std::uint64_t*>(entry + kFovaFile_Fv2PathId) == fv2PathId)
                    return static_cast<std::int32_t>(i);
            }
            return -1;
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
                LogError("CustomFace: PathCode64Ext 0x%016llX not in %u faceFova rows, fv2 path lookups refused",
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

        bool BuildPortraitPaths(const CustomFaceRow& row, std::array<std::string, 3>& paths,
                                std::uint8_t& count, std::string& reason)
        {
            count = row.uiTextureName.empty() ? 0 : row.uiTextureCount;
            if (row.uiTextureCount > paths.size())
            {
                reason = "uiTextureCount must be 0..3";
                return false;
            }
            for (std::uint8_t i = 0; i < count; ++i)
            {
                paths[i] = row.uiTexBasePath + row.uiTextureName + "_"
                         + static_cast<char>('0' + i) + ".ftex";
                if (paths[i].size() >= 512)
                {
                    reason = "each staff portrait path must be shorter than 512 bytes";
                    return false;
                }
                paths[i] = FoxHashes::NormalizeAssetPath(paths[i]);
                if (paths[i].size() >= 512 || !IsAssetPathWith(paths[i], ".ftex"))
                {
                    reason = "uiTexBasePath and uiTextureName must form a /Assets/tpp/... .ftex path";
                    return false;
                }
            }
            return true;
        }

        bool IsDonorCandidate_NoLock(std::uint8_t* impl, std::int32_t faceId)
        {
            if (IsFaceIdInPool(faceId))
                return false;

            std::uint8_t* row = FaceUnitAt(impl, faceId);
            if (!row)
                return false;

            std::uint16_t indices[4]{};
            if (ReadWideFaceFova(row, indices))
                return false;

            const std::uint32_t flags    = *reinterpret_cast<std::uint32_t*>(row + kRow_Flags);
            const std::uint32_t fovaWord = *reinterpret_cast<std::uint32_t*>(row + kRow_FovaWord);

            if ((flags & kFlag_RowDefined) == 0 || (flags & kFlag_MbIneligible) != 0)
                return false;
            if ((flags & kFlag_HasFaceFova) == 0 || (fovaWord & kFovaWord_Unique) != 0)
                return false;
            return true;
        }

        bool IsCompatibleDonorCandidate_NoLock(std::uint8_t* impl, std::int32_t faceId,
                                               std::int32_t gender)
        {
            if (faceId < 0 || faceId >= kMbDiscoveryBound || !IsFovaFileTableCarved(impl)
                || !IsDonorCandidate_NoLock(impl, faceId))
                return false;

            const std::uint8_t* row = FaceUnitAt(impl, faceId);
            const std::uint32_t flags = *reinterpret_cast<const std::uint32_t*>(row + kRow_Flags);
            const std::uint32_t fovaWord = *reinterpret_cast<const std::uint32_t*>(row + kRow_FovaWord);
            if (static_cast<std::int32_t>((fovaWord >> 30) & 1) != gender
                || ((flags >> 15) & 3) > 1
                || ((flags & kFlag_HasHairDeco) && !(flags & kFlag_HasHair)))
                return false;

            constexpr std::uint32_t shifts[kHeadFovaColumnCount] = { 0, 8, 18, 24 };
            constexpr std::uint32_t masks[kHeadFovaColumnCount] = { 0xFF, 0x3FF, 0x3F, 0x3F };
            for (int column = 0; column < kHeadFovaColumnCount; ++column)
            {
                if (!(flags & (kFlag_HasFaceFova << column)))
                    continue;
                const std::uint32_t index = (fovaWord >> shifts[column]) & masks[column];
                if (index >= ColumnUsableRows(impl, column))
                    return false;
                const std::uint8_t* entry = FovaFileEntry(impl, column, index);
                if (!*reinterpret_cast<const std::uint64_t*>(entry + kFovaFile_FpkPath)
                    || !*reinterpret_cast<const std::uint64_t*>(entry + kFovaFile_Fv2PathId))
                    return false;
            }
            const std::uint32_t portraitCount = (flags >> 29) & 3;
            for (std::uint32_t i = 0; i < portraitCount; ++i)
                if (!*reinterpret_cast<const std::uint64_t*>(row + i * 8))
                    return false;
            return true;
        }

        std::int32_t PickCompatibleDonor_Guarded(std::uint32_t sourceFaceId,
                                                std::int32_t gender, std::int32_t race,
                                                std::int32_t preferredDonorId)
        {
            __try
            {
                std::uint8_t* impl = FaceSystemImpl();
                if (!impl || !*reinterpret_cast<std::uint8_t**>(impl + kOff_FaceUnitArray))
                    return -1;

                std::uint32_t matchingRace = 0;
                std::uint32_t matchingGender = 0;
                for (std::int32_t id = 0; id < kMbDiscoveryBound; ++id)
                {
                    if (!IsCompatibleDonorCandidate_NoLock(impl, id, gender))
                        continue;
                    ++matchingGender;
                    const std::uint32_t flags =
                        *reinterpret_cast<std::uint32_t*>(FaceUnitAt(impl, id) + kRow_Flags);
                    if ((flags & kFlag_RaceMask) == static_cast<std::uint32_t>(race))
                        ++matchingRace;
                }
                if (!matchingGender)
                    return -1;

                if (IsCompatibleDonorCandidate_NoLock(impl, preferredDonorId, gender))
                {
                    const std::uint32_t flags =
                        *reinterpret_cast<std::uint32_t*>(FaceUnitAt(impl, preferredDonorId) + kRow_Flags);
                    if (!matchingRace || (flags & kFlag_RaceMask) == static_cast<std::uint32_t>(race))
                        return preferredDonorId;
                }

                std::uint32_t skip = sourceFaceId % (matchingRace ? matchingRace : matchingGender);
                for (std::int32_t id = 0; id < kMbDiscoveryBound; ++id)
                {
                    if (!IsCompatibleDonorCandidate_NoLock(impl, id, gender))
                        continue;
                    const std::uint32_t flags =
                        *reinterpret_cast<std::uint32_t*>(FaceUnitAt(impl, id) + kRow_Flags);
                    if (matchingRace && (flags & kFlag_RaceMask) != static_cast<std::uint32_t>(race))
                        continue;
                    if (!skip)
                        return id;
                    --skip;
                }
                return -1;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return -1;
            }
        }

        std::int32_t PickDonor_Guarded(std::uint32_t sourceFaceId)
        {
            __try
            {
                std::uint8_t* impl = FaceSystemImpl();
                if (!impl || !*reinterpret_cast<std::uint8_t**>(impl + kOff_FaceUnitArray))
                    return -1;

                std::uint32_t matches = 0;
                for (std::int32_t id = 0; id < kMbDiscoveryBound; ++id)
                    if (IsDonorCandidate_NoLock(impl, id))
                        ++matches;

                if (matches == 0)
                    return -1;

                std::uint32_t skip = sourceFaceId % matches;
                for (std::int32_t id = 0; id < kMbDiscoveryBound; ++id)
                {
                    if (!IsDonorCandidate_NoLock(impl, id))
                        continue;
                    if (skip == 0)
                        return id;
                    --skip;
                }
                return -1;
            }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                return -1;
            }
        }

        bool CheckRow_NoLock(std::uint8_t* impl, const CustomFaceRow& row, std::string& reason)
        {
            const bool namesFovaRow = row.faceFova != kFovaNone || row.faceDecoFova != kFovaNone
                                   || row.hairFova != kFovaNone || row.hairDecoFova != kFovaNone;

            if (namesFovaRow && !IsFovaFileTableCarved(impl))
                reason = "the fova file table is not filled yet - call this from LoadLibraries or later";
            else if (row.eyeMeshType > 1)
                reason = "eyeMeshType must be 0 or 1";
            else if (row.race > 3)
                reason = "race must be 0..3";
            else if (row.gender > 1)
                reason = "gender must be 0 or 1";
            else if (row.outsideFaceRef1 > 0x3FF || row.outsideFaceRef2 > 0x3FF || row.outsideFaceRef3 > 0x3FF)
                reason = "outsideFaceRef1, outsideFaceRef2 and outsideFaceRef3 must be 0..1023";
            else if (row.outsideFaceCount > 3)
                reason = "outsideFaceCount must be 0..3";
            else if (row.faceDecoFova != kFovaNone && row.faceFova == kFovaNone)
                reason = "faceDecoFova was given without faceFova - the engine applies a face deco variation onto the "
                         "face model, so a row with no face model faults as soon as the player wears this face";
            else if (row.hairDecoFova != kFovaNone && row.hairFova == kFovaNone)
                reason = "hairDecoFova was given without hairFova - the engine applies a hair deco variation onto the "
                         "hair model, so a row with no hair model faults as soon as the player wears this face";
            else if (row.eyeFova != kFovaNone && row.eyeFova >= 0x20)
                reason = "eyeFova must be 0..31";
            else if (row.skinFova != kFovaNone && row.skinFova >= 0x20)
                reason = "skinFova must be 0..31";
            else if (!ColumnAccepts(impl, kFovaColumn_Face, row.faceFova, reason)
                  || !ColumnAccepts(impl, kFovaColumn_FaceDeco, row.faceDecoFova, reason)
                  || !ColumnAccepts(impl, kFovaColumn_Hair, row.hairFova, reason)
                  || !ColumnAccepts(impl, kFovaColumn_HairDeco, row.hairDecoFova, reason))
                return false;
            else
            {
                std::array<std::string, 3> paths;
                std::uint8_t count = 0;
                return BuildPortraitPaths(row, paths, count, reason);
            }

            return false;
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

    bool ReadFaceTableIdentity(std::uintptr_t& implId, std::uintptr_t& rowsId)
    {
        implId = 0;
        rowsId = 0;
        std::lock_guard<std::mutex> lock(g_Mutex);
        std::uint8_t* impl = FaceSystemImpl();
        if (!impl || !*reinterpret_cast<std::uint8_t**>(impl + kOff_FaceUnitArray)
            || !IsFovaFileTableCarved(impl))
            return false;
        implId = reinterpret_cast<std::uintptr_t>(impl);
        rowsId = reinterpret_cast<std::uintptr_t>(
            *reinterpret_cast<std::uint8_t**>(impl + kOff_FaceUnitArray));
        return true;
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

    bool IsFaceRowDefinedByOtherMod(std::int32_t faceId)
    {
        if (faceId < 0 || static_cast<std::size_t>(faceId) >= kFaceUnitCount)
            return false;

        std::lock_guard<std::mutex> lock(g_Mutex);

        std::uint8_t* impl = FaceSystemImpl();
        if (!impl)
            return false;

        std::uint8_t* row = FaceUnitAt(impl, faceId);
        if (!row)
            return false;

        return IsRowDefinedByOtherMod_NoLock(impl, faceId, row);
    }

    bool HasFrameworkFaceMark(std::int32_t faceId)
    {
        if (faceId < 0 || static_cast<std::size_t>(faceId) >= kFaceUnitCount)
            return false;

        std::uint8_t* impl = FaceSystemImpl();
        if (!impl)
            return false;

        std::uint8_t* row = FaceUnitAt(impl, faceId);
        if (!row)
            return false;

        return *reinterpret_cast<std::uint32_t*>(row + kRow_Owner) == kOwnerMark;
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
        out.uiTextureCount = static_cast<std::uint8_t>((out.flags >> 29) & 3);
        for (std::size_t i = 0; i < 3; ++i)
            out.uiTexturePaths[i] = *reinterpret_cast<std::uint64_t*>(row + i * 8);
        return true;
    }

    std::int32_t PickVanillaDonorFaceId(std::uint32_t sourceFaceId)
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        return PickDonor_Guarded(sourceFaceId);
    }

    std::int32_t PickCompatibleVanillaDonorFaceId(std::uint32_t sourceFaceId,
                                                std::int32_t gender, std::int32_t race,
                                                std::int32_t preferredDonorId)
    {
        if (gender < 0 || gender > 1 || race < 0 || race > 7)
            return -1;
        std::lock_guard<std::mutex> lock(g_Mutex);
        return PickCompatibleDonor_Guarded(sourceFaceId, gender, race, preferredDonorId);
    }

    const char* FovaColumnName(int column)
    {
        if (!IsHeadColumn(column))
            return "?";
        return kFovaColumnNames[column];
    }

    bool ReadFovaColumnInfo(int column, FovaColumnInfo& out)
    {
        if (!IsHeadColumn(column))
            return false;

        std::uint8_t* impl = FaceSystemImpl();
        if (!impl)
            return false;

        if (!IsFovaFileTableCarved(impl))
            return false;

        out.registered = ColumnRegistered(impl, column);
        out.base       = ColumnBase(impl, column);
        out.capacity   = ColumnCapacity(impl, column);
        out.widthLimit = FaceFovaWidenReady() ? kFovaNone : kFovaWidthLimit[column];
        return true;
    }

    const char* FindFovaIndexByFv2(int column, const char* fv2Path, const char* fpkPath,
                                   std::uint32_t& outIndex)
    {
        outIndex = 0;

        if (!IsHeadColumn(column))
            return "column must be faceFova, faceDecoFova, hairFova or hairDecoFova";
        if (!fv2Path || !fv2Path[0])
            return "fv2 path is empty";

        const std::string fv2 = FoxHashes::NormalizeAssetPath(fv2Path);
        if (!IsAssetPathWith(fv2, ".fv2"))
            return "fv2 must be a /Assets/tpp/... path ending in .fv2";

        std::uint64_t fpkPathId = 0;
        const bool checkFpk = fpkPath && fpkPath[0];
        if (checkFpk)
        {
            const std::string fpk = FoxHashes::NormalizeAssetPath(fpkPath);
            if (!IsAssetPathWith(fpk, ".fpk"))
                return "fpk must be a /Assets/tpp/... path ending in .fpk";
            fpkPathId = FoxHashes::PathCode64Ext(fpk);
        }

        const std::uint64_t fv2PathId = FoxHashes::PathCode64Ext(fv2);
        if (fv2PathId == 0 || (checkFpk && fpkPathId == 0))
            return "the fox path hasher is not resolved on this build";

        std::uint8_t* impl = FaceSystemImpl();
        if (!impl)
            return "the soldier face system is not created yet";

        std::lock_guard<std::mutex> lock(g_Mutex);
        if (!IsFovaFileTableCarved(impl))
            return "the fova file table is not filled yet - call this from LoadLibraries or later";
        if (!HasherReproducesEngineTable_NoLock(impl))
            return "the DLL path hasher does not reproduce the engine fova table";

        const std::uint32_t usable = ColumnUsableRows(impl, column);
        bool sawFv2         = false;
        bool sawDifferentFpk = false;
        for (std::uint32_t i = 0; i < usable; ++i)
        {
            std::uint8_t* entry = FovaFileEntry(impl, column, i);
            if (*reinterpret_cast<std::uint64_t*>(entry + kFovaFile_Fv2PathId) != fv2PathId)
                continue;
            sawFv2 = true;
            if (i >= (FaceFovaWidenReady() ? kFovaNone : kFovaWidthLimit[column]))
                continue;
            if (checkFpk && *reinterpret_cast<std::uint64_t*>(entry + kFovaFile_FpkPath) != fpkPathId)
            {
                sawDifferentFpk = true;
                continue;
            }
            outIndex = i;
            return nullptr;
        }

        if (sawDifferentFpk)
            return "every row that holds this fv2 loads a different fpk than the one given";
        if (sawFv2)
            return "the row is past the face record field limit for this column";
        if (ColumnRegistered(impl, column) > ColumnCapacity(impl, column))
            return "the fv2 path was not found in the rows this column has room for - the column holds "
                   "more rows than its size because Infinite Heaven did not finish sizing the fova table "
                   "at boot (look for an InfSoldierFaceAndBody.Setup error in ih_log.txt)";
        return "the fv2 path is not in this list of the fova file table - declare the pair as "
               "{\"<fv2 path>\", \"<fpk path>\"} in a list of that name in a mod/fovaInfo/*.lua file, "
               "which Infinite Heaven appends to the table at boot, then restart the game";
    }

    bool ValidateCustomFaceRow(const CustomFaceRow& row, std::string& reason)
    {
        std::lock_guard<std::mutex> lock(g_Mutex);

        std::uint8_t* impl = FaceSystemImpl();
        if (!impl)
        {
            reason = "the soldier face system is not created yet";
            return false;
        }

        return CheckRow_NoLock(impl, row, reason);
    }

    bool WriteCustomFaceRow(std::int32_t faceId, const CustomFaceRow& row, std::string& reason)
    {
        if (!IsFaceIdInPool(faceId))
        {
            reason = "faceId outside the custom faceId pool";
            return false;
        }
        if (static_cast<std::size_t>(faceId) >= kFaceUnitCount)
        {
            reason = "faceId outside the engine face table";
            return false;
        }

        std::lock_guard<std::mutex> lock(g_Mutex);

        std::uint8_t* impl = FaceSystemImpl();
        if (!impl)
        {
            reason = "the soldier face system is not created yet";
            return false;
        }

        std::uint8_t* target = FaceUnitAt(impl, faceId);
        if (!target)
        {
            reason = "the engine face table is not carved yet";
            return false;
        }

        if (IsRowDefinedByOtherMod_NoLock(impl, faceId, target))
        {
            reason = "this faceId holds a native or externally changed face row that V Framework does not own "
                     "in this session - overwriting it is unsafe";
            return false;
        }

        OwnedFaceRow& owned = g_OwnedFaceRows[static_cast<std::size_t>(faceId)];
        if (owned.recorded && owned.fallback)
        {
            reason = "this faceId already uses its missing-addon fallback in this session - "
                     "restart the game with the face addon installed to restore its custom definition";
            return false;
        }

        if (!CheckRow_NoLock(impl, row, reason))
            return false;

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
        if (row.unique)
            fovaWord |= kFovaWord_Unique;

        flags |= static_cast<std::uint32_t>(row.race) & 0x7u;
        if (!row.mbEligible)
            flags |= kFlag_MbIneligible;
        if (row.forceHairFova)
            flags |= kFlag_ForceHair;

        if (row.skinFova != kFovaNone)
            flags |= ((static_cast<std::uint32_t>(row.skinFova) & 0x1Fu) | 0x10000u) << 5;
        if (row.eyeFova != kFovaNone)
            flags |= ((static_cast<std::uint32_t>(row.eyeFova) & 0x1Fu) | 0x1000u) << 10;

        flags |= (static_cast<std::uint32_t>(row.eyeMeshType) & 0x3u) << 15;

        std::array<std::string, 3> portraitPaths;
        std::array<std::uint64_t, 3> portraitHashes{};
        std::uint8_t portraitCount = 0;
        if (!BuildPortraitPaths(row, portraitPaths, portraitCount, reason))
            return false;
        for (std::uint8_t i = 0; i < portraitCount; ++i)
        {
            portraitHashes[i] = FoxHashes::PathCode64Ext(portraitPaths[i]);
            if (portraitHashes[i] == 0)
            {
                reason = "the fox path hasher could not resolve the staff portrait path";
                return false;
            }
        }
        flags |= static_cast<std::uint32_t>(portraitCount) << 29;
        const std::uint32_t packed = static_cast<std::uint32_t>(row.outsideFaceRef1)
                                  | (static_cast<std::uint32_t>(row.outsideFaceRef2) << 10)
                                  | (static_cast<std::uint32_t>(row.outsideFaceRef3) << 20)
                                  | (static_cast<std::uint32_t>(row.outsideFaceCount) << 30);

        FaceRowBytes wanted{};
        const std::uint32_t definedFlags = (flags & ~kFlag_Preserved) | kFlag_RowDefined;
        std::memcpy(wanted.data(), portraitHashes.data(), sizeof(portraitHashes));
        std::memcpy(wanted.data() + kRow_Refs, &packed, sizeof(packed));
        std::memcpy(wanted.data() + kRow_FovaWord, &fovaWord, sizeof(fovaWord));
        std::memcpy(wanted.data() + kRow_Flags, &definedFlags, sizeof(definedFlags));
        std::memcpy(wanted.data() + kRow_Owner, &kOwnerMark, sizeof(kOwnerMark));

        const std::uint16_t indices[4] = { row.faceFova, row.faceDecoFova, row.hairFova, row.hairDecoFova };
        const std::array<std::uint16_t, 4> wantedIndices = { indices[0], indices[1], indices[2], indices[3] };
        if (owned.recorded && (owned.bytes != wanted || owned.indices != wantedIndices))
        {
            reason = "this faceId is already registered with a different definition in this session - "
                     "changing it can mismatch loaded face resources; restart the game to apply the new definition";
            return false;
        }
        if (!owned.recorded && *reinterpret_cast<const std::uint32_t*>(target + kRow_Owner) == kOwnerMark)
        {
            std::uint16_t current[4]{};
            const bool haveWide = ReadWideFaceFova(target, current);
            bool narrowOnly = true;
            for (int column = 0; column < kHeadFovaColumnCount; ++column)
                if (wantedIndices[column] != kFovaNone && wantedIndices[column] >= kFovaWidthLimit[column])
                    narrowOnly = false;
            const bool sameIndices = !FaceFovaWidenReady() || (!haveWide && narrowOnly)
                || (haveWide && std::equal(std::begin(current), std::end(current), wantedIndices.begin()));
            if (ReadFaceRowBytes(target) != wanted || !sameIndices)
            {
                reason = "this faceId already holds a V Framework face with a different definition or a missing-addon "
                         "fallback - changing it can mismatch loaded face resources; restart the game to apply it";
                return false;
            }
            owned.bytes = wanted;
            owned.indices = wantedIndices;
            owned.sidecar = haveWide;
            owned.recorded = true;
        }

        if (!owned.recorded)
        {
            FaceRowBytes previous{};
            std::memcpy(previous.data(), target, previous.size());
            *reinterpret_cast<std::uint64_t*>(target + kRow_PortraitPath0) = portraitHashes[0];
            *reinterpret_cast<std::uint64_t*>(target + kRow_PortraitPath1) = portraitHashes[1];
            *reinterpret_cast<std::uint64_t*>(target + kRow_PortraitPath2) = portraitHashes[2];
            *reinterpret_cast<std::uint32_t*>(target + kRow_Refs)          = packed;
            *reinterpret_cast<std::uint32_t*>(target + kRow_FovaWord)      = fovaWord;
            *reinterpret_cast<std::uint32_t*>(target + kRow_Owner)         = kOwnerMark;
            const bool sidecar = FaceFovaWidenReady();
            if (sidecar && !StoreWideFaceFova(target, indices))
            {
                std::memcpy(target, previous.data(), previous.size());
                reason = "the full-width face indices could not be published; no face was registered";
                return false;
            }
            *reinterpret_cast<std::uint32_t*>(target + kRow_Flags)         = flags | kFlag_RowDefined;
            owned.bytes = wanted;
            owned.indices = wantedIndices;
            owned.sidecar = sidecar;
            owned.recorded = true;
        }
        return true;
    }

    bool IsCustomFaceFallback(std::int32_t faceId)
    {
        if (!IsFaceIdInPool(faceId))
            return false;
        std::lock_guard<std::mutex> lock(g_Mutex);
        std::uint8_t* impl = FaceSystemImpl();
        if (!impl)
            return false;
        std::uint8_t* row = FaceUnitAt(impl, faceId);
        if (!row || IsRowDefinedByOtherMod_NoLock(impl, faceId, row))
            return false;
        const OwnedFaceRow& owned = g_OwnedFaceRows[static_cast<std::size_t>(faceId)];
        return owned.recorded && owned.fallback;
    }

    void RevokeMbEligibility()
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        std::uint8_t* impl = FaceSystemImpl();
        if (!impl || !*reinterpret_cast<std::uint8_t**>(impl + kOff_FaceUnitArray))
            return;
        AdoptFaceTable_NoLock(impl);
        for (std::int32_t faceId = 0; faceId < static_cast<std::int32_t>(kFaceUnitCount); ++faceId)
        {
            OwnedFaceRow& owned = g_OwnedFaceRows[static_cast<std::size_t>(faceId)];
            if (!IsFaceIdInPool(faceId) || !owned.recorded)
                continue;
            std::uint8_t* row = FaceUnitAt(impl, faceId);
            if (!row || *reinterpret_cast<const std::uint32_t*>(row + kRow_Owner) != kOwnerMark)
                continue;
            const std::uint32_t flags = *reinterpret_cast<const std::uint32_t*>(row + kRow_Flags);
            if ((flags & kFlag_MbIneligible) != 0)
                continue;
            *reinterpret_cast<std::uint32_t*>(row + kRow_Flags) = flags | kFlag_MbIneligible;
            owned.bytes = ReadFaceRowBytes(row);
        }
    }

    bool WriteMissingCustomFaceFallback(std::int32_t faceId, std::int32_t gender,
                                       std::int32_t race, std::int32_t preferredDonorId,
                                       std::int32_t& outDonorId, std::string& reason)
    {
        outDonorId = -1;
        if (!IsFaceIdInPool(faceId))
        {
            reason = "faceId outside the custom faceId pool";
            return false;
        }
        if (gender < 0 || gender > 1 || race < 0 || race > 7)
        {
            reason = "fallback gender must be 0..1 and race must be 0..7";
            return false;
        }
        std::lock_guard<std::mutex> lock(g_Mutex);
        std::uint8_t* impl = FaceSystemImpl();
        if (!impl || !*reinterpret_cast<std::uint8_t**>(impl + kOff_FaceUnitArray)
            || !IsFovaFileTableCarved(impl))
        {
            reason = "the native face and fova file tables are not ready for missing-addon fallbacks";
            return false;
        }
        std::uint8_t* target = FaceUnitAt(impl, faceId);
        if (IsRowDefinedByOtherMod_NoLock(impl, faceId, target))
        {
            reason = "the saved faceId is defined by another native face or mod and cannot receive a fallback";
            return false;
        }
        OwnedFaceRow& owned = g_OwnedFaceRows[static_cast<std::size_t>(faceId)];
        if (owned.recorded)
        {
            if (!owned.fallback)
            {
                reason = "the saved faceId already has a registered custom definition";
                return false;
            }
            const std::uint32_t fovaWord =
                *reinterpret_cast<const std::uint32_t*>(target + kRow_FovaWord);
            if (static_cast<std::int32_t>((fovaWord >> 30) & 1) != gender)
            {
                reason = "the saved faceId already has a different fallback gender - restart to change it";
                return false;
            }
            outDonorId = owned.donorId;
            return true;
        }
        if ((*reinterpret_cast<const std::uint32_t*>(target + kRow_Flags) & kFlag_RowDefined) != 0
            && *reinterpret_cast<const std::uint32_t*>(target + kRow_Owner) == kOwnerMark)
        {
            reason = "the saved faceId already holds a V Framework face written earlier in this session";
            return false;
        }

        const std::uint32_t needCount = *reinterpret_cast<std::uint32_t*>(impl + kOff_NeedCount);
        const std::uint32_t needCapacity = *reinterpret_cast<std::uint32_t*>(impl + kOff_NeedCapacity);
        const std::uint8_t* needs = *reinterpret_cast<std::uint8_t**>(impl + kOff_NeedTable);
        if (needCount > needCapacity || (needCount && !needs))
        {
            reason = "native face resource tracking is not ready for a missing-addon fallback";
            return false;
        }
        for (std::uint32_t i = 0; i < needCount; ++i)
        {
            if (*reinterpret_cast<const std::uint16_t*>(needs + i * kNeedStride + kNeed_FaceId) == faceId)
            {
                reason = "the saved faceId was already queued for native face resources - "
                         "restart so its missing-addon fallback can be installed before loading";
                return false;
            }
        }

        const std::int32_t donorId = PickCompatibleDonor_Guarded(
            static_cast<std::uint32_t>(faceId), gender, race, preferredDonorId);
        if (donorId < 0)
        {
            reason = "no valid vanilla face with the saved gender is available for a missing-addon fallback";
            return false;
        }
        FaceRowBytes wanted = ReadFaceRowBytes(FaceUnitAt(impl, donorId));
        std::uint32_t flags = *reinterpret_cast<const std::uint32_t*>(wanted.data() + kRow_Flags);
        flags |= kFlag_MbIneligible | kFlag_RowDefined;
        std::memcpy(wanted.data() + kRow_Flags, &flags, sizeof(flags));
        std::memcpy(wanted.data() + kRow_Owner, &kOwnerMark, sizeof(kOwnerMark));
        const std::uint32_t preserved =
            *reinterpret_cast<const std::uint32_t*>(target + kRow_Flags) & kFlag_Preserved;
        std::memcpy(target, wanted.data(), kRow_Flags);
        ClearWideFaceFova(target);
        *reinterpret_cast<std::uint32_t*>(target + kRow_Owner) = kOwnerMark;
        *reinterpret_cast<std::uint32_t*>(target + kRow_Flags) = flags | preserved;
        owned.bytes = wanted;
        owned.recorded = true;
        owned.fallback = true;
        owned.donorId = donorId;
        outDonorId = donorId;
        return true;
    }
}
