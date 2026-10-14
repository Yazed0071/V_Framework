#include "pch.h"

#include "PlayerAppearance_LoadPlayerFv2s.h"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <unordered_map>

#include "AddressSet.h"
#include "CustomHeadRegistry.h"
#include "FoxPathInternal.h"
#include "HeadMotion.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "log.h"

namespace
{
    using LoadPlayerFv2s_t = std::uint64_t (__fastcall*)(
        void* impl, std::uint64_t* fv2Info, void* info,
        std::uint64_t playerIndex, std::uint32_t implSlot);
    using LoadPlayerFv2sSubset_t = std::uint64_t (__fastcall*)(
        void* listener, void* blockGroup, std::uint32_t blockIndex);
    using CreateFv2ResourceManager_t = void* (__fastcall*)(void* file);
    using GetShell_t          = void* (__fastcall*)(void* op, std::uint64_t key);
    using GetShellForBlock_t  = void* (__fastcall*)(void* impl, void* blockGroup,
                                                     std::uint32_t blockIndex);
    using GetFaceSystem_t     = void* (__fastcall*)();
    using GetBlockAtIndex_t   = void* (__fastcall*)(void* blockGroup, std::uint32_t index);
    using GetFileForPathId_t  = void* (__fastcall*)(void* block, std::uint64_t pathId);
    using FoxPathCtor_t       = void* (__fastcall*)(void* outPath, std::uint64_t code);
    using FaceFovaCodes_t     = void (__fastcall*)(void* faceSys, std::uint32_t faceId,
                                                   std::uint64_t* out, std::uint32_t count);
    using FaceFovaCode_t      = std::uint64_t* (__fastcall*)(void* faceSys,
                                                             std::uint64_t* out,
                                                             std::uint32_t faceId);

    static LoadPlayerFv2s_t           g_OrigLoadPlayerFv2s       = nullptr;
    static LoadPlayerFv2sSubset_t     g_OrigLoadPlayerFv2sSubset = nullptr;
    static CreateFv2ResourceManager_t g_CreateFv2ResourceManager = nullptr;
    static GetShell_t                 g_GetShell                 = nullptr;
    static GetShellForBlock_t         g_GetShellForBlock         = nullptr;
    static GetFaceSystem_t            g_GetFaceSystem            = nullptr;
    static GetBlockAtIndex_t          g_GetBlockAtIndex          = nullptr;
    static GetFileForPathId_t         g_GetFileForPathId         = nullptr;
    static FoxPathCtor_t              g_FoxPathCtor              = nullptr;

    static void* g_LoadPlayerFv2sTarget       = nullptr;
    static void* g_LoadPlayerFv2sSubsetTarget = nullptr;
    static std::atomic<bool> g_Ready{ false };
    static std::atomic<void*> g_LocalHeadImpl{ nullptr };

    constexpr std::size_t   kImpl_ResidentOp     = 0x148;
    constexpr std::size_t   kImpl_StreamedOp     = 0x150;
    constexpr std::size_t   kImpl_ResidentCount  = 0x168;
    constexpr std::size_t   kImpl_SlotState      = 0x17C;
    constexpr std::size_t   kOp_BlockGroup       = 0x50;
    constexpr std::uint64_t kShellKeyResident    = 0x298F727BC63Bull;
    constexpr std::uint64_t kShellKeyStreamed    = 0x5B7BBFF0FE05ull;
    constexpr std::uint32_t kMaxImplSlots        = 16;

    constexpr std::size_t   kShell_PathCount     = 13;
    constexpr std::size_t   kShell_LoadByteBase  = 0x68;
    constexpr std::size_t   kShell_ContainerBase = 0x78;
    constexpr std::size_t   kShell_BlockIndex    = 0xE0;
    constexpr std::size_t   kShell_TypeInfo      = 0xF0;

    constexpr std::size_t   kPath_Face           = 2;
    constexpr std::size_t   kPath_FaceDeco       = 4;
    constexpr std::size_t   kPath_Hair           = 5;
    constexpr std::size_t   kPath_HairDeco       = 6;
    constexpr std::size_t   kPath_DdLayerPack    = 8;
    constexpr std::size_t   kPath_Horn           = 12;

    constexpr std::size_t   kContainer_Face      = 2;
    constexpr std::size_t   kContainer_Hair      = 5;
    constexpr std::size_t   kContainer_Horn      = 0xC;

    constexpr std::size_t   kFv2_Face            = 1;
    constexpr std::size_t   kFv2_FaceDeco        = 2;
    constexpr std::size_t   kFv2_Hair            = 3;
    constexpr std::size_t   kFv2_HairDeco        = 4;
    constexpr std::size_t   kFv2_Horn            = 5;
    constexpr std::size_t   kFv2_ContainerOffset = 0xB;

    constexpr std::size_t   kTypeInfo_PlayerType = 0x00;
    constexpr std::size_t   kTypeInfo_PartsType  = 0x01;
    constexpr std::size_t   kTypeInfo_CamoType   = 0x02;
    constexpr std::size_t   kTypeInfo_ArmType    = 0x03;
    constexpr std::size_t   kTypeInfo_FaceId     = 0x04;
    constexpr std::size_t   kTypeInfo_FaceEquip  = 0x06;
    constexpr std::size_t   kTypeInfo_Flags      = 0x56;

    constexpr std::size_t   kFaceSys_Fv2Codes    = 0x18;
    constexpr std::size_t   kFaceSys_FpkCodes    = 0x20;
    constexpr std::size_t   kFaceSys_FaceFv2     = 0x28;
    constexpr std::size_t   kFaceSys_HairFv2     = 0x30;
    constexpr std::size_t   kFaceSys_FaceFpk     = 0x40;

    constexpr std::size_t   kPartsInfo_FaceEquip = 0x68;
    constexpr std::size_t   kPartsInfo_PlayerType = 0x40;
    constexpr std::size_t   kPartsInfo_FaceId = 0x60;
    constexpr std::size_t   kPartsInfo_CopySize = 0xF0;

    constexpr std::size_t   kFile_State          = 0x40;
    constexpr std::size_t   kFile_Data           = 0x60;
    constexpr std::uint8_t  kFileStateLoaded     = 2;
    constexpr std::size_t   kContainer_ModelCount = 0x20;

    struct HeadAddresses
    {
        std::uintptr_t loadPlayerFv2s;
        std::uintptr_t loadPlayerFv2sSubset;
        std::uintptr_t createFv2ResourceManager;
        std::uintptr_t getShell;
        std::uintptr_t getShellForBlock;
        std::uintptr_t getFaceSystem;
        std::uintptr_t getFaceFovaPathIdAtFaceId;
        std::uintptr_t getFaceFovaFpkPathIdAtFaceId;
    };

