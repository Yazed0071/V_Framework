#include <pch.h>
#include "TppHeli2_Allocate.h"

#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <cstring>

#include "FoxHashes.h"
#include "MissionCodeGuard.h"
#include "log.h"
#include "SupportHeliHookInstall.h"
#include "SupportHeliParts.h"

namespace
{
    using SupportHeliParts::Kind;
    using SupportHeliParts::ModelFile;
    using SupportHeliParts::ModelFileKind;
    using SupportHeliParts::Part;

    using Allocate_t               = std::uint64_t(__fastcall*)(std::uint8_t* heliType, std::uint8_t* desc);
    using GetCurrentBlockPackage_t = std::uint8_t*(__fastcall*)();
    using GetFileForPathId_t       = void*(__fastcall*)(void* block, std::uint64_t pathId);
    using MapFind_t                = void*(__fastcall*)(std::uint8_t* map, std::uint64_t* found, const std::uint64_t* name);
    using Realize_t                = void(__fastcall*)(std::uint8_t* heli2Impl, std::uint32_t instance, std::uint32_t realizedIndex);
    using CreateRealized_t         = std::uint8_t*(__fastcall*)(void* realizedDesc);
    using QuarkFind_t              = std::uint8_t*(__fastcall*)(void* registry, std::uint64_t name);
    using DeleteRealized_t         = void*(__fastcall*)(std::uint8_t* realized, std::uint32_t free);
    using FreeResources_t          = void(__fastcall*)(std::uint8_t* heliType);

    constexpr std::size_t   kDescParameter = 0x10;   // TppHeli2Parameter
    constexpr std::size_t   kPackageBlock  = 0x10;
    constexpr std::uint32_t kTlsSlotsOffset = 0x58;   // TEB ThreadLocalStoragePointer
    constexpr std::size_t   kContextHeap    = 0x30;
    constexpr std::size_t   kContextBase    = 0x38;
    constexpr std::size_t   kContextPackage = 0x40;

    struct BlockContext
    {
        void* heap;
        void* base;
        void* package;
    };
    constexpr std::size_t   kFileLists[SupportHeliParts::kFileListCount] =
        { 0x38, 0x68, 0x98, 0xC8, 0xF8, 0x128 };     // partsFiles, fmdlFiles, fcnpFiles, gsklFiles, vfxFiles, fovaFiles
    constexpr std::size_t   kMotionFile    = 0x158;  // mtarFile
    constexpr std::size_t   kListValues    = 0x28;   // one 0x18-byte file pointer per name, the file first
    constexpr std::size_t   kEntrySize     = 0x18;   // a name entry is as large: the name's string first
    constexpr std::size_t   kFoundIndex    = 1;
    constexpr std::size_t   kFoundNames    = 2;
    constexpr std::uint32_t kNameMissing   = 0xFFFFFFFF;
    constexpr std::size_t   kFilePath      = 0x30;
    constexpr std::uint64_t kNameMask      = 0xFFFFFFFFFFFF;
    constexpr std::size_t   kMostFiles     = SupportHeliParts::kMostModelFiles * SupportHeliParts::kKindCount;
    constexpr std::size_t   kMostChanges   = 1024;
    constexpr char          kPequodParts[] = "/Assets/tpp/parts/mecha/uth/uth0_main0_def.parts";

    // TppHeli2 (the game object type) and its Heli2Impl quark
    constexpr std::size_t   kTypeQuark       = 0x28;
    constexpr std::size_t   kTypeRealized    = 0x48;    // vector of RealizedHeli2Impl, one per block of instances
    constexpr std::size_t   kTypeInstances   = 0x64;
    constexpr std::size_t   kQuarkRegistry   = 0x10;
    constexpr std::size_t   kRegistryFind    = 0x18 / sizeof(void*);
    constexpr std::uint64_t kHeli2ImplName   = 0x89731475623D;
    constexpr std::size_t   kQuarkOffset     = 8;
    constexpr std::size_t   kImplRealized    = 0x170;   // the realized heli Realize hands an instance to
    constexpr std::size_t   kImplUnrealize   = 3;
    constexpr std::size_t   kRealizedDelete  = 0x38 / sizeof(void*);
    constexpr std::int32_t  kHeliSpaces[]    = { 40010, 40020, 40050 };
    constexpr std::size_t   kMostRetired     = 32;

