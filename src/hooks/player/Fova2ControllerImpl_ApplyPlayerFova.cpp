#include <pch.h>
#include "Fova2ControllerImpl_ApplyPlayerFova.h"

#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <map>
#include <mutex>
#include <vector>

#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "log.h"
#include "../core/FoxGrModelMesh.h"

namespace
{
    using ApplyPlayerFova_t    = void(__fastcall*)(std::uint8_t* controller, std::uint32_t playerIndex, void* info);
    using GetFova2Controller_t = std::uint8_t*(__fastcall*)(void* owner);

    using MeshStates = std::map<std::uint32_t, bool>;

    constexpr std::uint32_t kPlayerType          = 0;
    constexpr std::uint32_t kPlayerIndexMask     = 0x1FF;

    constexpr std::size_t   kSystemImpl          = 0x08;
    constexpr std::size_t   kImplLocalIndex      = 0x218;
    constexpr std::size_t   kImplOwner           = 0x58;
    constexpr std::size_t   kOwnerFova2Slot      = 0x178;   // owner vtable: its Fova2 controller
    constexpr std::size_t   kControllerWorks     = 0x30;
    constexpr std::size_t   kControllerFirst     = 0x68;   // index of the controller's first work
    constexpr std::size_t   kControllerWorkCount = 0x69;
    constexpr std::size_t   kWorkStride          = 0x160;
    constexpr std::size_t   kWorkPartManagers    = 0x58;   // 11 part resource managers: body, face, hats, horn...
    constexpr std::size_t   kWorkPartCount       = 11;
    constexpr std::size_t   kWorkMainModel       = 0xD0;
    constexpr std::size_t   kWorkState           = 0x152;
    constexpr std::uint8_t  kWorkApplied         = 0x01;
    constexpr std::size_t   kManagerModelCount   = 0x20;
    constexpr std::size_t   kManagerModelArray   = 0x3C;   // offset from the manager to its model array
    constexpr std::size_t   kMaxPlayerModels     = 64;
    constexpr std::size_t   kModelMeshIds        = 0x180;  // fox::gr::Model: its mesh ids (u32), their count a u16 at +0x1E8
    constexpr std::size_t   kModelMeshCount      = 0x1E8;
    constexpr std::uint32_t kMostMeshes          = 512;

    const std::uint8_t kPrologue[] = {
        0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x48,
        0x89, 0x7C, 0x24, 0x20, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x20, 0x0F, 0xB6, 0x41, 0x68
    };

    struct PlayerModels
    {
        void*       models[kMaxPlayerModels] = {};
        std::size_t count = 0;
    };

    ApplyPlayerFova_t g_Orig = nullptr;
    void*             g_Addr = nullptr;

    std::mutex g_Mutex;
    MeshStates g_Meshes;

    struct HiddenMesh
    {
        void*         model;
        std::uint32_t meshId;
    };

    std::vector<HiddenMesh> g_Hidden;   // what SetAllHidden switched off, under g_Mutex
    std::atomic<bool>       g_AllHidden{ false };

    struct LocalWork
    {
        std::uint8_t* controller = nullptr;
        std::uint8_t* work       = nullptr;
        std::uint32_t index      = 0;
    };