    static HeadAddresses AddressesForBuild()
    {
        switch (gGameBuild)
        {
        case ::AddressSetRuntime::GameBuild::En_1_0_15_4a:
        case ::AddressSetRuntime::GameBuild::En_1_0_15_4:
            return { 0x1409b2750ull, 0x1409b3770ull, 0x1404e39d0ull,
                     0x1409b3670ull, 0x1409b3170ull, 0x1409f1fe0ull,
                     0x1409f09d0ull, 0x1409f0860ull };
        case ::AddressSetRuntime::GameBuild::Jp_1_0_15_4a:
        case ::AddressSetRuntime::GameBuild::Jp_1_0_15_4:
            return { 0x1409b2660ull, 0x1409b3680ull, 0x1404e3dc0ull,
                     0x1409b3580ull, 0x1409b3080ull, 0x1409f1e00ull,
                     0x1409f07f0ull, 0x1409f0680ull };
        case ::AddressSetRuntime::GameBuild::En_1_0_15_3:
            return { 0x1409b1ae0ull, 0x1409b2b00ull, 0x144a43950ull,
                     0x146235c90ull, 0x146233a90ull, 0x146323c30ull,
                     0x14631c0f0ull, 0x14631b870ull };
        case ::AddressSetRuntime::GameBuild::Jp_1_0_15_3:
            return { 0x1409b15c0ull, 0x1409b25e0ull, 0x1448a71f0ull,
                     0x147ee6ab0ull, 0x147ee5050ull, 0x147fd3170ull,
                     0x147fcc600ull, 0x147fcbf70ull };
        default:
            return { 0, 0, 0, 0, 0, 0, 0, 0 };
        }
    }

    constexpr std::uint8_t kFaceGetterMovR9Rcx[]  = { 0x4C, 0x8B, 0xC9 };
    constexpr std::uint8_t kFaceGetterMovR9RcxAlt[] = { 0x49, 0x89, 0xC9 };
    constexpr std::uint8_t kFaceGetterCmpFaceId[] =
    {
        0x41, 0x81, 0xF8, 0x84, 0x03, 0x00, 0x00
    };

    using ModelFromHandle_t = void* (__fastcall*)(void* container, std::uint32_t index);

    static FaceFovaCode_t     g_OrigGetFaceFovaPathIdAtFaceId    = nullptr;
    static FaceFovaCode_t     g_OrigGetFaceFovaFpkPathIdAtFaceId = nullptr;
    static void*              g_FaceFovaPathTarget               = nullptr;
    static void*              g_FaceFovaFpkPathTarget            = nullptr;
    static std::atomic<bool>  g_FaceGettersReady{ false };
    static ModelFromHandle_t  g_ModelFromHandle                  = nullptr;

    struct BranchFaceArm
    {
        std::uint64_t fv2    = 0;
        std::uint64_t fpk    = 0;
        std::uint16_t faceId = 0;
        bool          armed  = false;
    };
    static thread_local BranchFaceArm t_BranchFaceArm{};
    static thread_local bool          t_BranchFaceServed = false;

    static std::uint64_t PathAt(const std::uint8_t* shell, std::size_t index)
    {
        return *reinterpret_cast<const std::uint64_t*>(shell + index * 8);
    }

    static void*& ContainerAt(std::uint8_t* shell, std::size_t index)
    {
        return *reinterpret_cast<void**>(shell + kShell_ContainerBase + index * 8);
    }

    static bool HasModels(void* container)
    {
        return container
            && *reinterpret_cast<std::uint32_t*>(
                   static_cast<std::uint8_t*>(container) + kContainer_ModelCount) != 0;
    }

    static void* ShellForImplSlot(void* impl, std::uint32_t implSlot, void** outOp)
    {
        auto* base = static_cast<std::uint8_t*>(impl);
        const std::uint32_t residentCount =
            *reinterpret_cast<std::uint32_t*>(base + kImpl_ResidentCount);
        const bool resident = implSlot < residentCount;
        void* op = *reinterpret_cast<void**>(
            base + (resident ? kImpl_ResidentOp : kImpl_StreamedOp));
        if (!op)
            return nullptr;
        if (outOp)
            *outOp = op;
        const std::uint64_t key = resident
            ? kShellKeyResident + implSlot
            : kShellKeyStreamed + (implSlot - residentCount);
        return g_GetShell(op, key);
    }

    static bool TypeInfoMatchesShell(const std::uint8_t* shell, const std::uint8_t* typeInfo)
    {
        const std::uint8_t* ti = shell + kShell_TypeInfo;
        if (ti[kTypeInfo_PlayerType] != typeInfo[kTypeInfo_PlayerType]
            || ti[kTypeInfo_PartsType] != typeInfo[kTypeInfo_PartsType]
            || ti[kTypeInfo_CamoType] != typeInfo[kTypeInfo_CamoType]
            || ti[kTypeInfo_ArmType] != typeInfo[kTypeInfo_ArmType]
            || ti[kTypeInfo_FaceId] != typeInfo[kTypeInfo_FaceId]
            || ti[kTypeInfo_FaceId + 1] != typeInfo[kTypeInfo_FaceId + 1]
            || ti[kTypeInfo_FaceEquip] != typeInfo[kTypeInfo_FaceEquip])
            return false;
        return ((ti[kTypeInfo_Flags] ^ typeInfo[kTypeInfo_Flags]) & 0x7) == 0;
    }

    static std::uint16_t TypeInfoFaceId(const std::uint8_t* ti)
    {
        return static_cast<std::uint16_t>(
            ti[kTypeInfo_FaceId] | (ti[kTypeInfo_FaceId + 1] << 8));
    }

    static FaceFovaCode_t FaceSysSingle(void* faceSys, std::size_t slot)
    {
        void** vt = *static_cast<void***>(faceSys);
        return reinterpret_cast<FaceFovaCode_t>(vt[slot / sizeof(void*)]);
    }

    static FaceFovaCodes_t FaceSysMulti(void* faceSys, std::size_t slot)
    {
        void** vt = *static_cast<void***>(faceSys);
        return reinterpret_cast<FaceFovaCodes_t>(vt[slot / sizeof(void*)]);
    }

    static std::uint64_t ReadFaceCode(void* faceSys, std::size_t slot, std::uint16_t faceId)
    {
        std::uint64_t scratch = 0;
        const std::uint64_t* code = FaceSysSingle(faceSys, slot)(faceSys, &scratch, faceId);
        return code ? *code : 0;
    }

    struct Accessory
    {
        std::uint64_t fv2 = 0;
        std::uint64_t fpk = 0;
        bool fromBranch = false;
    };

