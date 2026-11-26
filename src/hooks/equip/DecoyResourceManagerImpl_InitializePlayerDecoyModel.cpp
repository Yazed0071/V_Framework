#include <pch.h>
#include "DecoyResourceManagerImpl_InitializePlayerDecoyModel.h"

#include <Windows.h>
#include <cstdint>
#include <cstring>
#include <mutex>

#include "AddressSet.h"
#include "FoxHashes.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "log.h"

namespace
{
    using InitializePlayerDecoyModel_t = void*(__fastcall*)(std::uint8_t* manager, void* result);
    using GetFileForPathId_t = void*(__fastcall*)(void* block, std::uint64_t pathId);

    constexpr std::size_t kPlayerController = 0x68;
    constexpr std::size_t kPlayerEffects    = 0x78;

    constexpr std::size_t kSlotCount       = 0x50;   // SimplePartsControllerImpl
    constexpr std::size_t kPartsFileCount  = 0x08;
    constexpr std::size_t kSlotPartsFile   = 0x10;
    constexpr std::size_t kPartsFiles      = 0x88;
    constexpr std::size_t kPartsFileStride = 0x18;
    constexpr std::size_t kPartsFileBlocks = 0x90;

    constexpr char kCanisterPath[] = "/Assets/tpp/weapon/put/Scenes/pt07_main0_sta.fmdl";
    constexpr char kMotionPath[]   = "/Assets/tpp/motion/mtar/equip/DecoyCommon.mtar";

    constexpr std::uint8_t kEntry[] = {
        0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x57, 0x48, 0x8D, 0x6C, 0x24, 0xC9, 0x48, 0x81, 0xEC, 0xD0, 0x00, 0x00, 0x00,
    };
    constexpr std::uint8_t kPartsFileCountStore[] = { 0xC7, 0x45, 0x8B };   // mov dword [rbp-0x75], imm32
    constexpr std::uint32_t kGamePartsFiles   = 2;
    constexpr std::uint32_t kRaisedPartsFiles = 16;   // a different figure on each of the 12 player decoys, plus the game's

    InitializePlayerDecoyModel_t g_Orig = nullptr;
    void* g_Target = nullptr;
    std::uint8_t* g_CountSite = nullptr;
    void* g_PartsVtable = nullptr;
    GetFileForPathId_t g_GetFileForPathId = nullptr;

    std::mutex g_Lock;
    DecoyPlayerModels g_Models;
    std::uint32_t g_Generation = 0;

    bool SetPartsFileCount(std::uint32_t count)
    {
        std::uint8_t store[sizeof(kPartsFileCountStore) + sizeof(std::uint32_t)] = {};
        if (!g_CountSite || !ReadEntryBytesSEH(g_CountSite, store, sizeof(store))
            || std::memcmp(store, kPartsFileCountStore, sizeof(kPartsFileCountStore)) != 0)
            return false;
        std::uint32_t current = 0;
        std::memcpy(&current, store + sizeof(kPartsFileCountStore), sizeof(current));
        if (current == count)
            return true;
        if (current != kGamePartsFiles && current != kRaisedPartsFiles)
            return false;
        std::uint8_t* imm = g_CountSite + sizeof(kPartsFileCountStore);
        DWORD protect = 0;
        if (!VirtualProtect(imm, sizeof(count), PAGE_EXECUTE_READWRITE, &protect))
            return false;
        std::memcpy(imm, &count, sizeof(count));
        VirtualProtect(imm, sizeof(count), protect, &protect);
        FlushInstructionCache(GetCurrentProcess(), imm, sizeof(count));
        return true;
    }