    bool LocalWorkSEH(std::uint8_t* system, LocalWork& out)
    {
        __try
        {
            auto* impl = *reinterpret_cast<std::uint8_t**>(system + kSystemImpl);
            if (!impl)
                return false;
            out.index = *reinterpret_cast<std::uint32_t*>(impl + kImplLocalIndex);
            void* owner = *reinterpret_cast<void**>(impl + kImplOwner);
            if (!owner)
                return false;
            out.controller = reinterpret_cast<GetFova2Controller_t>(
                (*reinterpret_cast<void***>(owner))[kOwnerFova2Slot / sizeof(void*)])(owner);
            if (!out.controller)
                return false;
            const std::uint8_t first = out.controller[kControllerFirst];
            const std::uint8_t count = out.controller[kControllerWorkCount];
            auto* works = *reinterpret_cast<std::uint8_t**>(out.controller + kControllerWorks);
            if (!works || out.index < first || out.index - first >= count)
                return false;
            out.work = works + static_cast<std::size_t>(out.index - first) * kWorkStride;
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    bool FindLocalWork(LocalWork& out)
    {
        auto* system = static_cast<std::uint8_t*>(ResolveGameAddress(gAddr.Player2System));
        return system && LocalWorkSEH(system, out);
    }

    void AddModel(PlayerModels& out, void* model)
    {
        if (model && out.count < kMaxPlayerModels)
            out.models[out.count++] = model;
    }

    bool CollectModelsSEH(const std::uint8_t* work, PlayerModels& out)
    {
        __try
        {
            if ((work[kWorkState] & kWorkApplied) == 0)
                return false;
            AddModel(out, *reinterpret_cast<void* const*>(work + kWorkMainModel));
            for (std::size_t part = 0; part < kWorkPartCount; ++part)
            {
                const auto* manager = *reinterpret_cast<const std::uint8_t* const*>(work + kWorkPartManagers + part * sizeof(void*));
                if (!manager)
                    continue;
                const std::uint32_t modelCount = *reinterpret_cast<const std::uint32_t*>(manager + kManagerModelCount);
                const std::uint32_t arrayOffset = *reinterpret_cast<const std::uint32_t*>(manager + kManagerModelArray);
                if (!arrayOffset)
                    continue;
                const auto* models = reinterpret_cast<void* const*>(manager + arrayOffset);
                for (std::uint32_t i = 0; i < modelCount; ++i)
                    AddModel(out, models[i]);
            }
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return false;
        }
    }

    void ApplyToWork(const std::uint8_t* work, const MeshStates& meshes)
    {
        PlayerModels player;
        if (!CollectModelsSEH(work, player))
            return;
        for (const auto& [meshId, visible] : meshes)
            for (std::size_t i = 0; i < player.count; ++i)
                FoxGrModelMesh::SetVisible(player.models[i], meshId, visible);
    }

    std::uint32_t MeshIdsSEH(const void* model, std::uint32_t* ids)
    {
        __try
        {
            const auto* bytes = static_cast<const std::uint8_t*>(model);
            const std::uint32_t count = (std::min)(static_cast<std::uint32_t>(*reinterpret_cast<const std::uint16_t*>(bytes + kModelMeshCount)),
                                                   kMostMeshes);
            const auto* from = *reinterpret_cast<const std::uint32_t* const*>(bytes + kModelMeshIds);
            if (!from)
                return 0;
            std::memcpy(ids, from, count * sizeof(std::uint32_t));
            return count;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return 0;
        }
    }

    void HideAllMeshes(const PlayerModels& player)   // g_Mutex held
    {
        static std::uint32_t ids[kMostMeshes];
        for (std::size_t i = 0; i < player.count; ++i)
        {
            const std::uint32_t count = MeshIdsSEH(player.models[i], ids);
            for (std::uint32_t m = 0; m < count; ++m)
            {
                bool visible = false;
                if (!FoxGrModelMesh::IsVisible(player.models[i], ids[m], visible) || !visible)
                    continue;
                if (FoxGrModelMesh::SetVisible(player.models[i], ids[m], false))
                    g_Hidden.push_back({ player.models[i], ids[m] });
            }
        }
    }

    void ShowHiddenMeshes(const PlayerModels& player)   // g_Mutex held; a model the player no longer has is left alone
    {
        for (const HiddenMesh& hidden : g_Hidden)
            if (std::find(player.models, player.models + player.count, hidden.model) != player.models + player.count)
                FoxGrModelMesh::SetVisible(hidden.model, hidden.meshId, true);
        g_Hidden.clear();
    }

    MeshStates StoredMeshes()
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        return g_Meshes;
    }

    void __fastcall hkApplyPlayerFova(std::uint8_t* controller, std::uint32_t playerIndex, void* info)
    {
        MISSION_GUARD_ORIGINAL_VOID(g_Orig, controller, playerIndex, info);
        g_Orig(controller, playerIndex, info);

        const MeshStates meshes = StoredMeshes();
        const bool allHidden = g_AllHidden.load(std::memory_order_acquire);
        if (meshes.empty() && !allHidden)
            return;
        LocalWork local;
        if (!FindLocalWork(local) || local.controller != controller || local.index != playerIndex)
            return;
        if (!meshes.empty())
            ApplyToWork(local.work, meshes);
        PlayerModels player;
        if (allHidden && CollectModelsSEH(local.work, player))
        {
            std::lock_guard<std::mutex> lock(g_Mutex);
            if (g_AllHidden.load(std::memory_order_relaxed))
                HideAllMeshes(player);
        }
    }
}

namespace PlayerMesh
{
    bool SetVisible(std::uint32_t playerId, std::uint32_t meshId, bool visible)
    {
        if ((playerId >> 9) != kPlayerType || meshId == 0 || !FoxGrModelMesh::IsReady())
            return false;
        LocalWork local;
        const bool live = FindLocalWork(local);
        if (live && (playerId & kPlayerIndexMask) != local.index)
            return false;
        {
            std::lock_guard<std::mutex> lock(g_Mutex);
            g_Meshes[meshId] = visible;
        }
        if (live)
            ApplyToWork(local.work, MeshStates{ { meshId, visible } });
        return true;
    }