    struct RealizedDesc   // what TppHeli2_Allocate hands RealizedHeli2Impl_Create
    {
        void*         quark;
        std::uint8_t* parameter;
        std::uint32_t zero;
        std::uint32_t firstInstance;
        std::uint32_t instanceCount;
        std::uint32_t pad;
    };

    // every part name the heli asks its file lists for
    constexpr const char* kPartNames[] =
    {
        "Main", "Missile", "Rocket", "Default",
        "PilotL", "PilotR", "GunArmL", "GunArmR", "GunL", "GunR", "Gun", "Shield", "ExtraWeapon",
        "MissileLauncher", "MissileLauncherLL", "MissileLauncherLR", "MissileLauncherRL", "MissileLauncherRR",
        "RocketLauncher", "RocketLauncherLL", "RocketLauncherLR", "RocketLauncherRL", "RocketLauncherRR",
        "Logo", "Glass0", "Glass1", "Glass2", "Glass3", "Glass4", "Glass5",
        "Downwash", "Explosion", "MuzzleFlash", "DownwashWater", "LauncherMuzzleFlash", "LauncherBackFire",
        "Flare", "SearchLight", "BarrelHeat", "BarrelHeatSmoke", "LensFlare",
    };
    constexpr std::size_t kPartNameCount = sizeof(kPartNames) / sizeof(kPartNames[0]);

    constexpr std::uint8_t kAllocateEntry[] =
    {
        0x40, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8D, 0x6C, 0x24,
    };
    constexpr std::uint8_t kGetFileEntry[] =
    {
        0x48, 0x89, 0x54, 0x24, 0x10, 0x56, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xF1, 0x48, 0x85, 0xD2,
    };
    constexpr std::uint8_t kMapFindEntry[] =
    {
        0x48, 0x83, 0xEC, 0x08, 0x8B, 0x41, 0x04, 0x4C, 0x8B, 0xCA, 0x4C, 0x8B, 0xD1, 0x85, 0xC0, 0x75,
    };
    constexpr std::uint8_t kRealizeEntry[] =
    {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x48, 0x89, 0x7C, 0x24, 0x18, 0x41,
    };
    constexpr std::uint8_t kFreeResourcesEntry[] = { 0x48, 0x89, 0x5C, 0x24, 0x18, 0x56, 0x48, 0x83, 0xEC, 0x20 };
    constexpr std::uint8_t kCreateRealizedEntry[] =
    {
        0x40, 0x53, 0x48, 0x83, 0xEC, 0x30, 0x48, 0xC7, 0x44, 0x24, 0x20, 0xFE, 0xFF, 0xFF, 0xFF, 0x48,
    };
    constexpr std::uint8_t kCurrentPackageEntry[] = { 0x8B, 0x0D };   // mov ecx, [the game's TLS index]
    constexpr std::size_t  kCurrentPackageTail    = 6;
    constexpr std::uint8_t kCurrentPackageRest[]  = { 0x65, 0x48, 0x8B, 0x04, 0x25, 0x58, 0x00, 0x00, 0x00, 0xBA, 0x40, 0x00, 0x00, 0x00 };

    struct Change
    {
        void** place;   // a list's file pointer or name pointer
        void*  before;
    };

    struct Changes
    {
        Change               made[kMostChanges];
        volatile std::size_t count = 0;   // read back after a fault inside the guarded block
    };

