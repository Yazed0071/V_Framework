#include <pch.h>
#include "AnimationControllerImpl_SetGameObjectIdToCharaControl.h"

#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>
#include <unordered_map>

#include "AddressSet.h"
#include "HookUtils.h"
#include "MissionCodeGuard.h"
#include "log.h"
#include "FoxGrModelMesh.h"

namespace
{
    using SetGameObjectId_t = void(__fastcall*)(std::uint8_t* controller, std::uint32_t realizedIndex, std::uint16_t gameObjectId);
    using GetModel_t        = void*(__fastcall*)(std::uint8_t* controller, std::uint32_t realizedIndex);

    using MeshStates = std::map<std::uint32_t, bool>;

    constexpr std::uint16_t kNoGameObject = 0xFFFF;   // what Unrealize binds to free the slot
    constexpr std::size_t   kGetModelSlot = 0x3F0;    // AnimationControllerImpl vtable: model of a realized index
    constexpr std::uint32_t kPlayerType   = 0;
    constexpr std::uint32_t kSoldierType  = 2;

    const std::uint8_t kPrologue[] = {
        0x48, 0x83, 0xEC, 0x28, 0x2B, 0x51, 0x0C, 0x48, 0x8B, 0x49, 0x10, 0x4C, 0x8D, 0x0C, 0x52, 0x41,
        0x0F, 0xB7, 0xD0, 0x49, 0xC1, 0xE1, 0x06, 0x49, 0x8B, 0x4C, 0x09, 0x60
    };

    struct Binding
    {
        std::uint8_t* controller    = nullptr;
        std::uint32_t realizedIndex = 0;
        void*         model         = nullptr;
        MeshStates    originals;   // each mesh we changed on this model, as the game had it
    };

    SetGameObjectId_t g_Orig = nullptr;
    void*             g_Addr = nullptr;

    std::atomic<void*> g_ControllerVtable{ nullptr };

    std::mutex                                     g_Mutex;
    std::unordered_map<std::uint16_t, MeshStates>  g_MeshesByObject;
    std::unordered_map<std::uint16_t, Binding>     g_Bindings;

    bool HasOwnPath(std::uint16_t gameObjectId)
    {
        const std::uint32_t type = gameObjectId >> 9;
        return type == kPlayerType || type == kSoldierType;
    }

    void* ModelSEH(std::uint8_t* controller, std::uint32_t realizedIndex)
    {
        __try
        {
            if (*reinterpret_cast<void**>(controller) != g_ControllerVtable.load(std::memory_order_relaxed))
                return nullptr;
            return reinterpret_cast<GetModel_t>((*reinterpret_cast<void***>(controller))[kGetModelSlot / sizeof(void*)])(
                controller, realizedIndex);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            return nullptr;
        }
    }

    bool IsLive(const Binding& binding)
    {
        return binding.model && ModelSEH(binding.controller, binding.realizedIndex) == binding.model;
    }

    void ApplyMesh(Binding& binding, std::uint32_t meshId, bool visible)
    {
        if (binding.originals.find(meshId) == binding.originals.end())
        {
            bool original = false;
            if (FoxGrModelMesh::IsVisible(binding.model, meshId, original))
                binding.originals[meshId] = original;
        }
        FoxGrModelMesh::SetVisible(binding.model, meshId, visible);
    }

    void RestoreOriginals(const Binding& binding)
    {
        for (const auto& [meshId, visible] : binding.originals)
            FoxGrModelMesh::SetVisible(binding.model, meshId, visible);
    }