    bool IsVisible(std::uint32_t playerId, std::uint32_t meshId, bool& visible)
    {
        if ((playerId >> 9) != kPlayerType || meshId == 0)
            return false;
        LocalWork local;
        PlayerModels player;
        if (!FindLocalWork(local) || (playerId & kPlayerIndexMask) != local.index
            || !CollectModelsSEH(local.work, player))
            return false;
        bool known = false;
        visible = false;
        for (std::size_t i = 0; i < player.count && !visible; ++i)
        {
            bool shown = false;
            if (FoxGrModelMesh::IsVisible(player.models[i], meshId, shown))
            {
                known = true;
                visible = shown;
            }
        }
        return known;
    }

    void ClearForNewMission()
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        g_Meshes.clear();
        g_Hidden.clear();
        g_AllHidden.store(false, std::memory_order_release);
    }

    bool SetAllHidden(bool hidden)
    {
        if (!FoxGrModelMesh::IsReady())
            return false;
        LocalWork local;
        PlayerModels player;
        const bool live = FindLocalWork(local) && CollectModelsSEH(local.work, player);
        std::lock_guard<std::mutex> lock(g_Mutex);
        if (hidden)
        {
            if (!live)
                return false;
            HideAllMeshes(player);
        }
        else if (live)
            ShowHiddenMeshes(player);
        else
            g_Hidden.clear();
        g_AllHidden.store(hidden, std::memory_order_release);
        return true;
    }

    bool AllHidden()
    {
        return g_AllHidden.load(std::memory_order_acquire);
    }
}

bool Install_Fova2ControllerImpl_ApplyPlayerFova_Hook()
{
    if (!gAddr.Fova2ControllerImpl_ApplyPlayerFova)
    {
        LogError("PlayerMesh: no address for ApplyPlayerFova on %s, player mesh visibility is lost on every outfit change",
            GetGameBuildName(gGameBuild));
        return false;
    }

    void* fn = ResolveGameAddress(gAddr.Fova2ControllerImpl_ApplyPlayerFova);
    const HookEntry entry = CheckHookEntry(fn, kPrologue, sizeof(kPrologue));
    if (entry == HookEntry::Unknown)
    {
        char bytes[160] = {};
        DescribeFunctionEntry(fn, bytes, sizeof(bytes));
        LogError("PlayerMesh: ApplyPlayerFova is not the known code on %s (%s), player mesh visibility is lost on every outfit change",
            GetGameBuildName(gGameBuild), bytes);
        return false;
    }
    if (!CreateAndEnableHook(fn, reinterpret_cast<void*>(&hkApplyPlayerFova),
                             reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("PlayerMesh: ApplyPlayerFova hook failed, player mesh visibility is lost on every outfit change");
        return false;
    }
    g_Addr = fn;
    if (entry == HookEntry::ForeignHook)
        LogHookedOnTop("PlayerMesh: ApplyPlayerFova", fn);
    return true;
}

void Uninstall_Fova2ControllerImpl_ApplyPlayerFova_Hook()
{
    if (g_Addr)
    {
        DisableAndRemoveHook(g_Addr);
        g_Addr = nullptr;
    }
    g_Orig = nullptr;
}