    static bool AccessoryFpkInstalled(const outfit::DdHeadLayers& layers)
    {
        if (fox::detail::PathExistsByCode(layers.branchFpk))
            return true;
        static std::atomic<const char*> s_lastMissing{ nullptr };
        if (s_lastMissing.exchange(layers.name, std::memory_order_relaxed) != layers.name)
            LogWarn("DdHeadLayers: head '%s': fpk not installed, using face 0x%X",
                layers.name ? layers.name : "?",
                static_cast<unsigned>(layers.enemyFaceId));
        return false;
    }

    static bool ResolveAccessory(void* faceSys, const outfit::DdHeadLayers& layers,
                                 Accessory* out)
    {
        if (outfit::HasBranchModelPair(layers) && AccessoryFpkInstalled(layers))
        {
            out->fv2 = layers.branchFv2;
            out->fpk = layers.branchFpk;
            out->fromBranch = true;
            return true;
        }
        out->fromBranch = false;
        out->fv2 = ReadFaceCode(faceSys, kFaceSys_FaceFv2, layers.enemyFaceId);
        out->fpk = ReadFaceCode(faceSys, kFaceSys_FaceFpk, layers.enemyFaceId);
        return out->fv2 != 0 && out->fpk != 0;
    }

    static void LogAccessoryOnce(const outfit::DdHeadLayers& layers)
    {
        static std::atomic<const char*> s_last{ nullptr };
        if (s_last.exchange(layers.name) == layers.name)
            return;
        LogWarn("DdHeadLayers: head '%s': no fv2/fpk or face row %u model, shows nothing",
            layers.name ? layers.name : "?",
            static_cast<unsigned>(layers.enemyFaceId));
    }

    static void LogAccessoryModelOnce(const outfit::DdHeadLayers& layers, bool fromBranch)
    {
        static std::atomic<const char*> s_last{ nullptr };
        if (s_last.exchange(layers.name) == layers.name)
            return;
        LogWarn("DdHeadLayers: head '%s': %s has no loadable model, shows nothing",
            layers.name ? layers.name : "?",
            fromBranch ? "fv2/fpk pair" : "face row");
    }

    static bool PathAlreadyQueued(const std::uint8_t* shell, std::uint64_t code)
    {
        for (std::size_t i = 0; i < kShell_PathCount; ++i)
            if (PathAt(shell, i) == code)
                return true;
        return false;
    }

    static void QueuePath(std::uint8_t* shell, std::size_t index, std::uint64_t code)
    {
        if (code == 0 || PathAt(shell, index) != 0 || PathAlreadyQueued(shell, code))
            return;
        g_FoxPathCtor(shell + index * 8, code);
        shell[kShell_LoadByteBase + index] = 0;
    }

    static bool QueueLayerPack(std::uint8_t* shell, std::size_t index,
        std::uint64_t code, const outfit::DdHeadLayers& layers, std::size_t layer)
    {
        if (!outfit::IsFpkPathCode(code))
            return false;
        if (!fox::detail::PathExistsByCode(code))
        {
            static std::atomic<std::uint64_t> s_lastMissing[4]{};
            static constexpr const char* names[] = {
                "faceFovaFpkPath", "faceDecoFovaFpkPath",
                "hairFovaFpkPath", "hairDecoFovaFpkPath"
            };
            if (s_lastMissing[layer].exchange(code, std::memory_order_relaxed) != code)
                LogWarn("DdHeadLayers: head '%s': %s %016llX not installed, original pack kept",
                    layers.name ? layers.name : "?",
                    names[layer], static_cast<unsigned long long>(code));
            return false;
        }
        g_FoxPathCtor(shell + index * 8, code);
        shell[kShell_LoadByteBase + index] = 0;
        return true;
    }

    static void DeduplicateLayerPack(std::uint8_t* shell, std::size_t index)
    {
        const auto code = PathAt(shell, index);
        if (!code)
            return;
        for (std::size_t i = 0; i < kShell_PathCount; ++i)
        {
            if (i != index && PathAt(shell, i) == code)
            {
                g_FoxPathCtor(shell + index * 8, 0);
                shell[kShell_LoadByteBase + index] = 0;
                return;
            }
        }
    }

    static void* LoadedFile(void* block, std::uint64_t code)
    {
        if (!block || code == 0)
            return nullptr;
        void* file = g_GetFileForPathId(block, code);
        if (!file)
            return nullptr;
        auto* f = static_cast<std::uint8_t*>(file);
        if (f[kFile_State] != kFileStateLoaded
            || *reinterpret_cast<void**>(f + kFile_Data) == nullptr)
            return nullptr;
        return file;
    }

    static void* MakeContainer(void* block, std::uint64_t fv2Code)
    {
        void* file = LoadedFile(block, fv2Code);
        return file ? g_CreateFv2ResourceManager(file) : nullptr;
    }

    static std::uint64_t LayerFv2Code(void* block, const outfit::DdHeadLayers& layers,
        std::uint64_t overrideCode, std::uint64_t fallbackCode, std::size_t layer)
    {
        if (!overrideCode)
            return fallbackCode;
        if (LoadedFile(block, overrideCode))
            return overrideCode;
        static std::atomic<std::uint64_t> s_lastMissing[4]{};
        static constexpr const char* names[] = {
            "faceFovaPath", "faceDecoFovaPath", "hairFovaPath", "hairDecoFovaPath"
        };
        if (s_lastMissing[layer].exchange(overrideCode, std::memory_order_relaxed)
            != overrideCode)
            LogWarn("DdHeadLayers: head '%s': %s %016llX not loaded, original FV2 used",
                layers.name ? layers.name : "?",
                names[layer], static_cast<unsigned long long>(overrideCode));
        return fallbackCode;
    }

    static std::uint64_t EffectiveFaceFv2(void* faceSys, std::uint16_t faceId,
        const outfit::DdHeadLayers& layers)
    {
        const auto faceFpk = layers.paths.faceFovaFpk
            ? layers.paths.faceFovaFpk : layers.branchFpk;
        if ((layers.flags & outfit::kHeadLayerKeepFace) && layers.paths.faceFova
            && outfit::ddheadlayers::CanReplaceFace()
            && outfit::IsFpkPathCode(faceFpk)
            && fox::detail::PathExistsByCode(faceFpk))
            return layers.paths.faceFova;
        return ReadFaceCode(faceSys, kFaceSys_FaceFv2, faceId);
    }