    struct Picked
    {
        ModelFile   files[kMostFiles];   // the outfit's first, then the guns'
        std::size_t fileCount   = 0;
        std::size_t outfitFiles = 0;
        std::uint8_t outfitId   = 0;
        bool        wholeHeli   = false;
    };

    Allocate_t               g_Orig   = nullptr;
    void*                    g_Target = nullptr;
    GetCurrentBlockPackage_t g_GetCurrentBlockPackage = nullptr;
    GetFileForPathId_t       g_GetFileForPathId = nullptr;
    MapFind_t                g_MapFind = nullptr;
    std::atomic<bool>        g_MissingFileWarned{ false };
    std::atomic<bool>        g_WholeHeliWarned{ false };
    std::atomic<bool>        g_FaultWarned{ false };
    Realize_t                g_OrigRealize = nullptr;
    void*                    g_RealizeTarget = nullptr;
    CreateRealized_t         g_CreateRealized = nullptr;
    std::uint8_t*            g_PequodType = nullptr;       // the Pequod's TppHeli2, its parameter and its block, kept from its load
    std::uint8_t*            g_PequodParameter = nullptr;
    void*                    g_PequodBlock = nullptr;
    BlockContext             g_PequodContext{};
    bool                     g_PequodRealized = false;
    std::uint32_t            g_RealizedInstance = 0;
    std::uint32_t            g_RealizedIndex = 0;
    std::atomic<bool>        g_LayoutWarned{ false };
    FreeResources_t          g_OrigFreeResources = nullptr;
    void*                    g_FreeResourcesTarget = nullptr;
    std::uint8_t*            g_Retired[kMostRetired] = {};   // realized helis a live rebuild replaced: other systems may still point at them
    std::size_t              g_RetiredCount = 0;
    std::uint64_t            g_NoName[3] = { 0, 0, kNameMask };   // stands in for a name's string: no part name hashes to this
    std::uint64_t            g_PartNameCodes[kPartNameCount] = {};

    bool Find(std::uint8_t* list, std::uint64_t name, std::uint64_t* found)
    {
        g_MapFind(list, found, &name);
        return found[0] != 0 && static_cast<std::uint32_t>(found[kFoundIndex]) != kNameMissing;
    }

    void** FileOfName(std::uint8_t* list, std::uint64_t name)
    {
        std::uint64_t found[3] = {};
        if (!Find(list, name, found))
            return nullptr;
        std::uint8_t* values = *reinterpret_cast<std::uint8_t**>(reinterpret_cast<std::uint8_t*>(found[0]) + kListValues);
        return values ? reinterpret_cast<void**>(values + static_cast<std::size_t>(static_cast<std::uint32_t>(found[kFoundIndex])) * kEntrySize) : nullptr;
    }

    void Put(Changes& changes, void** place, void* value)
    {
        if (!place || changes.count >= kMostChanges)
            return;
        changes.made[changes.count].place  = place;
        changes.made[changes.count].before = *place;
        *place = value;
        changes.count = changes.count + 1;
    }

    // the heli looks a part up by name: with another string under the name, it finds none and builds nothing there
    void LeaveOut(Changes& changes, std::uint8_t* list, std::uint64_t name)
    {
        std::uint64_t found[3] = {};
        if (!Find(list, name, found) || found[kFoundNames] == 0)
            return;
        auto* names = reinterpret_cast<std::uint8_t*>(found[kFoundNames]);
        Put(changes, reinterpret_cast<void**>(names + static_cast<std::size_t>(static_cast<std::uint32_t>(found[kFoundIndex])) * kEntrySize), g_NoName);
    }

    bool OutfitLists(const Picked& picked, std::size_t list, std::uint64_t name)
    {
        for (std::size_t i = 0; i < picked.outfitFiles; ++i)
        {
            if (picked.files[i].name == name && static_cast<std::size_t>(picked.files[i].kind) == list)
                return true;
        }
        return false;
    }