    void ReleaseSlot(std::uint8_t* controller, std::uint32_t realizedIndex)
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        for (auto it = g_Bindings.begin(); it != g_Bindings.end(); ++it)
        {
            if (it->second.controller != controller || it->second.realizedIndex != realizedIndex)
                continue;
            if (IsLive(it->second))
                RestoreOriginals(it->second);
            g_Bindings.erase(it);
            return;
        }
    }

    void Bind(std::uint8_t* controller, std::uint32_t realizedIndex, std::uint16_t gameObjectId)
    {
        void* model = ModelSEH(controller, realizedIndex);
        if (!model)
            return;

        std::lock_guard<std::mutex> lock(g_Mutex);
        const auto old = g_Bindings.find(gameObjectId);
        if (old != g_Bindings.end())
        {
            if (old->second.controller == controller && IsLive(old->second))
                RestoreOriginals(old->second);
            g_Bindings.erase(old);
        }

        Binding& binding = g_Bindings[gameObjectId];
        binding.controller = controller;
        binding.realizedIndex = realizedIndex;
        binding.model = model;

        const auto meshes = g_MeshesByObject.find(gameObjectId);
        if (meshes == g_MeshesByObject.end())
            return;
        for (const auto& [meshId, visible] : meshes->second)
            ApplyMesh(binding, meshId, visible);
    }

    void __fastcall hkSetGameObjectIdToCharaControl(std::uint8_t* controller, std::uint32_t realizedIndex, std::uint16_t gameObjectId)
    {
        MISSION_GUARD_ORIGINAL_VOID(g_Orig, controller, realizedIndex, gameObjectId);

        void* expected = nullptr;
        g_ControllerVtable.compare_exchange_strong(expected, *reinterpret_cast<void**>(controller), std::memory_order_relaxed);

        ReleaseSlot(controller, realizedIndex);
        g_Orig(controller, realizedIndex, gameObjectId);
        if (gameObjectId != kNoGameObject && !HasOwnPath(gameObjectId))
            Bind(controller, realizedIndex, gameObjectId);
    }
}

namespace GameObjectMesh
{
    bool SetVisible(std::uint32_t gameObjectId, std::uint32_t meshId, bool visible)
    {
        const auto id = static_cast<std::uint16_t>(gameObjectId);
        if (meshId == 0 || HasOwnPath(id) || !FoxGrModelMesh::IsReady())
            return false;

        std::lock_guard<std::mutex> lock(g_Mutex);
        g_MeshesByObject[id][meshId] = visible;
        const auto bound = g_Bindings.find(id);
        if (bound != g_Bindings.end() && IsLive(bound->second))
            ApplyMesh(bound->second, meshId, visible);
        return true;
    }

    bool IsVisible(std::uint32_t gameObjectId, std::uint32_t meshId, bool& visible)
    {
        const auto id = static_cast<std::uint16_t>(gameObjectId);
        if (meshId == 0 || HasOwnPath(id))
            return false;

        std::lock_guard<std::mutex> lock(g_Mutex);
        const auto bound = g_Bindings.find(id);
        if (bound != g_Bindings.end() && IsLive(bound->second))
            return FoxGrModelMesh::IsVisible(bound->second.model, meshId, visible);

        const auto meshes = g_MeshesByObject.find(id);
        if (meshes == g_MeshesByObject.end())
            return false;
        const auto mesh = meshes->second.find(meshId);
        if (mesh == meshes->second.end())
            return false;
        visible = mesh->second;
        return true;
    }

    void ClearForNewMission()
    {
        std::lock_guard<std::mutex> lock(g_Mutex);
        g_MeshesByObject.clear();
        g_Bindings.clear();
    }
}

bool Install_AnimationControllerImpl_SetGameObjectIdToCharaControl_Hook()
{
    if (!gAddr.AnimationControllerImpl_SetGameObjectIdToCharaControl)
    {
        LogError("ModelMesh: no address for SetGameObjectIdToCharaControl on %s, mesh visibility only works on the player and soldiers",
            GetGameBuildName(gGameBuild));
        return false;
    }

    void* fn = ResolveGameAddress(gAddr.AnimationControllerImpl_SetGameObjectIdToCharaControl);
    const HookEntry entry = CheckHookEntry(fn, kPrologue, sizeof(kPrologue));
    if (entry == HookEntry::Unknown)
    {
        char bytes[160] = {};
        DescribeFunctionEntry(fn, bytes, sizeof(bytes));
        LogError("ModelMesh: SetGameObjectIdToCharaControl is not the known code on %s (%s), mesh visibility only works on the player and soldiers",
            GetGameBuildName(gGameBuild), bytes);
        return false;
    }
    if (!CreateAndEnableHook(fn, reinterpret_cast<void*>(&hkSetGameObjectIdToCharaControl),
                             reinterpret_cast<void**>(&g_Orig)))
    {
        LogError("ModelMesh: SetGameObjectIdToCharaControl hook failed, mesh visibility only works on the player and soldiers");
        return false;
    }
    g_Addr = fn;
    if (entry == HookEntry::ForeignHook)
        LogHookedOnTop("ModelMesh: SetGameObjectIdToCharaControl", fn);
    return true;
}

void Uninstall_AnimationControllerImpl_SetGameObjectIdToCharaControl_Hook()
{
    if (g_Addr)
    {
        DisableAndRemoveHook(g_Addr);
        g_Addr = nullptr;
    }
    g_Orig = nullptr;
}