    static bool QueueLayerPaths(std::uint8_t* shell, const std::uint8_t* ti,
                                const outfit::DdHeadLayers& layers)
    {
        if (PathAt(shell, kPath_Face) == 0)
            return true;

        void* faceSys = g_GetFaceSystem();
        if (!faceSys)
            return true;

        const bool faceDecoPack = (layers.flags & outfit::kHeadLayerKeepFaceDeco)
            && layers.paths.faceDecoFova
            && QueueLayerPack(shell, kPath_FaceDeco, layers.paths.faceDecoFovaFpk, layers, 1);
        const bool hairPack = (layers.flags & outfit::kHeadLayerKeepHair)
            && layers.paths.hairFova
            && QueueLayerPack(shell, kPath_Hair, layers.paths.hairFovaFpk, layers, 2);
        const bool hairDecoPack = (layers.flags & outfit::kHeadLayerKeepHair)
            && (layers.flags & outfit::kHeadLayerKeepHairDeco) && layers.paths.hairDecoFova
            && QueueLayerPack(shell, kPath_HairDeco, layers.paths.hairDecoFovaFpk, layers, 3);

        if (layers.flags & outfit::kHeadLayerKeepHair)
        {
            std::uint64_t codes[4] = {};
            FaceSysMulti(faceSys, kFaceSys_FpkCodes)(faceSys, TypeInfoFaceId(ti), codes, 4);
            QueuePath(shell, kPath_Hair, codes[2]);
            if (layers.flags & outfit::kHeadLayerKeepHairDeco)
                QueuePath(shell, kPath_HairDeco, codes[3]);
        }

        bool accessoryResolved = true;
        if (layers.flags & outfit::kHeadLayerKeepFace)
        {
            Accessory acc{};
            accessoryResolved = ResolveAccessory(faceSys, layers, &acc);
            if (accessoryResolved)
                QueuePath(shell, kPath_Horn, acc.fpk);
        }
        if ((layers.paths.faceFova || layers.paths.faceDecoFova
             || layers.paths.hairFova || layers.paths.hairDecoFova)
            && outfit::IsFpkPathCode(layers.branchFpk)
            && fox::detail::PathExistsByCode(layers.branchFpk))
            QueuePath(shell, kPath_DdLayerPack, layers.branchFpk);
        if (faceDecoPack)
            DeduplicateLayerPack(shell, kPath_FaceDeco);
        if (hairPack)
            DeduplicateLayerPack(shell, kPath_Hair);
        if (hairDecoPack)
            DeduplicateLayerPack(shell, kPath_HairDeco);
        return accessoryResolved;
    }

    static bool QueueLayerPathsSEH(std::uint8_t* shell, const std::uint8_t* ti,
                                   const outfit::DdHeadLayers& layers)
    {
        __try
        {
            return QueueLayerPaths(shell, ti, layers);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return true;
        }
    }

    static bool AddLayerContainers(void* impl, void* blockGroup, std::uint32_t blockIndex,
                                   outfit::DdHeadLayers* outLayers)
    {
        auto* shell = static_cast<std::uint8_t*>(
            g_GetShellForBlock(impl, blockGroup, blockIndex));
        if (!shell)
            return false;
        const std::uint8_t* ti = shell + kShell_TypeInfo;
        if (!outfit::TryGetDdHeadLayers(ti[kTypeInfo_FaceEquip],
                                        ti[kTypeInfo_PlayerType], outLayers))
            return false;
        if (!ContainerAt(shell, kContainer_Face))
            return false;

        void* faceSys = g_GetFaceSystem();
        void* block = g_GetBlockAtIndex(blockGroup, blockIndex);
        if (!faceSys || !block)
            return false;

        const std::uint16_t faceId = TypeInfoFaceId(ti);

        if ((outLayers->flags & outfit::kHeadLayerKeepHair)
            && !ContainerAt(shell, kContainer_Hair))
            ContainerAt(shell, kContainer_Hair) =
                MakeContainer(block, LayerFv2Code(block, *outLayers,
                    outLayers->paths.hairFova,
                    ReadFaceCode(faceSys, kFaceSys_HairFv2, faceId), 2));

        if ((outLayers->flags & outfit::kHeadLayerKeepFace)
            && !ContainerAt(shell, kContainer_Horn))
        {
            Accessory acc{};
            if (ResolveAccessory(faceSys, *outLayers, &acc)
                && acc.fv2 != EffectiveFaceFv2(faceSys, faceId, *outLayers))
            {
                void* container = MakeContainer(block, acc.fv2);
                ContainerAt(shell, kContainer_Horn) = container;
                if (!container)
                    LogAccessoryModelOnce(*outLayers, acc.fromBranch);
            }
        }
        return true;
    }

    static void AddLayerContainersSEH(void* impl, void* blockGroup, std::uint32_t blockIndex)
    {
        outfit::DdHeadLayers layers{};
        __try
        {
            AddLayerContainers(impl, blockGroup, blockIndex, &layers);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            static std::atomic<int> s_faults{ 0 };
            if (s_faults.fetch_add(1, std::memory_order_relaxed) < 4)
                LogWarn("DdHeadLayers: kept hair/head model build faulted, layer off this load");
        }
    }

    static void LogFaceModelMissingOnce(std::uint8_t slot, std::uint8_t playerType,
                                        const char* cause)
    {
        outfit::DdHeadLayers branch{};
        const bool known = outfit::TryGetDdHeadBranch(slot, playerType, &branch);
        static std::atomic<const char*> s_last{ nullptr };
        if (known && s_last.exchange(branch.name) == branch.name)
            return;
        LogWarn("DdHeadLayers: head '%s': %s, face shows nothing",
            known && branch.name ? branch.name : "?", cause);
    }