    void CollectPicked(Picked& picked)
    {
        static const Kind kinds[] = { Kind::Outfit, Kind::Gatling, Kind::SideGun };
        Part part{};
        for (const Kind kind : kinds)
        {
            if (!SupportHeliParts::PickedPart(kind, part))
                continue;
            for (std::uint8_t i = 0; i < part.modelCount && picked.fileCount < kMostFiles; ++i)
                picked.files[picked.fileCount++] = part.models[i];
            if (kind == Kind::Outfit)
            {
                picked.outfitFiles = picked.fileCount;
                picked.outfitId    = part.id;
                picked.wholeHeli   = part.wholeHeli;
            }
        }
    }

    void RestoreSEH(Changes& changes)
    {
        __try
        {
            for (std::size_t i = changes.count; i-- > 0;)
                *changes.made[i].place = changes.made[i].before;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
        }
        changes.count = 0;
    }

    // the heli type builds its models, effects and motion from TppHeli2Parameter's files once; the picked parts' files stand in for that one call
    std::uint64_t MainName()
    {
        static const std::uint64_t name = FoxHashes::StrCode64("Main") & kNameMask;
        return name;
    }

    std::uint64_t PequodParts()
    {
        static const std::uint64_t path = FoxHashes::PathCode64Ext(kPequodParts);
        return path;
    }

    // fox::BlockPackageContext keeps the heap, the package base and the current package in the game's thread-local block
    std::uint8_t* GameThreadBlockContext()
    {
        const auto* code = reinterpret_cast<const std::uint8_t*>(g_GetCurrentBlockPackage);
        std::int32_t rel = 0;
        std::memcpy(&rel, code + 2, sizeof(rel));
        const std::uint32_t tlsIndex = *reinterpret_cast<const std::uint32_t*>(code + kCurrentPackageTail + rel);
        auto** tlsSlots = reinterpret_cast<std::uint8_t**>(__readgsqword(kTlsSlotsOffset));
        return tlsSlots ? tlsSlots[tlsIndex] : nullptr;
    }

    BlockContext ReadBlockContext(std::uint8_t* tls)
    {
        return { *reinterpret_cast<void**>(tls + kContextHeap), *reinterpret_cast<void**>(tls + kContextBase),
                 *reinterpret_cast<void**>(tls + kContextPackage) };
    }

    void WriteBlockContext(std::uint8_t* tls, const BlockContext& context)
    {
        *reinterpret_cast<void**>(tls + kContextHeap)    = context.heap;
        *reinterpret_cast<void**>(tls + kContextBase)    = context.base;
        *reinterpret_cast<void**>(tls + kContextPackage) = context.package;
    }

    void ReadLoadSEH(std::uint8_t* desc, std::uint8_t*& parameter, void*& block, BlockContext& context)
    {
        __try
        {
            parameter = *reinterpret_cast<std::uint8_t**>(desc + kDescParameter);
            std::uint8_t* package = g_GetCurrentBlockPackage();
            block = package ? *reinterpret_cast<void**>(package + kPackageBlock) : nullptr;
            std::uint8_t* tls = GameThreadBlockContext();
            context = tls ? ReadBlockContext(tls) : BlockContext{};
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            parameter = nullptr;
            block = nullptr;
            context = BlockContext{};
        }
    }