    bool ReadGameFigureSEH(const std::uint8_t* manager, DecoyPlayerModels* out)
    {
        __try
        {
            auto* controller = *reinterpret_cast<std::uint8_t* const*>(manager + kPlayerController);
            if (!controller || *reinterpret_cast<void* const*>(controller) != g_PartsVtable)
                return false;
            const std::uint32_t slots = *reinterpret_cast<const std::uint32_t*>(controller + kSlotCount);
            const std::uint32_t files = *reinterpret_cast<const std::uint32_t*>(controller + kPartsFileCount);
            if (slots < 2)
                return false;
            const std::uint8_t file = (*reinterpret_cast<std::uint8_t* const*>(controller + kSlotPartsFile))[slots / 2];   // canisters first, then the figures
            if (file >= files)
                return false;
            const auto* fileEntries = *reinterpret_cast<std::uint8_t* const*>(controller + kPartsFiles);
            out->controller = controller;
            out->effects = *reinterpret_cast<void* const*>(manager + kPlayerEffects);
            out->figure = *reinterpret_cast<void* const*>(fileEntries + file * kPartsFileStride);
            out->block = (*reinterpret_cast<void* const* const*>(controller + kPartsFileBlocks))[file];
            return out->figure && out->block;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void RememberGameModels(const std::uint8_t* manager)
    {
        static const std::uint64_t canisterId = FoxHashes::PathCode64Ext(kCanisterPath);
        static const std::uint64_t motionId = FoxHashes::PathCode64Ext(kMotionPath);
        DecoyPlayerModels models;
        if (manager && ReadGameFigureSEH(manager, &models))
        {
            models.canister = g_GetFileForPathId(models.block, canisterId);
            models.motion = g_GetFileForPathId(models.block, motionId);
            if (!models.canister || !models.motion)
            {
                LogWarn("DecoyModel: the game's decoy canister or motion is missing, custom decoys look like the game's");
                models = {};
            }
        }
        else
        {
            models = {};
        }
        std::lock_guard<std::mutex> lock(g_Lock);
        g_Models = models;
        g_Models.generation = ++g_Generation;
    }

    void* __fastcall hkInitializePlayerDecoyModel(std::uint8_t* manager, void* result)
    {
        if (g_CountSite)
            SetPartsFileCount(MissionCodeGuard::ShouldBypassHooks() ? kGamePartsFiles : kRaisedPartsFiles);
        void* made = g_Orig(manager, result);
        RememberGameModels(manager);
        return made;
    }
}

bool Install_DecoyResourceManagerImpl_InitializePlayerDecoyModel_Hook()
{
    if (g_Target)
        return true;
    void* target = ResolveGameAddress(gAddr.DecoyResourceManagerImpl_InitializePlayerDecoyModel);
    auto* site = static_cast<std::uint8_t*>(
        ResolveGameAddress(gAddr.DecoyResourceManagerImpl_InitializePlayerDecoyModel_PartsFileCountSite));
    g_PartsVtable = ResolveGameAddress(gAddr.SimplePartsControllerImpl_Vtable);
    g_GetFileForPathId = reinterpret_cast<GetFileForPathId_t>(ResolveGameAddress(gAddr.Fox_Block_GetFileForPathId));
    if (!target || !site || !g_PartsVtable || !g_GetFileForPathId)
    {
        LogError("DecoyModel: decoy model addresses missing for this build, decoys keep the game's models");
        return false;
    }
    const HookEntry entry = CheckHookEntry(target, kEntry, sizeof(kEntry));
    if (entry == HookEntry::Unknown)
    {
        char desc[160] = {};
        DescribeFunctionEntry(target, desc, sizeof(desc));
        LogError("DecoyModel: DecoyResourceManagerImpl::InitializePlayerDecoyModel differs on this build (%s), "
                 "decoys keep the game's models", desc);
        return false;
    }
    if (entry == HookEntry::ForeignHook)
        LogHookedOnTop("DecoyModel: DecoyResourceManagerImpl::InitializePlayerDecoyModel", target);

    std::uint8_t store[sizeof(kPartsFileCountStore) + sizeof(std::uint32_t)] = {};
    const std::uint32_t gameCount = kGamePartsFiles;
    if (!ReadEntryBytesSEH(site, store, sizeof(store))
        || std::memcmp(store, kPartsFileCountStore, sizeof(kPartsFileCountStore)) != 0
        || std::memcmp(store + sizeof(kPartsFileCountStore), &gameCount, sizeof(gameCount)) != 0)
    {
        LogError("DecoyModel: the decoy parts table differs on this build, custom decoy figures are off");
        site = nullptr;
    }
    if (!CreateAndEnableHook(target, reinterpret_cast<void*>(&hkInitializePlayerDecoyModel),
                             reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("DecoyModel: InitializePlayerDecoyModel hook failed, decoys keep the game's models");
        return false;
    }
    g_Target = target;
    g_CountSite = site;
    return true;
}

void Uninstall_DecoyResourceManagerImpl_InitializePlayerDecoyModel_Hook()
{
    if (g_Target)
        DisableAndRemoveHook(g_Target);
    if (g_CountSite)
        SetPartsFileCount(kGamePartsFiles);
    g_Target = nullptr;
    g_Orig = nullptr;
    g_CountSite = nullptr;
    std::lock_guard<std::mutex> lock(g_Lock);
    g_Models = {};
    g_Models.generation = ++g_Generation;
}

bool DecoyResourceManagerImpl_PlayerModels(DecoyPlayerModels* out)
{
    std::lock_guard<std::mutex> lock(g_Lock);
    *out = g_Models;
    return out->controller != nullptr;
}