    static void ApplyLayerFiles(void* impl, std::uint64_t* rec, std::uint32_t implSlot)
    {
        void* op = nullptr;
        auto* shell = static_cast<std::uint8_t*>(ShellForImplSlot(impl, implSlot, &op));
        if (!shell || !op)
            return;
        const std::uint8_t* ti = shell + kShell_TypeInfo;
        const std::uint8_t playerType = ti[kTypeInfo_PlayerType];
        const std::uint8_t slot = ti[kTypeInfo_FaceEquip];
        if (!outfit::IsDdPlayerType(playerType) || !outfit::IsCustomHeadSlot(slot))
            return;

        const std::size_t faceContainer = kFv2_ContainerOffset + kFv2_Face;
        const std::size_t hairContainer = kFv2_ContainerOffset + kFv2_Hair;
        const std::size_t hornContainer = kFv2_ContainerOffset + kFv2_Horn;

        outfit::DdHeadLayers layers{};
        if (rec[kFv2_Face] && outfit::TryGetDdHeadLayers(slot, playerType, &layers))
        {
            void* faceSys = g_GetFaceSystem();
            void* blockGroup = *reinterpret_cast<void**>(
                static_cast<std::uint8_t*>(op) + kOp_BlockGroup);
            void* block = g_GetBlockAtIndex(
                blockGroup, *reinterpret_cast<std::uint32_t*>(shell + kShell_BlockIndex));

            if ((layers.flags & outfit::kHeadLayerKeepFaceDeco) == 0)
                rec[kFv2_FaceDeco] = 0;

            if (faceSys && block)
            {
                std::uint64_t codes[4] = {};
                FaceSysMulti(faceSys, kFaceSys_Fv2Codes)(faceSys, TypeInfoFaceId(ti), codes, 4);

                if ((layers.flags & outfit::kHeadLayerKeepFaceDeco)
                    && layers.paths.faceDecoFova)
                {
                    const auto code = LayerFv2Code(block, layers,
                        layers.paths.faceDecoFova, codes[1], 1);
                    if (void* file = LoadedFile(block, code))
                        rec[kFv2_FaceDeco] = reinterpret_cast<std::uint64_t>(file);
                }

                void* hair = ContainerAt(shell, kContainer_Hair);
                if ((layers.flags & outfit::kHeadLayerKeepHair)
                    && !rec[kFv2_Hair] && HasModels(hair))
                {
                    const auto code = LayerFv2Code(block, layers,
                        layers.paths.hairFova, codes[2], 2);
                    if (void* file = LoadedFile(block, code))
                    {
                        rec[kFv2_Hair] = reinterpret_cast<std::uint64_t>(file);
                        rec[hairContainer] = reinterpret_cast<std::uint64_t>(hair);
                    }
                }

                if ((layers.flags & outfit::kHeadLayerKeepHairDeco)
                    && (layers.flags & outfit::kHeadLayerKeepHair)
                    && !rec[kFv2_HairDeco] && rec[kFv2_Hair] && rec[hairContainer])
                {
                    const auto code = LayerFv2Code(block, layers,
                        layers.paths.hairDecoFova, codes[3], 3);
                    if (void* file = LoadedFile(block, code))
                        rec[kFv2_HairDeco] = reinterpret_cast<std::uint64_t>(file);
                }

                void* horn = ContainerAt(shell, kContainer_Horn);
                if ((layers.flags & outfit::kHeadLayerKeepFace)
                    && !rec[kFv2_Horn] && HasModels(horn))
                {
                    Accessory acc{};
                    if (ResolveAccessory(faceSys, layers, &acc))
                    {
                        if (void* file = LoadedFile(block, acc.fv2))
                        {
                            rec[kFv2_Horn] = reinterpret_cast<std::uint64_t>(file);
                            rec[hornContainer] = reinterpret_cast<std::uint64_t>(horn);
                        }
                    }
                }
            }
        }

        if (rec[faceContainer] && g_ModelFromHandle
            && !g_ModelFromHandle(reinterpret_cast<void*>(rec[faceContainer]), 0))
        {
            rec[kFv2_Face]     = 0;
            rec[faceContainer] = 0;
            LogFaceModelMissingOnce(slot, playerType, "face container has no model");
        }

        if (rec[kFv2_FaceDeco]
            && !HasModels(reinterpret_cast<void*>(rec[faceContainer])))
            rec[kFv2_FaceDeco] = 0;
        if (rec[kFv2_HairDeco]
            && !HasModels(reinterpret_cast<void*>(rec[hairContainer])))
            rec[kFv2_HairDeco] = 0;
    }

    static void ApplyLayerFilesSEH(void* impl, std::uint64_t* rec, std::uint32_t implSlot)
    {
        __try
        {
            ApplyLayerFiles(impl, rec, implSlot);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            static std::atomic<int> s_faults{ 0 };
            if (s_faults.fetch_add(1, std::memory_order_relaxed) < 4)
                LogWarn("DdHeadLayers: face layer choice faulted, engine choice kept");
        }
    }

    static std::uint64_t* ServeArmedFace(void* faceSys, std::uint64_t* out,
                                         std::uint32_t faceId, bool wantFpk,
                                         FaceFovaCode_t orig)
    {
        const BranchFaceArm arm = t_BranchFaceArm;
        t_BranchFaceArm = BranchFaceArm{};
        if (arm.armed && arm.faceId == faceId && out)
        {
            *out = wantFpk ? arm.fpk : arm.fv2;
            t_BranchFaceServed = true;
            return out;
        }
        return orig(faceSys, out, faceId);
    }

    static std::uint64_t* __fastcall hkGetFaceFovaPathIdAtFaceId(
        void* faceSys, std::uint64_t* out, std::uint32_t faceId)
    {
        return ServeArmedFace(faceSys, out, faceId, false,
                              g_OrigGetFaceFovaPathIdAtFaceId);
    }

    static std::uint64_t* __fastcall hkGetFaceFovaFpkPathIdAtFaceId(
        void* faceSys, std::uint64_t* out, std::uint32_t faceId)
    {
        return ServeArmedFace(faceSys, out, faceId, true,
                              g_OrigGetFaceFovaFpkPathIdAtFaceId);
    }