    void ChangeFilesSEH(std::uint8_t* parameter, void* block, const Picked& picked, Changes& changes, std::uint64_t mainName,
                        std::uint64_t pequodParts, std::size_t& missing, bool& isPequod, bool& outfitUsed)
    {
        bool faulted = false;
        __try
        {
            if (!parameter || !block)
                return;
            void** main = FileOfName(parameter + kFileLists[0], mainName);
            if (!main || !*main || *reinterpret_cast<std::uint64_t*>(static_cast<std::uint8_t*>(*main) + kFilePath) != pequodParts)
                return;   // another heli that shares the class
            isPequod = true;

            static thread_local void** places[kMostFiles];
            static thread_local void*  files[kMostFiles];
            std::size_t outfitMissing = 0;
            for (std::size_t i = 0; i < picked.fileCount; ++i)
            {
                const ModelFile& file = picked.files[i];
                places[i] = nullptr;
                files[i]  = nullptr;
                if (file.kind == ModelFileKind::EveryList)
                    continue;
                places[i] = file.kind == ModelFileKind::Mtar ? reinterpret_cast<void**>(parameter + kMotionFile)
                                                             : FileOfName(parameter + kFileLists[static_cast<std::size_t>(file.kind)], file.name);
                files[i]  = places[i] ? g_GetFileForPathId(block, file.path) : nullptr;
                if (!files[i])
                {
                    ++missing;
                    if (i < picked.outfitFiles)
                        ++outfitMissing;
                }
            }
            outfitUsed = picked.outfitId != 0 && !(picked.wholeHeli && outfitMissing != 0);   // half a heli is worse than the Pequod

            for (std::size_t i = outfitUsed ? 0 : picked.outfitFiles; i < picked.fileCount; ++i)
            {
                if (files[i])
                    Put(changes, places[i], files[i]);
            }
            for (std::size_t i = outfitUsed ? 0 : picked.outfitFiles; i < picked.fileCount; ++i)
            {
                if (picked.files[i].kind != ModelFileKind::EveryList)
                    continue;
                for (const std::size_t list : kFileLists)
                    LeaveOut(changes, parameter + list, picked.files[i].name);
            }
            if (outfitUsed && picked.wholeHeli)
            {
                for (std::size_t list = 0; list < SupportHeliParts::kFileListCount; ++list)
                {
                    for (const std::uint64_t name : g_PartNameCodes)
                    {
                        if (!OutfitLists(picked, list, name))
                            LeaveOut(changes, parameter + kFileLists[list], name);
                    }
                }
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            faulted = true;
        }
        if (faulted)
        {
            RestoreSEH(changes);
            outfitUsed = false;
            if (!g_FaultWarned.exchange(true))
                LogWarn("SupportHeliWeapon: the heli's file lists could not be read, custom Pequod parts keep the heli's own model files");
        }
    }

    std::uint64_t __fastcall hkAllocate(std::uint8_t* heliType, std::uint8_t* desc)
    {
        if (!desc || MissionCodeGuard::ShouldBypassHooks())
        {
            SupportHeliParts::SetLoadedOutfit(0);
            return g_Orig(heliType, desc);
        }
        static thread_local Picked  picked;
        static thread_local Changes changes;
        picked = Picked{};
        CollectPicked(picked);

        std::uint8_t* parameter = nullptr;
        void* block = nullptr;
        BlockContext context{};
        ReadLoadSEH(desc, parameter, block, context);
        std::size_t missing = 0;
        bool isPequod = false;
        bool outfitUsed = false;
        changes.count = 0;
        ChangeFilesSEH(parameter, block, picked, changes, MainName(), PequodParts(), missing, isPequod, outfitUsed);
        if (isPequod)
        {
            SupportHeliParts::SetLoadedOutfit(outfitUsed ? picked.outfitId : 0);
            SupportHeliParts::NoteHeliBuilt();
            g_PequodType      = heliType;
            g_PequodParameter = parameter;
            g_PequodBlock     = block;
            g_PequodContext   = context;
            g_PequodRealized  = false;
        }
        if (isPequod && picked.outfitId != 0 && !outfitUsed && !g_WholeHeliWarned.exchange(true))
            LogWarn("SupportHeliWeapon: %u file(s) of the picked whole-heli outfit are not loaded or have an unknown part name, the Pequod keeps its own model",
                    static_cast<unsigned>(missing));
        else if (missing != 0 && !g_MissingFileWarned.exchange(true))
            LogWarn("SupportHeliWeapon: %u custom Pequod model file(s) not loaded or with an unknown part name, the heli keeps its own model there",
                    static_cast<unsigned>(missing));
#ifdef _DEBUG
        if (picked.fileCount != 0)
            LogDebug("PequodModels: %u changes for %u picked files (%u missing), outfit %u %s%s", static_cast<unsigned>(changes.count),
                     static_cast<unsigned>(picked.fileCount), static_cast<unsigned>(missing), static_cast<unsigned>(picked.outfitId),
                     outfitUsed ? "used" : "not used", isPequod ? "" : " (not the Pequod's parts file, left alone)");   // TEMP probe
#endif
        const std::uint64_t result = g_Orig(heliType, desc);
        RestoreSEH(changes);
        return result;
    }

    std::uint8_t* Heli2ImplOfSEH(std::uint8_t* heliType)
    {
        __try
        {
            std::uint8_t* quark = heliType ? *reinterpret_cast<std::uint8_t**>(heliType + kTypeQuark) : nullptr;
            void* registry = quark ? *reinterpret_cast<void**>(quark + kQuarkRegistry) : nullptr;
            if (!registry)
                return nullptr;
            std::uint8_t* found = reinterpret_cast<QuarkFind_t>((*static_cast<void***>(registry))[kRegistryFind])(registry, kHeli2ImplName);
            return found ? found - kQuarkOffset : nullptr;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    void __fastcall hkRealize(std::uint8_t* heli2Impl, std::uint32_t instance, std::uint32_t realizedIndex)
    {
        g_OrigRealize(heli2Impl, instance, realizedIndex);
        if (g_PequodType && heli2Impl && heli2Impl == Heli2ImplOfSEH(g_PequodType))
        {
            g_PequodRealized   = true;
            g_RealizedInstance = instance;
            g_RealizedIndex    = realizedIndex;
        }
    }

    bool IsHeliSpace()
    {
        const std::int32_t missionCode = MissionCodeGuard::GetCurrentMissionCode();
        for (const std::int32_t heliSpace : kHeliSpaces)
        {
            if (missionCode == heliSpace)
                return true;
        }
        return false;
    }

    // the realized heli builds its models from the parameter's files in its constructor: one is unrealized, deleted and built
    // again with the picked files, in the same slot, then realized; the game object and its id stay
    enum class Rebuild { Done, Skipped, Failed };

    std::uint8_t* CreateRealizedSEH(RealizedDesc* desc)
    {
        __try
        {
            return g_CreateRealized(desc);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    Rebuild RebuildSEH(const Picked& picked, Changes& changes, bool& outfitUsed, std::size_t& changeCount)
    {
        std::uint8_t* type = g_PequodType;
        __try
        {
            std::uint8_t*  impl  = Heli2ImplOfSEH(type);
            auto**         list  = *reinterpret_cast<std::uint8_t***>(type + kTypeRealized);
            const auto     count = *reinterpret_cast<std::uint32_t*>(type + kTypeInstances);
            auto**         slots = impl ? *reinterpret_cast<std::uint8_t***>(impl + kImplRealized) : nullptr;
            std::uint8_t*  old   = list ? list[0] : nullptr;
            std::uint8_t*  tls   = GameThreadBlockContext();
            if (!impl || !old || !slots || count != 1 || slots[0] != old || g_RetiredCount >= kMostRetired
                || !tls || !g_PequodContext.package)
            {
                if (!g_LayoutWarned.exchange(true))
                    LogWarn("SupportHeliWeapon: the Pequod's realized heli is not laid out as expected (%u instances), picks show after leaving Customize",
                            static_cast<unsigned>(count));
                return Rebuild::Skipped;
            }

            std::size_t missing = 0;
            bool isPequod = false;
            ChangeFilesSEH(g_PequodParameter, g_PequodBlock, picked, changes, MainName(), PequodParts(), missing, isPequod, outfitUsed);
            if (!isPequod)
            {
                RestoreSEH(changes);
                return Rebuild::Skipped;
            }

            reinterpret_cast<Realize_t>((*reinterpret_cast<void***>(impl))[kImplUnrealize])(impl, g_RealizedInstance, g_RealizedIndex);
            RealizedDesc desc{ *reinterpret_cast<void**>(type + kTypeQuark), g_PequodParameter, 0, 0, 1, 0 };
            const BlockContext menuContext = ReadBlockContext(tls);
            WriteBlockContext(tls, g_PequodContext);
            std::uint8_t* fresh = CreateRealizedSEH(&desc);
            WriteBlockContext(tls, menuContext);
            changeCount = changes.count;
            RestoreSEH(changes);
            if (!fresh)
            {
                g_OrigRealize(impl, g_RealizedInstance, g_RealizedIndex);   // the old one stays in its slots
                return Rebuild::Failed;
            }
            list[0] = fresh;
            if (slots[0] == old)
                slots[0] = fresh;
            g_OrigRealize(impl, g_RealizedInstance, g_RealizedIndex);
            reinterpret_cast<DeleteRealized_t>((*reinterpret_cast<void***>(old))[kRealizedDelete])(old, 1);
            return Rebuild::Done;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            RestoreSEH(changes);
            return Rebuild::Failed;
        }
    }

    void DeleteRetiredSEH()
    {
        __try
        {
            while (g_RetiredCount > 0)
            {
                std::uint8_t* old = g_Retired[--g_RetiredCount];
                g_Retired[g_RetiredCount] = nullptr;
                reinterpret_cast<DeleteRealized_t>((*reinterpret_cast<void***>(old))[kRealizedDelete])(old, 1);
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            g_RetiredCount = 0;
        }
    }

    // the type tears every realized heli down here: the ones a live rebuild replaced go with them
    void __fastcall hkFreeResources(std::uint8_t* heliType)
    {
        if (heliType && heliType == g_PequodType)
        {
            DeleteRetiredSEH();
            g_PequodType = nullptr;
            g_PequodRealized = false;
        }
        g_OrigFreeResources(heliType);
    }

    bool HelperCodeMatches(void* getFile, void* mapFind, void* currentPackage)
    {
        const auto* package = static_cast<const std::uint8_t*>(currentPackage);
        return CheckHookEntry(getFile, kGetFileEntry, sizeof(kGetFileEntry)) != HookEntry::Unknown
            && CheckHookEntry(mapFind, kMapFindEntry, sizeof(kMapFindEntry)) != HookEntry::Unknown
            && std::memcmp(package, kCurrentPackageEntry, sizeof(kCurrentPackageEntry)) == 0
            && std::memcmp(package + kCurrentPackageTail, kCurrentPackageRest, sizeof(kCurrentPackageRest)) == 0;
    }
}

bool Install_TppHeli2_Allocate_Hook()
{
    if (g_Target)
        return true;
    constexpr const char* kNoModels = "custom Pequod parts keep the heli's own model files";
    void* getFile        = gAddr.Fox_Block_GetFileForPathId ? ResolveGameAddress(gAddr.Fox_Block_GetFileForPathId) : nullptr;
    void* mapFind        = gAddr.StringMap_Find ? ResolveGameAddress(gAddr.StringMap_Find) : nullptr;
    void* currentPackage = gAddr.BlockPackageContext_GetCurrentBlockPackage ? ResolveGameAddress(gAddr.BlockPackageContext_GetCurrentBlockPackage) : nullptr;
    if (!getFile || !mapFind || !currentPackage || !HelperCodeMatches(getFile, mapFind, currentPackage))
    {
        LogError("SupportHeliWeapon: file list code missing or changed on this build, %s", kNoModels);
        return false;
    }
    for (std::size_t i = 0; i < kPartNameCount; ++i)
        g_PartNameCodes[i] = FoxHashes::StrCode64(kPartNames[i]) & kNameMask;
    g_GetFileForPathId       = reinterpret_cast<GetFileForPathId_t>(getFile);
    g_MapFind                = reinterpret_cast<MapFind_t>(mapFind);
    g_GetCurrentBlockPackage = reinterpret_cast<GetCurrentBlockPackage_t>(currentPackage);
    g_Target = InstallSupportHeliHook(
        "Pequod model files", gAddr.TppHeli2_Allocate, kAllocateEntry, nullptr, sizeof(kAllocateEntry),
        reinterpret_cast<void*>(&hkAllocate), reinterpret_cast<void**>(&g_Orig), kNoModels);
    if (!g_Target)
        return false;

    constexpr const char* kNoLive = "Pequod picks show after leaving Customize";
    void* create = gAddr.RealizedHeli2Impl_Create ? ResolveGameAddress(gAddr.RealizedHeli2Impl_Create) : nullptr;
    if (create && CheckHookEntry(create, kCreateRealizedEntry, sizeof(kCreateRealizedEntry)) != HookEntry::Unknown)
    {
        g_CreateRealized = reinterpret_cast<CreateRealized_t>(create);
        g_FreeResourcesTarget = InstallSupportHeliHook(
            "Pequod free", gAddr.TppHeli2_FreeResources, kFreeResourcesEntry, nullptr, sizeof(kFreeResourcesEntry),
            reinterpret_cast<void*>(&hkFreeResources), reinterpret_cast<void**>(&g_OrigFreeResources), kNoLive);
        if (g_FreeResourcesTarget)
        {
            g_RealizeTarget = InstallSupportHeliHook(
                "Pequod realize", gAddr.Heli2Impl_Realize, kRealizeEntry, nullptr, sizeof(kRealizeEntry),
                reinterpret_cast<void*>(&hkRealize), reinterpret_cast<void**>(&g_OrigRealize), kNoLive);
        }
    }
    else
        LogError("SupportHeliWeapon: realized heli code missing or changed on this build, %s", kNoLive);
    return true;
}

bool TppHeli2_RebuildPequodModels()
{
    if (!g_PequodType || !g_PequodRealized || !g_CreateRealized || !g_RealizeTarget || !IsHeliSpace()
        || MissionCodeGuard::ShouldBypassHooks())
        return false;
    static Picked  picked;
    static Changes changes;
    picked = Picked{};
    CollectPicked(picked);
    changes.count = 0;
    bool outfitUsed = false;
    std::size_t changeCount = 0;
    const Rebuild result = RebuildSEH(picked, changes, outfitUsed, changeCount);
    if (result == Rebuild::Failed)
    {
        g_PequodRealized = false;
        LogError("SupportHeliWeapon: the Pequod could not be rebuilt for the picked parts");
        return false;
    }
    if (result != Rebuild::Done)
        return false;
    SupportHeliParts::SetLoadedOutfit(outfitUsed ? picked.outfitId : 0);
    SupportHeliParts::NoteHeliBuilt();
#ifdef _DEBUG
    LogDebug("PequodModels: live rebuild, %u changes, outfit %u %s", static_cast<unsigned>(changeCount),
             static_cast<unsigned>(picked.outfitId), outfitUsed ? "used" : "not used");   // TEMP probe
#endif
    return true;
}

#ifdef _DEBUG
void* TppHeli2_ProbePequodFile(unsigned long long pathCode)
{
    if (!g_GetFileForPathId || !g_PequodBlock)
        return nullptr;
    __try
    {
        return g_GetFileForPathId(g_PequodBlock, pathCode);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return nullptr;
    }
}
#endif

void Uninstall_TppHeli2_Allocate_Hook()
{
    UninstallSupportHeliHook(g_RealizeTarget);
    UninstallSupportHeliHook(g_FreeResourcesTarget);
    g_PequodType = nullptr;
    UninstallSupportHeliHook(g_Target);
}