    static bool FaceGetterPrologueMatchesSEH(void* fn)
    {
        __try
        {
            const auto* bytes = static_cast<const std::uint8_t*>(fn);
            const bool movOk =
                std::memcmp(bytes, kFaceGetterMovR9Rcx, sizeof(kFaceGetterMovR9Rcx)) == 0
                || std::memcmp(bytes, kFaceGetterMovR9RcxAlt,
                               sizeof(kFaceGetterMovR9RcxAlt)) == 0;
            return movOk
                && std::memcmp(bytes + sizeof(kFaceGetterMovR9Rcx), kFaceGetterCmpFaceId,
                               sizeof(kFaceGetterCmpFaceId)) == 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    static void ReportBranchFaceModelSEH(void* impl, void* blockGroup,
                                         std::uint32_t blockIndex)
    {
        std::uint8_t slot = 0;
        std::uint8_t playerType = 0xFF;
        bool built = true;
        __try
        {
            auto* shell = static_cast<std::uint8_t*>(
                g_GetShellForBlock(impl, blockGroup, blockIndex));
            if (!shell)
                return;
            const std::uint8_t* ti = shell + kShell_TypeInfo;
            slot       = ti[kTypeInfo_FaceEquip];
            playerType = ti[kTypeInfo_PlayerType];
            built      = HasModels(ContainerAt(shell, kContainer_Face));
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return;
        }
        if (!built)
            LogFaceModelMissingOnce(slot, playerType, "fv2 not in its fpk or has no model");
    }

    static std::uint64_t __fastcall hkLoadPlayerFv2sSubset(
        void* listener, void* blockGroup, std::uint32_t blockIndex)
    {
        t_BranchFaceServed = false;
        const std::uint64_t result =
            g_OrigLoadPlayerFv2sSubset(listener, blockGroup, blockIndex);
        const bool served = t_BranchFaceServed;
        t_BranchFaceServed = false;
        if (listener && blockGroup && g_Ready.load(std::memory_order_acquire)
            && !MissionCodeGuard::ShouldBypassHooks())
        {
            if (served)
                ReportBranchFaceModelSEH(static_cast<std::uint8_t*>(listener) - 0x30,
                                         blockGroup, blockIndex);
            AddLayerContainersSEH(static_cast<std::uint8_t*>(listener) - 0x30,
                                  blockGroup, blockIndex);
        }
        return result;
    }

    struct FaceEquipSwap
    {
        std::uint8_t* cell  = nullptr;
        std::uint8_t  saved = 0;
    };

    static FaceEquipSwap HideDroppedHeadSEH(void* impl, void* info,
                                            std::uint64_t playerIndex,
                                            std::uint32_t implSlot)
    {
        FaceEquipSwap swap{};
        __try
        {
            auto* shell = static_cast<std::uint8_t*>(
                ShellForImplSlot(impl, implSlot, nullptr));
            if (!shell)
                return swap;
            const std::uint8_t* ti = shell + kShell_TypeInfo;
            if (!outfit::IsDdPlayerType(ti[kTypeInfo_PlayerType])
                || ti[kTypeInfo_FaceEquip] != 0)
                return swap;
            auto* faceEquipIds = *reinterpret_cast<std::uint8_t**>(
                static_cast<std::uint8_t*>(info) + kPartsInfo_FaceEquip);
            const std::uint32_t index = static_cast<std::uint32_t>(playerIndex);
            if (!faceEquipIds || faceEquipIds[index] == 0)
                return swap;
            swap.cell  = faceEquipIds + index;
            swap.saved = *swap.cell;
            *swap.cell = 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            swap.cell = nullptr;
            static std::atomic<int> s_faults{ 0 };
            if (s_faults.fetch_add(1, std::memory_order_relaxed) < 4)
                LogWarn("DdHeadLayers: dropped-head parts read faulted, DD hair hidden this load");
        }
        return swap;
    }

    static void RestoreDroppedHeadSEH(const FaceEquipSwap& swap)
    {
        if (!swap.cell)
            return;
        __try
        {
            if (*swap.cell == 0)
                *swap.cell = swap.saved;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    struct StaffFaceLoadSnapshot
    {
        void* shell = nullptr;
        std::uint16_t nativeFaceId = 0;
        std::uint16_t visualFaceId = 0;
        std::uint8_t playerType = 0;
    };

    static std::mutex g_StaffFaceLoadMutex;
    static std::unordered_map<void*, StaffFaceLoadSnapshot> g_StaffFaceLoads;

    static bool ReadStaffFaceLoadSnapshot(void* impl, StaffFaceLoadSnapshot& snapshot)
    {
        std::lock_guard<std::mutex> lock(g_StaffFaceLoadMutex);
        const auto it = g_StaffFaceLoads.find(impl);
        if (it == g_StaffFaceLoads.end())
            return false;
        snapshot = it->second;
        return true;
    }

    struct StaffFaceLoadCopy
    {
        alignas(8) std::uint8_t info[kPartsInfo_CopySize]{};
        std::uint16_t faceIds[kMaxImplSlots]{};
    };

    static bool BuildStaffFaceLoadCopySEH(void* impl, const void* info,
                                         std::uint64_t playerIndex,
                                         std::uint32_t implSlot,
                                         StaffFaceLoadCopy& copy)
    {
        const auto index = static_cast<std::uint32_t>(playerIndex);
        if (!impl || !info || implSlot != 0 || index >= kMaxImplSlots)
            return false;
        StaffFaceLoadSnapshot snapshot{};
        if (!ReadStaffFaceLoadSnapshot(impl, snapshot))
            return false;
        __try
        {
            const auto* shell = static_cast<const std::uint8_t*>(
                ShellForImplSlot(impl, implSlot, nullptr));
            if (!shell || shell != snapshot.shell)
                return false;
            const std::uint8_t* ti = shell + kShell_TypeInfo;
            const auto* base = static_cast<const std::uint8_t*>(info);
            const auto* playerTypes = *reinterpret_cast<const std::uint8_t* const*>(
                base + kPartsInfo_PlayerType);
            const auto* faceIds = *reinterpret_cast<const std::uint16_t* const*>(
                base + kPartsInfo_FaceId);
            if (!playerTypes || !faceIds || !outfit::IsDdPlayerType(playerTypes[index])
                || playerTypes[index] != ti[kTypeInfo_PlayerType]
                || playerTypes[index] != snapshot.playerType)
                return false;
            const std::uint16_t visualFaceId = snapshot.visualFaceId;
            if (faceIds[index] != snapshot.nativeFaceId || visualFaceId == faceIds[index]
                || visualFaceId != TypeInfoFaceId(ti))
                return false;
            std::memcpy(copy.info, info, sizeof(copy.info));
            std::memcpy(copy.faceIds, faceIds, (index + 1) * sizeof(copy.faceIds[0]));
            copy.faceIds[index] = visualFaceId;
            std::uint16_t* privateFaceIds = copy.faceIds;
            std::memcpy(copy.info + kPartsInfo_FaceId, &privateFaceIds, sizeof(privateFaceIds));
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    static std::uint64_t __fastcall hkLoadPlayerFv2s(
        void* impl, std::uint64_t* fv2Info, void* info,
        std::uint64_t playerIndex, std::uint32_t implSlot)
    {
        const bool ready = impl && g_Ready.load(std::memory_order_acquire);
        StaffFaceLoadCopy staffVisualInfo{};
        void* loadInfo = info;
        if (ready && BuildStaffFaceLoadCopySEH(
                impl, info, playerIndex, implSlot, staffVisualInfo))
            loadInfo = staffVisualInfo.info;
        const FaceEquipSwap swap = (ready && info)
            ? HideDroppedHeadSEH(impl, loadInfo, playerIndex, implSlot)
            : FaceEquipSwap{};
        const std::uint64_t result =
            g_OrigLoadPlayerFv2s(impl, fv2Info, loadInfo, playerIndex, implSlot);
        RestoreDroppedHeadSEH(swap);
        if (ready && fv2Info && !MissionCodeGuard::ShouldBypassHooks())
            ApplyLayerFilesSEH(impl, fv2Info, implSlot);
        if (ready && playerIndex == 0 && implSlot == 0)
        {
            g_LocalHeadImpl.store(impl, std::memory_order_release);
            outfit::headmotion::OnPlayerPartsReloaded();
        }
        return result;
    }

    static void* LocalHeadBlockSEH(void* impl)
    {
        __try
        {
            void* op = nullptr;
            auto* shell = static_cast<std::uint8_t*>(ShellForImplSlot(impl, 0, &op));
            if (!shell || !op)
                return nullptr;
            void* blockGroup = *reinterpret_cast<void**>(
                static_cast<std::uint8_t*>(op) + kOp_BlockGroup);
            return g_GetBlockAtIndex(
                blockGroup, *reinterpret_cast<std::uint32_t*>(shell + kShell_BlockIndex));
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    static void* FindLocalHeadFileSEH(void* impl, std::uint64_t pathCode)
    {
        __try
        {
            return LoadedFile(LocalHeadBlockSEH(impl), pathCode);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    static void* ProbeShellSEH(void* impl, std::uint32_t implSlot,
                               std::int32_t* outState)
    {
        __try
        {
            *outState = *reinterpret_cast<std::int32_t*>(
                static_cast<std::uint8_t*>(impl) + kImpl_SlotState + implSlot * 4);
            return ShellForImplSlot(impl, implSlot, nullptr);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    static bool ReadSlotStateSEH(void* impl, std::uint32_t implSlot, std::int32_t* out)
    {
        __try
        {
            *out = *reinterpret_cast<std::int32_t*>(
                static_cast<std::uint8_t*>(impl) + kImpl_SlotState + implSlot * 4);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    static bool TypeInfoMatchesShellSEH(void* shell, const std::uint8_t* typeInfo)
    {
        __try
        {
            return TypeInfoMatchesShell(static_cast<std::uint8_t*>(shell), typeInfo);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    static bool ForgetShellTypeInfoSEH(void* impl, std::uint32_t implSlot, std::uint8_t camoType)
    {
        __try
        {
            auto* shell = static_cast<std::uint8_t*>(ShellForImplSlot(impl, implSlot, nullptr));
            if (!shell)
                return false;
            shell[kShell_TypeInfo + kTypeInfo_CamoType] = static_cast<std::uint8_t>(~camoType);
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    static bool BuildStaffFaceLoadSnapshotSEH(
        void* impl, const std::uint8_t* nativeTypeInfo,
        const std::uint8_t* visualTypeInfo, StaffFaceLoadSnapshot& snapshot)
    {
        __try
        {
            std::int32_t state = 0;
            void* shell = ProbeShellSEH(impl, 0, &state);
            if (!shell || state == 0 || !TypeInfoMatchesShellSEH(shell, visualTypeInfo))
                return false;
            snapshot.shell = shell;
            snapshot.nativeFaceId = TypeInfoFaceId(nativeTypeInfo);
            snapshot.visualFaceId = TypeInfoFaceId(visualTypeInfo);
            snapshot.playerType = visualTypeInfo[kTypeInfo_PlayerType];
            if (!outfit::IsDdPlayerType(snapshot.playerType)
                || nativeTypeInfo[kTypeInfo_PlayerType] != snapshot.playerType)
                snapshot.visualFaceId = snapshot.nativeFaceId;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }
}

namespace outfit::ddheadlayers
{
    bool Install()
    {
        const HeadAddresses a = AddressesForBuild();
        g_CreateFv2ResourceManager = reinterpret_cast<CreateFv2ResourceManager_t>(
            ResolveGameAddress(a.createFv2ResourceManager));
        g_GetShell = reinterpret_cast<GetShell_t>(ResolveGameAddress(a.getShell));
        g_GetShellForBlock = reinterpret_cast<GetShellForBlock_t>(
            ResolveGameAddress(a.getShellForBlock));
        g_GetFaceSystem = reinterpret_cast<GetFaceSystem_t>(
            ResolveGameAddress(a.getFaceSystem));
        g_GetBlockAtIndex = reinterpret_cast<GetBlockAtIndex_t>(
            ResolveGameAddress(gAddr.Fox_BlockGroup_GetBlockAtIndex));
        g_GetFileForPathId = reinterpret_cast<GetFileForPathId_t>(
            ResolveGameAddress(gAddr.Fox_Block_GetFileForPathId));
        g_FoxPathCtor = reinterpret_cast<FoxPathCtor_t>(
            ResolveGameAddress(gAddr.FoxPath_Path));

        g_LoadPlayerFv2sTarget       = ResolveGameAddress(a.loadPlayerFv2s);
        g_LoadPlayerFv2sSubsetTarget = ResolveGameAddress(a.loadPlayerFv2sSubset);

        if (!g_CreateFv2ResourceManager || !g_GetShell || !g_GetShellForBlock
            || !g_GetFaceSystem || !g_GetBlockAtIndex || !g_GetFileForPathId
            || !g_FoxPathCtor || !g_LoadPlayerFv2sTarget || !g_LoadPlayerFv2sSubsetTarget)
        {
            LogError("DdHeadLayers: no face-layer address for this build, enable*Fova off");
            return false;
        }

        const bool subsetOk = CreateAndEnableHook(
            g_LoadPlayerFv2sSubsetTarget,
            reinterpret_cast<void*>(&hkLoadPlayerFv2sSubset),
            reinterpret_cast<void**>(&g_OrigLoadPlayerFv2sSubset));
        const bool fv2sOk = subsetOk && CreateAndEnableHook(
            g_LoadPlayerFv2sTarget,
            reinterpret_cast<void*>(&hkLoadPlayerFv2s),
            reinterpret_cast<void**>(&g_OrigLoadPlayerFv2s));

        if (!subsetOk || !fv2sOk)
        {
            if (subsetOk)
                DisableAndRemoveHook(g_LoadPlayerFv2sSubsetTarget);
            g_OrigLoadPlayerFv2sSubset = nullptr;
            g_OrigLoadPlayerFv2s = nullptr;
            LogError("DdHeadLayers: LoadPlayerFv2s%s hook failed, enable*Fova off",
                subsetOk ? "" : "SubsetUnk");
            return false;
        }

        g_ModelFromHandle = reinterpret_cast<ModelFromHandle_t>(
            ResolveGameAddress(gAddr.Fox_ModelFromHandle));
        g_Ready.store(true, std::memory_order_release);

        g_FaceFovaPathTarget    = ResolveGameAddress(a.getFaceFovaPathIdAtFaceId);
        g_FaceFovaFpkPathTarget = ResolveGameAddress(a.getFaceFovaFpkPathIdAtFaceId);
        if (!g_FaceFovaPathTarget || !g_FaceFovaFpkPathTarget
            || !FaceGetterPrologueMatchesSEH(g_FaceFovaPathTarget)
            || !FaceGetterPrologueMatchesSEH(g_FaceFovaFpkPathTarget))
        {
            LogError("DdHeadLayers: face fova getters %s, fv2/fpk heads use their face row",
                (g_FaceFovaPathTarget && g_FaceFovaFpkPathTarget)
                    ? "not the expected code" : "have no address");
            return true;
        }

        const bool fv2GetterOk = CreateAndEnableHook(
            g_FaceFovaPathTarget,
            reinterpret_cast<void*>(&hkGetFaceFovaPathIdAtFaceId),
            reinterpret_cast<void**>(&g_OrigGetFaceFovaPathIdAtFaceId));
        const bool fpkGetterOk = fv2GetterOk && CreateAndEnableHook(
            g_FaceFovaFpkPathTarget,
            reinterpret_cast<void*>(&hkGetFaceFovaFpkPathIdAtFaceId),
            reinterpret_cast<void**>(&g_OrigGetFaceFovaFpkPathIdAtFaceId));
        if (!fv2GetterOk || !fpkGetterOk)
        {
            if (fv2GetterOk)
                DisableAndRemoveHook(g_FaceFovaPathTarget);
            g_OrigGetFaceFovaPathIdAtFaceId    = nullptr;
            g_OrigGetFaceFovaFpkPathIdAtFaceId = nullptr;
            LogError("DdHeadLayers: face fova %s getter hook failed, fv2/fpk heads use their face row",
                fv2GetterOk ? "fpk" : "fv2");
            return true;
        }

        g_FaceGettersReady.store(true, std::memory_order_release);
        return true;
    }

    bool IsReady()
    {
        return g_Ready.load(std::memory_order_acquire);
    }

    bool CanReplaceFace()
    {
        return g_Ready.load(std::memory_order_acquire)
            && g_FaceGettersReady.load(std::memory_order_acquire);
    }

    void ArmBranchFace(std::uint16_t faceId, std::uint64_t fv2, std::uint64_t fpk)
    {
        BranchFaceArm arm{};
        arm.fv2    = fv2;
        arm.fpk    = fpk;
        arm.faceId = faceId;
        arm.armed  = true;
        t_BranchFaceArm = arm;
    }

    void DisarmBranchFace()
    {
        t_BranchFaceArm = BranchFaceArm{};
    }

    void Uninstall()
    {
        g_FaceGettersReady.store(false, std::memory_order_release);
        if (g_OrigGetFaceFovaPathIdAtFaceId && g_FaceFovaPathTarget)
            DisableAndRemoveHook(g_FaceFovaPathTarget);
        if (g_OrigGetFaceFovaFpkPathIdAtFaceId && g_FaceFovaFpkPathTarget)
            DisableAndRemoveHook(g_FaceFovaFpkPathTarget);
        g_OrigGetFaceFovaPathIdAtFaceId    = nullptr;
        g_OrigGetFaceFovaFpkPathIdAtFaceId = nullptr;
        g_Ready.store(false, std::memory_order_release);
        if (g_OrigLoadPlayerFv2s && g_LoadPlayerFv2sTarget)
            DisableAndRemoveHook(g_LoadPlayerFv2sTarget);
        if (g_OrigLoadPlayerFv2sSubset && g_LoadPlayerFv2sSubsetTarget)
            DisableAndRemoveHook(g_LoadPlayerFv2sSubsetTarget);
        g_OrigLoadPlayerFv2s       = nullptr;
        g_OrigLoadPlayerFv2sSubset = nullptr;
        g_LocalHeadImpl.store(nullptr, std::memory_order_release);
        std::lock_guard<std::mutex> lock(g_StaffFaceLoadMutex);
        g_StaffFaceLoads.clear();
    }

    void* FindLocalHeadFile(std::uint64_t pathCode)
    {
        void* impl = g_LocalHeadImpl.load(std::memory_order_acquire);
        if (!impl || pathCode == 0 || !g_Ready.load(std::memory_order_acquire))
            return nullptr;
        return FindLocalHeadFileSEH(impl, pathCode);
    }

    void* FindLocalHeadBlock()
    {
        void* impl = g_LocalHeadImpl.load(std::memory_order_acquire);
        if (!impl || !g_Ready.load(std::memory_order_acquire))
            return nullptr;
        return LocalHeadBlockSEH(impl);
    }

    void RecordLocalStaffFaceLoad(void* impl, std::uint32_t implSlot,
                                  const std::uint8_t* nativeTypeInfo,
                                  const std::uint8_t* visualTypeInfo)
    {
        if (!impl || implSlot != 0 || !nativeTypeInfo || !visualTypeInfo || !IsReady())
            return;
        StaffFaceLoadSnapshot snapshot{};
        if (!BuildStaffFaceLoadSnapshotSEH(impl, nativeTypeInfo, visualTypeInfo, snapshot))
            return;
        std::lock_guard<std::mutex> lock(g_StaffFaceLoadMutex);
        if (snapshot.nativeFaceId == snapshot.visualFaceId)
            g_StaffFaceLoads.erase(impl);
        else
            g_StaffFaceLoads[impl] = snapshot;
    }

    bool ForgetShellTypeInfo(void* impl, std::uint32_t implSlot, const std::uint8_t* typeInfo)
    {
        if (!impl || !typeInfo || implSlot >= kMaxImplSlots || !g_Ready.load(std::memory_order_acquire))
            return false;
        return ForgetShellTypeInfoSEH(impl, implSlot, typeInfo[kTypeInfo_CamoType]);
    }

    PartsLoadProbe BeginPartsLoad(void* impl, std::uint32_t implSlot,
                                  const std::uint8_t* typeInfo)
    {
        PartsLoadProbe probe{};
        if (!impl || !typeInfo || implSlot >= kMaxImplSlots
            || !g_Ready.load(std::memory_order_acquire)
            || MissionCodeGuard::ShouldBypassHooks())
            return probe;
        if (!IsDdPlayerType(typeInfo[kTypeInfo_PlayerType])
            || !IsCustomHeadSlot(typeInfo[kTypeInfo_FaceEquip]))
            return probe;

        std::int32_t state = 0;
        void* shell = ProbeShellSEH(impl, implSlot, &state);
        if (!shell)
            return probe;

        probe.impl          = impl;
        probe.shell         = shell;
        probe.implSlot      = implSlot;
        probe.stateBefore   = state;
        probe.matchedBefore = TypeInfoMatchesShellSEH(shell, typeInfo);
        probe.valid         = true;
        return probe;
    }

    void EndPartsLoad(const PartsLoadProbe& probe, const std::uint8_t* typeInfo)
    {
        if (!probe.valid || !typeInfo)
            return;

        std::int32_t stateAfter = 0;
        if (!ReadSlotStateSEH(probe.impl, probe.implSlot, &stateAfter))
            return;

        const bool freshLoad = probe.stateBefore == 1 && stateAfter == 3;
        const bool reload = probe.stateBefore != 0 && probe.stateBefore != 1
            && !probe.matchedBefore && stateAfter == 5
            && TypeInfoMatchesShellSEH(probe.shell, typeInfo);
        if (!freshLoad && !reload)
            return;

        DdHeadLayers layers{};
        if (!TryGetDdHeadLayers(typeInfo[kTypeInfo_FaceEquip],
                                typeInfo[kTypeInfo_PlayerType], &layers))
            return;

        if (!QueueLayerPathsSEH(static_cast<std::uint8_t*>(probe.shell),
                                typeInfo, layers))
            LogAccessoryOnce(layers);
    }
}
